"""Run the stereo freeze fix bench unattended and measure each candidate.

For a baseline and each candidate: open its timed window through the WuWa test control
(wuwa-test.py bench), let it settle, capture the OpenXR Simulator window for a few
seconds, and report the share of changed pixels inside one box per half (put the boxes
on the same far tree in each eye's image). A frozen tree scores low; a swaying one high.
Ends with a second baseline. The player only has to stand still.

    python wuwa_bench_run.py --pid 1234 --left-box 200,400,60,190 --right-box 570,830,60,190
"""
from __future__ import annotations

import argparse
import subprocess
import sys
import time
from pathlib import Path

import numpy as np
from PIL import ImageGrab

sys.path.insert(0, str(Path(__file__).resolve().parent))
from wuwa_motion_halves import client_box, find_window  # noqa: E402

HERE = Path(__file__).resolve().parent
CANDIDATES = [
    ("baseline", None, 0),
    ("K1 construct pass 2", "construct_mode", 1),
    ("K2 pass 2 + family hidden", "construct_mode", 2),
    ("K3 family hidden", "construct_mode", 3),
    ("baseline again", None, 0),
]


def box(text: str) -> tuple[int, int, int, int]:
    x0, x1, y0, y1 = (int(v) for v in text.split(","))
    return x0, x1, y0, y1


def bench(pid: int, op: str, mode: int, seconds: int) -> None:
    subprocess.run([sys.executable, str(HERE / "wuwa-test.py"), "bench", "--pid", str(pid), "--op", op,
                    "--mode", str(mode), "--seconds", str(seconds)], check=True, stdout=subprocess.DEVNULL)


def measure(window_box, left, right, seconds: float, interval: float, menu_px: int, threshold: int):
    frames = []
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        frames.append(np.asarray(ImageGrab.grab(bbox=window_box, all_screens=True).convert("RGB"), dtype=np.int16)[menu_px:])
        time.sleep(interval)
    moved = np.zeros(frames[0].shape[:2], bool)
    for a, b in zip(frames, frames[1:]):
        moved |= np.abs(a - b).max(axis=2) > threshold
    share = lambda b: float(moved[b[2]:b[3], b[0]:b[1]].mean())
    ground = float(moved[int(moved.shape[0] * 0.75):].mean())   # camera-drift check: floor should stay still
    return share(left), share(right), ground


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pid", type=int, required=True)
    ap.add_argument("--left-box", type=box, required=True, help="x0,x1,y0,y1 of the far tree in the left half")
    ap.add_argument("--right-box", type=box, required=True, help="x0,x1,y0,y1 of the same tree in the right half")
    ap.add_argument("--settle", type=float, default=4.0)
    ap.add_argument("--seconds", type=float, default=6.0)
    ap.add_argument("--interval", type=float, default=0.5)
    ap.add_argument("--menu-px", type=int, default=24)
    ap.add_argument("--threshold", type=int, default=12)
    args = ap.parse_args()
    window_box = client_box(find_window("OpenXR Simulator"))
    print(f"{'candidate':28} {'left (views[1])':>16} {'right (views[0])':>17} {'floor drift':>12}")
    for name, op, mode in CANDIDATES:
        if op:
            bench(args.pid, op, mode, int(args.settle + args.seconds + 5))
        time.sleep(args.settle)
        left, right, ground = measure(window_box, args.left_box, args.right_box, args.seconds,
                                      args.interval, args.menu_px, args.threshold)
        if op:
            bench(args.pid, op, 0, 0)
        print(f"{name:28} {left:16.0%} {right:17.0%} {ground:12.1%}", flush=True)
        time.sleep(2)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
