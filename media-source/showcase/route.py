#!/usr/bin/env python3
"""Exercise-session tools: route, stats, Strava-style card, synced timelapse.

    load [--pid N]           parse the session's motion logs into route-cache.npz
    summary [--run N]        Strava-style summary card (PNG) for a run
    sync VIDEO               find the Start/End run flashes in an OBS video -> video clock offset
    timelapse VIDEO [--run N --length 75]
                             stabilised timelapse with a live route map and stats

Data: motion logs written by launcher/dev/wuwa_route_log.py (UEVR profile\\recordings) and run
markers from WuWa controls > Recording > Start/End run (UEVR profile\\wuwa-runs.jsonl). Positions
are UE units (cm) relative to WuWa's current world origin, which resets on teleports, so each
teleport starts a new route segment.
"""
from __future__ import annotations

import argparse
import datetime
import glob
import json
import math
import os
import subprocess
import sys
from pathlib import Path

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont

PROFILE = Path(os.path.expandvars(r"%APPDATA%\UnrealVRMod\Client-Win64-Shipping"))
WORK = Path(os.path.expandvars(r"%LOCALAPPDATA%\WuWa VR Launcher\routes"))
TELEPORT_CM = 3000          # a jump this large between samples (<1 s apart) is a teleport
GAP_S = 3.0                 # no samples for this long also splits the route
FONT = {"bold": "C:/Windows/Fonts/segoeuib.ttf", "semi": "C:/Windows/Fonts/seguisb.ttf",
        "regular": "C:/Windows/Fonts/segoeui.ttf"}
INK, MUTED, ACCENT, BG = (240, 242, 245), (150, 158, 170), (252, 82, 0), (16, 19, 24)


def font(kind, size):
    return ImageFont.truetype(FONT[kind], size)


# ---------------------------------------------------------------- data

def load(pid: int | None) -> Path:
    files = glob.glob(str(PROFILE / "recordings" / "motion-*.jsonl"))
    if pid is None:   # newest game session
        pid = int(max(files, key=os.path.getmtime).split("motion-")[1].split("-")[0])
    files = sorted((f for f in files if f.split("motion-")[1].split("-")[0] == str(pid)),
                   key=lambda f: int(f.split("-")[-1].split(".")[0]))
    rows = {}
    for path in files:
        with open(path, encoding="utf-8") as source:
            for line in source:
                try:
                    r = json.loads(line)
                except ValueError:
                    continue            # a line cut off when the game closed
                c = r.get("camera") or {}
                if r.get("type") != "frame" or not c.get("pawn"):
                    continue
                view = (c.get("rendered_views") or [{}])[0]
                rot = view.get("rotation") or [0.0, 0.0, 0.0]
                p = c["pawn"]
                rows[r["unix_ms"]] = (r["unix_ms"] / 1000.0, p["x"], p["y"], p["z"], rot[0], rot[1], rot[2],
                                      float(bool(c.get("game_menu"))), float(bool(c.get("uevr_menu"))))
    data = np.array([rows[k] for k in sorted(rows)])
    runs, start = [], None
    marker_file = PROFILE / "wuwa-runs.jsonl"
    if marker_file.is_file():
        for line in marker_file.read_text(encoding="utf-8").splitlines():
            event = json.loads(line)
            t = event["unix_ms"] / 1000.0
            if not (data[0, 0] - 60 <= t <= data[-1, 0] + 60):
                continue
            if event["event"] == "start":
                start = t
            elif start is not None:
                runs.append((start, t))
                start = None
        if start is not None:
            runs.append((start, data[-1, 0]))
    WORK.mkdir(parents=True, exist_ok=True)
    out = WORK / f"route-cache-{pid}.npz"
    np.savez_compressed(out, data=data, runs=np.array(runs).reshape(-1, 2), pid=pid)
    print(f"{len(files)} logs, {len(data)} samples, "
          f"{datetime.datetime.fromtimestamp(data[0, 0]):%H:%M:%S}-{datetime.datetime.fromtimestamp(data[-1, 0]):%H:%M:%S}, "
          f"{len(runs)} run(s) -> {out}")
    for i, (a, b) in enumerate(runs, 1):
        print(f"  run {i}: {datetime.datetime.fromtimestamp(a):%H:%M:%S} for {(b - a) / 60:.1f} min")
    return out


def latest_cache() -> Path:
    caches = sorted(WORK.glob("route-cache-*.npz"), key=os.path.getmtime)
    if not caches:
        sys.exit("No route cache yet: run `route.py load` first.")
    return caches[-1]


