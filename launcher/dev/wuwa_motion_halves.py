"""Measure per-half motion in the OpenXR Simulator window (both eyes side by side).

Grabs the simulator window every --interval seconds for --seconds, then reports, for the
left and right half separately, how many pixels changed between frames: overall and in
bands from top to bottom (far scenery usually sits in the upper bands). A frozen far tree
shows as a band that changes in one half but not the other. Writes a motion map image
(white = changed at least once) with both halves side by side. Read-only.

    python wuwa_motion_halves.py --out E:/.../motion --seconds 6
"""
from __future__ import annotations

import argparse
import ctypes
import time
from ctypes import wintypes
from pathlib import Path

import numpy as np
from PIL import Image, ImageGrab

user32 = ctypes.windll.user32
ctypes.windll.shcore.SetProcessDpiAwareness(2)


def find_window(prefix: str) -> int:
    found = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def callback(hwnd, _):
        length = user32.GetWindowTextLengthW(hwnd)
        if length and user32.IsWindowVisible(hwnd):
            buffer = ctypes.create_unicode_buffer(length + 1)
            user32.GetWindowTextW(hwnd, buffer, length + 1)
            if buffer.value.startswith(prefix):
                found.append(hwnd)
        return True

    user32.EnumWindows(callback, 0)
    if not found:
        raise SystemExit(f"No visible window titled '{prefix}...'")
    return found[0]


def client_box(hwnd: int) -> tuple[int, int, int, int]:
    rect = wintypes.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rect))
    origin = wintypes.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(origin))
    return origin.x, origin.y, origin.x + rect.right, origin.y + rect.bottom


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--seconds", type=float, default=6.0)
    ap.add_argument("--interval", type=float, default=0.5)
    ap.add_argument("--title", default="OpenXR Simulator")
    ap.add_argument("--menu-px", type=int, default=24, help="rows to skip at the top of the client area (menu bar)")
    ap.add_argument("--threshold", type=int, default=12, help="per-channel change that counts as motion")
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    hwnd = find_window(args.title)
    box = client_box(hwnd)
    frames = []
    end = time.monotonic() + args.seconds
    while time.monotonic() < end:
        frames.append(np.asarray(ImageGrab.grab(bbox=box, all_screens=True).convert("RGB"), dtype=np.int16))
        time.sleep(args.interval)
    frames = [f[args.menu_px:] for f in frames]
    Image.fromarray(frames[0].astype(np.uint8)).save(args.out / "first.png")
    moved = np.zeros(frames[0].shape[:2], bool)
    for a, b in zip(frames, frames[1:]):
        moved |= (np.abs(a - b).max(axis=2) > args.threshold)
    h, w = moved.shape
    half = w // 2
    bands = 6
    print(f"{len(frames)} frames, {w}x{h}; changed-pixel share per half (band 1 = top)")
    for name, part in (("left ", moved[:, :half]), ("right", moved[:, half:half * 2])):
        rows = [part[i * h // bands:(i + 1) * h // bands].mean() for i in range(bands)]
        print(f"  {name}: overall {part.mean():6.2%}  bands " + " ".join(f"{r:6.2%}" for r in rows))
    Image.fromarray((moved * 255).astype(np.uint8)).save(args.out / "motion-map.png")
    print(f"wrote {args.out / 'motion-map.png'} and {args.out / 'first.png'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
