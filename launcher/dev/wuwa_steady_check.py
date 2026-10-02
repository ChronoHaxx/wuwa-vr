"""A/B check of the steady desktop view in the OpenXR Simulator.

Shakes the simulated head with the simulator's pose sweep (small, fast sine), then measures how
much the game window's picture oscillates with steadying bypassed and with it on. Stand still in
a quiet spot first. Needs a build with the steady_view test op.

    python wuwa_steady_check.py [--amp 2.0] [--hz 3.0] [--seconds 6]
"""
import argparse
import ctypes
import ctypes.wintypes as wt
import importlib.util
import json
import os
import time
from pathlib import Path

import cv2
import numpy as np
from PIL import ImageGrab

spec = importlib.util.spec_from_file_location("wuwa_live", Path(__file__).with_name("wuwa-test.py"))
live = importlib.util.module_from_spec(spec)
spec.loader.exec_module(live)
SWEEP = Path(os.environ["LOCALAPPDATA"]) / "OpenXR-Simulator" / "pose_sweep_command.json"


def game_rect():
    user32 = ctypes.windll.user32
    hwnd = user32.FindWindowW("UnrealWindow", None)
    if not hwnd:
        raise SystemExit("Wuthering Waves window not found")
    rect = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rect))
    corner = wt.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(corner))
    return corner.x, corner.y, corner.x + rect.right, corner.y + rect.bottom


def sweep(enabled, amp=0.0, hz=0.0):
    SWEEP.write_text(json.dumps({"enabled": enabled, "yaw_amp_deg": amp, "pitch_amp_deg": amp * 0.6,
                                 "roll_amp_deg": amp * 0.5, "freq_hz": hz}), encoding="utf-8")


def capture(rect, seconds):
    """Central 60% of the window (away from the HUD), grey 320x180, with timestamps."""
    left, top, right, bottom = rect
    w, h = right - left, bottom - top
    box = (left + int(w * 0.2), top + int(h * 0.2), right - int(w * 0.2), bottom - int(h * 0.2))
    frames, end = [], time.time() + seconds
    while time.time() < end:
        image = np.asarray(ImageGrab.grab(bbox=box, all_screens=True))
        frames.append(cv2.resize(cv2.cvtColor(image, cv2.COLOR_RGB2GRAY), (320, 180),
                                 interpolation=cv2.INTER_AREA).astype(np.float32))
    return frames, seconds


def oscillation(frames, seconds):
    """RMS distance (px at 320x180) of the picture from its own 1 s moving average."""
    window = cv2.createHanningWindow((320, 180), cv2.CV_32F)
    kept = [frames[0]] + [f for p, f in zip(frames, frames[1:]) if np.abs(f - p).mean() > 0.05]
    steps = np.array([cv2.phaseCorrelate(a, b, window)[0] for a, b in zip(kept, kept[1:])])
    path = np.cumsum(steps, axis=0)
    span = max(3, int(len(kept) / seconds))
    average = np.stack([np.convolve(path[:, i], np.ones(span) / span, "same") for i in (0, 1)], 1)
    residual = (path - average)[span:-span]
    return float(np.sqrt((residual ** 2).sum(1).mean())), len(kept) / seconds


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--amp", type=float, default=2.0, help="yaw amplitude in degrees")
    parser.add_argument("--hz", type=float, default=3.0)
    parser.add_argument("--seconds", type=float, default=6.0)
    args = parser.parse_args()
    test = live.LiveTest()
    rect = game_rect()
    print(f"window {rect[2] - rect[0]}x{rect[3] - rect[1]}")
    results = {}
    try:
        # still: no sweep (picture noise floor); bypassed: sweep, steadying off; steady: sweep, on.
        for label, shaking, bypass in (("still", False, 0), ("bypassed", True, int(args.seconds + 8)),
                                       ("steady", True, 0)):
            sweep(shaking, args.amp, args.hz)
            test.request("steady_view", bypass_seconds=bypass)
            time.sleep(2.5)
            frames, seconds = capture(rect, args.seconds)
            results[label], fps = oscillation(frames, seconds)
            status = test.request("steady_view", bypass_seconds=bypass and 1)["steady"]
            print(f"{label:9s} oscillation {results[label]:6.2f} px  ({fps:.0f} fps)  status {status}")
    finally:
        sweep(False)
        test.request("steady_view", bypass_seconds=0)
    shake = results["bypassed"] - results["still"]
    left = results["steady"] - results["still"]
    print(f"head shake in picture: {shake:.2f} px; left with steady view: {left:.2f} px "
          f"({100 * left / shake if shake > 0 else float('nan'):.0f}%)")
    print("PASS" if shake > 0.5 and left < 0.35 * shake else "CHECK")


if __name__ == "__main__":
    main()