def run_slice(cache: Path, run: int | None):
    z = np.load(cache)
    data, runs = z["data"], z["runs"]
    if len(runs) == 0:
        return data, (data[0, 0], data[-1, 0]), 0
    if run is None:   # the longest run
        run = int(np.argmax(runs[:, 1] - runs[:, 0])) + 1
    a, b = runs[run - 1]
    return data[(data[:, 0] >= a) & (data[:, 0] <= b)], (a, b), run


def segments(d):
    """Indices where the route breaks (teleport or gap) -> list of index ranges."""
    dt = np.diff(d[:, 0])
    jump = np.hypot(np.diff(d[:, 1]), np.diff(d[:, 2]))
    breaks = np.flatnonzero((dt > GAP_S) | ((jump > TELEPORT_CM) & (dt < 1.0))) + 1
    edges = [0, *breaks.tolist(), len(d)]
    return [(a, b) for a, b in zip(edges, edges[1:]) if b - a > 5]


def stats(d):
    segs = segments(d)
    distance = climb = moving = 0.0
    for a, b in segs:
        s = d[a:b]
        step = np.hypot(np.diff(s[:, 1]), np.diff(s[:, 2])) / 100.0           # m
        dt = np.diff(s[:, 0])
        distance += step.sum()
        moving += dt[(step / np.maximum(dt, 1e-3)) > 0.4].sum()                 # walking pace or more
        z = np.convolve(s[:, 3], np.ones(15) / 15, "valid") / 100.0            # smooth jitter
        rise = np.diff(z)
        climb += rise[rise > 0].sum()
    return {"distance_m": distance, "climb_m": climb, "moving_s": moving,
            "elapsed_s": d[-1, 0] - d[0, 0], "segments": len(segs), "teleports": len(segs) - 1}


# ---------------------------------------------------------------- drawing

def map_points(d, box, segs):
    """World x/y (cm) -> pixel coordinates fitted into box=(x0, y0, x1, y1), equal aspect.

    Each segment after a teleport has its own origin; they are drawn side by side."""
    x0, y0, x1, y1 = box
    parts, offset = [], 0.0
    for a, b in segs:
        s = d[a:b, 1:3].copy()
        s[:, 0] -= s[:, 0].min() - offset
        offset = s[:, 0].max() + 4000               # 40 m between unrelated segments
        parts.append(s)
    allp = np.vstack(parts)
    lo, hi = allp.min(axis=0), allp.max(axis=0)
    scale = min((x1 - x0) / max(hi[0] - lo[0], 1), (y1 - y0) / max(hi[1] - lo[1], 1))
    pad = ((x1 - x0) - (hi[0] - lo[0]) * scale) / 2, ((y1 - y0) - (hi[1] - lo[1]) * scale) / 2
    return [np.column_stack([x0 + pad[0] + (p[:, 0] - lo[0]) * scale, y0 + pad[1] + (p[:, 1] - lo[1]) * scale])
            for p in parts]


def draw_route(draw, pts_list, width, upto=None, glow=True):
    for k, pts in enumerate(pts_list):
        if upto is not None:
            pts = pts[:max(0, upto[k])]
        if len(pts) < 2:
            continue
        flat = [tuple(p) for p in pts[::2]] + [tuple(pts[-1])]
        if glow:
            draw.line(flat, fill=(252, 82, 0, 70), width=width * 3, joint="curve")
        draw.line(flat, fill=ACCENT, width=width, joint="curve")


def fmt_time(seconds):
    seconds = int(round(seconds))
    return f"{seconds // 3600}:{seconds // 60 % 60:02d}:{seconds % 60:02d}" if seconds >= 3600 \
        else f"{seconds // 60}:{seconds % 60:02d}"


def summary(cache: Path, run: int | None, title: str, real: dict) -> Path:
    d, (a, b), run = run_slice(cache, run)
    s = stats(d)
    segs = segments(d)
    W, H, SS = 1920, 1080, 2
    img = Image.new("RGB", (W * SS, H * SS), BG)
    draw = ImageDraw.Draw(img, "RGBA")
    # Map panel with a faint grid
    mx0, my0, mx1, my1 = 70 * SS, 170 * SS, 1180 * SS, 820 * SS
    draw.rounded_rectangle((mx0 - 30 * SS, my0 - 30 * SS, mx1 + 30 * SS, my1 + 30 * SS), 24 * SS, fill=(24, 28, 35))
    for gx in range(mx0, mx1, 80 * SS):
        draw.line((gx, my0 - 20 * SS, gx, my1 + 20 * SS), fill=(255, 255, 255, 10), width=SS)
    for gy in range(my0, my1, 80 * SS):
        draw.line((mx0 - 20 * SS, gy, mx1 + 20 * SS, gy), fill=(255, 255, 255, 10), width=SS)
    pts = map_points(d, (mx0, my0, mx1, my1), segs)
    draw_route(draw, pts, 7 * SS)
    for k, p in enumerate(pts):
        r = 13 * SS
        draw.ellipse((p[0, 0] - r, p[0, 1] - r, p[0, 0] + r, p[0, 1] + r), fill=(60, 200, 120) if k == 0 else (90, 160, 255),
                     outline=BG, width=4 * SS)
        if k == len(pts) - 1:
            draw.ellipse((p[-1, 0] - r, p[-1, 1] - r, p[-1, 0] + r, p[-1, 1] + r), fill=(235, 60, 60), outline=BG, width=4 * SS)
    # Title
    draw.text((70 * SS, 58 * SS), title, font=font("bold", 54 * SS), fill=INK)
    when = datetime.datetime.fromtimestamp(a)
    draw.text((72 * SS, 124 * SS), f"{when:%A %d %B %Y, %H:%M}  ·  Wuthering Waves in VR  ·  Reality Runner treadmill",
              font=font("regular", 24 * SS), fill=MUTED)
    # Stat tiles
    tiles = [("Distance in game", f"{s['distance_m'] / 1000:.2f} km"),
             ("Moving time", fmt_time(s["moving_s"])),
             ("Avg speed", f"{s['distance_m'] / max(s['moving_s'], 1) * 3.6:.1f} km/h"),
             ("Elevation gain", f"{s['climb_m']:.0f} m")]
    if real.get("distance_km") is not None:
        tiles.append(("Walked for real", f"{real['distance_km']:.2f} km"))
    if real.get("calories") is not None:
        tiles.append(("Calories", f"{real['calories']:.0f}"))
    tx, ty = 1270 * SS, 150 * SS
    for i, (label, value) in enumerate(tiles):
        y = ty + i * 112 * SS
        draw.text((tx, y), label, font=font("semi", 24 * SS), fill=MUTED)
        draw.text((tx, y + 30 * SS), value, font=font("bold", 56 * SS), fill=INK)
    # Elevation profile
    px0, py0, px1, py1 = 70 * SS, 880 * SS, 1850 * SS, 1030 * SS
    dist = [0.0]
    for (sa, sb) in segs:
        seg = d[sa:sb]
        step = np.hypot(np.diff(seg[:, 1]), np.diff(seg[:, 2])) / 100.0
        dist.extend(dist[-1] + np.cumsum(step))
        dist.append(dist[-1])
    dist = np.array(dist[:len(d)]) if len(dist) >= len(d) else np.linspace(0, s["distance_m"], len(d))
    z = np.concatenate([d[sa:sb, 3] for sa, sb in segs]) / 100.0
    dist = dist[:len(z)]
    if len(z) > 10:
        zs = np.convolve(z, np.ones(15) / 15, "same")
        zmin, zmax = zs.min(), max(zs.max(), zs.min() + 10)
        xs = px0 + (dist / max(dist[-1], 1)) * (px1 - px0)
        ys = py1 - (zs - zmin) / (zmax - zmin) * (py1 - py0)
        poly = [(xs[0], py1)] + list(zip(xs[::3], ys[::3])) + [(xs[-1], py1)]
        draw.polygon(poly, fill=(252, 82, 0, 60))
        draw.line(list(zip(xs[::3], ys[::3])), fill=ACCENT, width=3 * SS)
        draw.text((px0, py0 - 40 * SS), f"Elevation  {zmin:.0f}–{zmax:.0f} m (relative)", font=font("semi", 22 * SS), fill=MUTED)
    if s["teleports"]:
        draw.text((mx0, my1 + 40 * SS), f"{s['teleports']} teleport(s): each part drawn separately",
                  font=font("regular", 22 * SS), fill=MUTED)
    out = cache.with_name(f"summary-run{run}-{datetime.datetime.fromtimestamp(a):%Y%m%d-%H%M}.png")
    img.resize((W, H), Image.LANCZOS).save(out)
    print(json.dumps({k: round(v, 1) if isinstance(v, float) else v for k, v in s.items()}), "->", out)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("load")
    p.add_argument("--pid", type=int)
    p = sub.add_parser("summary")
    p.add_argument("--run", type=int)
    p.add_argument("--title", default="Treadmill walk across Solaris-3")
    p.add_argument("--real-km", type=float)
    p.add_argument("--calories", type=float)
    args = parser.parse_args()
    if args.command == "load":
        load(args.pid)
    elif args.command == "summary":
        summary(latest_cache(), args.run, args.title, {"distance_km": args.real_km, "calories": args.calories})


if __name__ == "__main__":
    main()
