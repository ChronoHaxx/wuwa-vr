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
    if real.get("time"):
        tiles.append(("Treadmill time", real["time"]))
    if real.get("calories") is not None:
        tiles.append(("Calories", f"{real['calories']:.0f} kcal"))
    tx, ty = 1270 * SS, 150 * SS
    pitch = min(112, 680 // len(tiles))                   # keep clear of the elevation profile
    value_size = 56 if pitch >= 112 else 48
    for i, (label, value) in enumerate(tiles):
        y = ty + i * pitch * SS
        draw.text((tx, y), label, font=font("semi", 24 * SS), fill=MUTED)
        draw.text((tx, y + 28 * SS), value, font=font("bold", value_size * SS), fill=INK)
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
    tag = "-real" if any(v is not None for v in real.values()) else ""
    out = cache.with_name(f"summary-run{run}-{datetime.datetime.fromtimestamp(a):%Y%m%d-%H%M}{tag}.png")
    img.resize((W, H), Image.LANCZOS).save(out)
    print(json.dumps({k: round(v, 1) if isinstance(v, float) else v for k, v in s.items()}), "->", out)
    return out


# ---------------------------------------------------------------- video sync and timelapse

SRC_FPS = 60
OUT_W, OUT_H, OUT_FPS = 1920, 1080, 30


def probe(video: Path):
    """(first video frame time, width, height, duration) of an OBS recording."""
    info = json.loads(subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
         "stream=start_time,width,height:format=duration", "-of", "json", str(video)],
        capture_output=True, text=True, check=True).stdout)
    s = info["streams"][0]
    return float(s.get("start_time") or 0), s["width"], s["height"], float(info["format"]["duration"])


def decode(video: Path, times, w: int, h: int, scale: bool = True):
    """RGB frames at the given video times (s, on the frame grid, ascending), one per time.

    Works in chunks: each ffmpeg run seeks to just before its first frame and passes only the
    frames asked for (select on the original timestamps, -copyts), so a 40-minute 1440p file
    costs one decode pass and nothing else crosses the pipe."""
    size, last, i = w * h * 3, None, 0
    while i < len(times):
        j = i + 1
        while j < len(times) and j - i < 120 and times[j] - times[j - 1] < 20:
            j += 1
        chunk = times[i:j]
        start = max(0.0, chunk[0] - 1.0)
        vf = "select='" + "+".join(f"lt(abs(t-{p:.4f}),0.004)" for p in chunk) + "'"
        if scale:
            vf += f",scale={w}:{h}:flags=area"
        proc = subprocess.Popen(["ffmpeg", "-v", "error", "-hwaccel", "auto", "-copyts", "-ss", f"{start:.3f}",
                                 "-t", f"{chunk[-1] - start + 1.0:.3f}", "-i", str(video), "-an", "-vf", vf,
                                 "-fps_mode", "passthrough", "-pix_fmt", "rgb24", "-f", "rawvideo", "-"],
                                stdout=subprocess.PIPE, stdin=subprocess.DEVNULL)
        got = 0
        try:
            while got < len(chunk):
                raw = proc.stdout.read(size)
                if len(raw) < size:
                    break
                last = np.frombuffer(raw, np.uint8).reshape(h, w, 3)
                got += 1
                yield last
        finally:
            proc.kill()
            proc.wait()
        if got < len(chunk):
            if last is None:
                raise SystemExit(f"ffmpeg returned no frames near {chunk[0]:.1f} s")
            print(f"  {len(chunk) - got} frame(s) missing near {chunk[got]:.1f} s; repeating the previous one")
            for _ in range(len(chunk) - got):
                yield last
        i = j


def video_offset(video: Path, quiet: bool = False) -> float:
    """Video time = unix time - file creation time + offset, measured from the Start/End run flashes."""
    ctime = os.path.getctime(video)
    s0, _, _, duration = probe(video)
    marker_file = PROFILE / "wuwa-runs.jsonl"
    presses = [json.loads(line)["unix_ms"] / 1000.0
               for line in marker_file.read_text(encoding="utf-8").splitlines() if line.strip()] \
        if marker_file.is_file() else []
    offsets = []
    for press in presses:
        guess = press - ctime
        if not 2 < guess < duration - 3:
            continue
        first = s0 + round((guess - 1 - s0) * SRC_FPS) / SRC_FPS
        grid = [first + i / SRC_FPS for i in range(3 * SRC_FPS)]
        frames = decode(video, grid, 64, 36)
        for i, f in enumerate(frames):
            corner = f[:3, :3].astype(int)       # the square is 1/10 of the height, top-left
            if (corner[..., 0] > 170).all() and (corner[..., 2] > 170).all() and (corner[..., 1] < 100).all():
                offsets.append(grid[i] - guess)
                break
        frames.close()
    if not offsets:
        if not quiet:
            print("No Start/End run flash found; assuming the file was created as recording began.")
        return 0.0
    offset = float(np.median(offsets))
    if not quiet:
        print(f"Sync: {len(offsets)} flash(es), offset {offset:+.3f} s (spread {np.ptp(offsets) * 1000:.0f} ms)")
    return offset


def rotation_matrix(pitch, yaw, roll):
    """UE FRotator (degrees) -> rows = camera forward, right, up in world space."""
    p, y, r = np.radians([pitch, yaw, roll])
    sp, cp, sy, cy, sr, cr = np.sin(p), np.cos(p), np.sin(y), np.cos(y), np.sin(r), np.cos(r)
    return np.array([[cp * cy, cp * sy, sp],
                     [sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp],
                     [-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp]])


def intrinsics(w, h, f):
    """Camera coordinates (forward, right, up) -> homogeneous pixels."""
    return np.array([[w / 2, f, 0], [h / 2, 0, -f], [1, 0, 0]])


def gaussian_smooth(values, sigma):
    if sigma <= 0 or len(values) < 2:
        return np.array(values, float)
    radius = int(3 * sigma) + 1
    kernel = np.exp(-0.5 * (np.arange(-radius, radius + 1) / sigma) ** 2)
    return np.convolve(np.pad(values, radius, mode="edge"), kernel / kernel.sum(), "valid")


def hide_uid(frame):
    """Paint over WuWa's "User ID" line, which sits at a fixed spot bottom-right of the game window."""
    h, w = frame.shape[:2]
    s = h / 1440
    x0, x1, y0 = w - int(305 * s), w - int(60 * s), h - int(24 * s)
    pad = int(12 * s) + 1
    frame = frame.copy()
    region = frame[y0 - pad:h, x0 - pad:x1 + pad]
    mask = np.zeros(region.shape[:2], np.uint8)
    mask[pad:, pad:pad + x1 - x0] = 255
    region[:] = cv2.inpaint(region, mask, 5, cv2.INPAINT_TELEA)
    return frame


def timelapse(video: Path, cache: Path, run: int | None, length: float, title: str, real: dict,
              fov: float, zoom: float, out: Path | None, stills: int, cover_uid: bool = True) -> Path:
    d, (a, b), run = run_slice(cache, run)
    ctime = os.path.getctime(video)
    s0, src_w, src_h, _ = probe(video)
    offset = video_offset(video)
    t = d[:, 0]
    rot = np.column_stack([d[:, 4], np.degrees(np.unwrap(np.radians(d[:, 5]))), d[:, 6]])   # pitch, yaw, roll

    def rot_at(times):
        return np.column_stack([np.interp(times, t, rot[:, j]) for j in range(3)])

    # Route: distance along it (teleports and gaps add nothing), height and climb so far
    step = np.hypot(np.diff(d[:, 1]), np.diff(d[:, 2])) / 100.0
    breaks = (step > TELEPORT_CM / 100.0) | (np.diff(t) > GAP_S)
    step[breaks] = 0.0
    dist = np.concatenate([[0.0], np.cumsum(step)])
    height = gaussian_smooth(d[:, 3] / 100.0, 8)
    rise = np.diff(height)
    rise[np.convolve(breaks, np.ones(33), "same") > 0] = 0.0
    climb = np.concatenate([[0.0], np.cumsum(np.clip(rise, 0, None))])

    # Frame times: mostly even in distance, partly in time, so standing still passes quickly.
    # Time in the UEVR menu counts for nothing, so no frame lands there.
    in_menu = d[:, 8] > 0
    playing = np.concatenate([[0.0], np.cumsum(np.diff(t) * ~in_menu[1:])])
    progress = 0.75 * dist / max(dist[-1], 1.0) + 0.25 * playing / max(playing[-1], 1.0) + 1e-9 * np.arange(len(t))
    first, last = int(np.argmax(~in_menu)), len(t) - 1 - int(np.argmax(~in_menu[::-1]))
    n = int(round(length * OUT_FPS))
    target = np.interp(np.linspace(progress[first], progress[last], n), progress, t)
    # Near each target, take the moment the head was closest to a smooth path: less to correct
    ref = rot_at(target)
    ref = np.column_stack([gaussian_smooth(ref[:, j], s) for j, s in enumerate((8, 5, 8))])
    spacing = np.gradient(target)
    chosen = target.copy()
    for k in range(n):
        w = min(0.5, 0.35 * spacing[k])
        lo, hi = np.searchsorted(t, (target[k] - w, target[k] + w))
        if hi > lo:
            cost = (((rot[lo:hi] - ref[k]) * (1.0, 1.0, 1.5)) ** 2).sum(axis=1) + in_menu[lo:hi] * 1e6
            chosen[k] = t[lo + int(np.argmin(cost))]
    vt = np.maximum.accumulate(s0 + np.round((chosen - ctime + offset - s0) * SRC_FPS) / SRC_FPS)
    frame_unix = vt - offset + ctime

    # Stabilisation: rotate each frame from the head's actual rotation to a smoothed one. The
    # zoom crop hides the edges; corrections are kept inside what it can hide.
    actual = rot_at(frame_unix)
    fx = (src_w / 2) / math.tan(math.radians(fov / 2))
    f_out = fx * OUT_W / src_w * zoom

    def half(size, f):
        return math.degrees(math.atan(size / 2 / f))

    limit = 0.9 * np.array([half(src_h, fx) - half(OUT_H, f_out), half(src_w, fx) - half(OUT_W, f_out), 4.0])
    smooth = actual.copy()
    smooth[:, 2] = 0.0                                     # level horizon where possible
    for _ in range(8):
        smooth = np.column_stack([gaussian_smooth(smooth[:, j], s) for j, s in enumerate((10, 6, 10))])
        smooth = actual + np.clip(smooth - actual, -limit, limit)
    a_src, a_out_inv = intrinsics(src_w, src_h, fx), np.linalg.inv(intrinsics(OUT_W, OUT_H, f_out))

    # Overlay: static layer once, live parts per frame
    segs = segments(d)
    mx0, my0, mx1, my1 = OUT_W - 470, 40, OUT_W - 40, 470
    pts = map_points(d, (mx0 + 30, my0 + 30, mx1 - 30, my1 - 30), segs)
    px = np.full((len(d), 2), np.nan)
    for (sa, sb), p in zip(segs, pts):
        px[sa:sb] = p
    valid = ~np.isnan(px[:, 0])
    px = px[np.maximum.accumulate(np.where(valid, np.arange(len(d)), 0))]
    sx0, sy0, sx1, sy1 = 40, OUT_H - 180, 790, OUT_H - 40
    ex0, ey0, ex1, ey1 = 830, OUT_H - 150, OUT_W - 40, OUT_H - 40
    hs = (height - height.min()) / max(np.ptp(height), 5.0)
    exs = ex0 + 24 + dist / max(dist[-1], 1.0) * (ex1 - ex0 - 48)
    eys = ey1 - 16 - hs * (ey1 - ey0 - 58)
    speed = (t[-1] - t[0]) / length
    base = Image.new("RGBA", (OUT_W, OUT_H), (0, 0, 0, 0))
    bd = ImageDraw.Draw(base)
    panel = (14, 16, 21, 175)
    bd.rounded_rectangle((mx0, my0, mx1, my1), 24, fill=panel)
    for p in pts:
        bd.line([tuple(q) for q in p[::4]], fill=(255, 255, 255, 60), width=5, joint="curve")
    bd.rounded_rectangle((mx0, my1 + 14, mx1, my1 + 62), 16, fill=panel)
    bd.text((mx0 + 22, my1 + 21), f"{speed:.0f}× timelapse  ·  Reality Runner", font=font("semi", 23),
            fill=(225, 229, 236, 255))
    bd.rounded_rectangle((sx0, sy0, sx1, sy1), 24, fill=panel)
    bd.rounded_rectangle((ex0, ey0, ex1, ey1), 24, fill=panel)
    bd.text((ex0 + 24, ey0 + 12), "Elevation", font=font("semi", 22), fill=(165, 172, 185, 255))
    bd.line(list(zip(exs[::8], eys[::8])), fill=(255, 255, 255, 70), width=3)
    rects = [(mx0, my0, mx1, my1 + 62), (sx0, sy0, sx1, sy1), (ex0, ey0, ex1, ey1)]
    label_font, value_font = font("semi", 22), font("bold", 48)

    def overlay(k):
        layer = base.copy()
        ld = ImageDraw.Draw(layer)
        for (sa, sb), p in zip(segs, pts):
            q = p[:max(0, min(k, sb - 1) - sa + 1)]
            if len(q) > 1:
                ld.line([tuple(v) for v in q[::4]] + [tuple(q[-1])], fill=ACCENT + (255,), width=7, joint="curve")
        cx, cy = px[k]
        if not math.isnan(cx):
            yaw = math.radians(d[k, 5])                    # where the head looks
            tip = (cx + 30 * math.cos(yaw), cy + 30 * math.sin(yaw))
            left = (cx + 14 * math.cos(yaw + 2.4), cy + 14 * math.sin(yaw + 2.4))
            right = (cx + 14 * math.cos(yaw - 2.4), cy + 14 * math.sin(yaw - 2.4))
            ld.polygon([tip, left, right], fill=(255, 255, 255, 120))
            ld.ellipse((cx - 11, cy - 11, cx + 11, cy + 11), fill=(255, 255, 255, 255), outline=ACCENT + (255,), width=5)
        upto = int(np.searchsorted(exs, exs[k], side="right"))
        ld.line(list(zip(exs[:upto:8], eys[:upto:8])) + [(exs[k], eys[k])], fill=ACCENT + (255,), width=4)
        ld.ellipse((exs[k] - 7, eys[k] - 7, exs[k] + 7, eys[k] + 7), fill=(255, 255, 255, 255))
        for col, (label, value) in enumerate((("Time", fmt_time(t[k] - a)),
                                              ("Distance", f"{dist[k] / 1000:.2f} km"),
                                              ("Climb", f"{climb[k]:.0f} m"))):
            x = sx0 + 30 + col * 250
            ld.text((x, sy0 + 18), label, font=label_font, fill=(165, 172, 185, 255))
            ld.text((x, sy0 + 48), value, font=value_font, fill=(245, 247, 250, 255))
        return np.asarray(layer)

    def compose(frame, layer):
        out_frame = frame.copy()
        for x0, y0, x1, y1 in rects:
            src = out_frame[y0:y1, x0:x1].astype(np.uint16)
            over = layer[y0:y1, x0:x1].astype(np.uint16)
            alpha = over[..., 3:4]
            out_frame[y0:y1, x0:x1] = ((src * (255 - alpha) + over[..., :3] * alpha + 127) // 255).astype(np.uint8)
        return out_frame

    def render(frame, k):
        """Stabilised, overlaid output frame k from its source frame."""
        if cover_uid:
            frame = hide_uid(frame)
        rotation = rotation_matrix(*actual[k]) @ rotation_matrix(*smooth[k]).T
        warped = cv2.warpPerspective(frame, a_src @ rotation @ a_out_inv, (OUT_W, OUT_H),
                                     flags=cv2.INTER_LINEAR | cv2.WARP_INVERSE_MAP, borderMode=cv2.BORDER_REFLECT)
        return compose(warped, overlay(min(len(d) - 1, int(np.searchsorted(t, frame_unix[k])))))

    when = datetime.datetime.fromtimestamp(a)
    if out is None:
        out = video.with_name(f"{video.stem} timelapse run{run}.mp4")
    uniq, which = np.unique(vt, return_inverse=True)
    if stills:
        picks = np.linspace(0, n - 1, stills).astype(int)
        frames = decode(video, [uniq[which[k]] for k in picks], src_w, src_h, scale=False)
        for k, frame in zip(picks, frames):
            path = out.with_name(f"{out.stem} still{k:04d}.png")
            Image.fromarray(render(frame, k)).save(path)
            print(path)
        return out

    enc = subprocess.Popen(["ffmpeg", "-y", "-v", "error", "-f", "rawvideo", "-pix_fmt", "rgb24",
                            "-s", f"{OUT_W}x{OUT_H}", "-r", str(OUT_FPS), "-i", "-",
                            "-c:v", "h264_nvenc", "-preset", "p6", "-rc", "vbr", "-cq", "19", "-b:v", "0",
                            "-profile:v", "high", "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(out)],
                           stdin=subprocess.PIPE)

    def card(path: Path, seconds: float):
        still = np.asarray(Image.open(path).convert("RGB").resize((OUT_W, OUT_H), Image.LANCZOS)).astype(np.float32)
        count = int(seconds * OUT_FPS)
        for i in range(count):
            fade = min(1.0, (i + 1) / 12, (count - i) / 12)
            enc.stdin.write((still * fade).astype(np.uint8).tobytes())

    card(summary(cache, run, title, {}), 3.5)
    frames = decode(video, list(uniq), src_w, src_h, scale=False)
    frame, current = None, -1
    for k in range(n):
        while current < which[k]:
            frame, current = next(frames), current + 1
        enc.stdin.write(render(frame, k).tobytes())
        if k % 150 == 0:
            print(f"  {k}/{n} frames ({fmt_time(t[min(len(t) - 1, int(np.searchsorted(t, frame_unix[k])))] - a)} into the run)",
                  flush=True)
    frames.close()
    card(summary(cache, run, title, real), 5.0)
    enc.stdin.close()
    enc.wait()
    print(f"{out}  ({n / OUT_FPS:.0f} s at ~{speed:.0f}x, run {run} {when:%H:%M}, "
          f"max correction pitch/yaw/roll {np.abs(smooth - actual).max(axis=0).round(1).tolist()} deg)")
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
    p.add_argument("--real-time", help="treadmill time, e.g. 32:48")
    p.add_argument("--calories", type=float)
    p = sub.add_parser("sync")
    p.add_argument("video", type=Path)
    p = sub.add_parser("timelapse")
    p.add_argument("video", type=Path)
    p.add_argument("--run", type=int)
    p.add_argument("--length", type=float, default=75, help="seconds of timelapse (before the cards)")
    p.add_argument("--title", default="Treadmill walk across Solaris-3")
    p.add_argument("--real-km", type=float)
    p.add_argument("--real-time", help="treadmill time, e.g. 32:48")
    p.add_argument("--calories", type=float)
    p.add_argument("--fov", type=float, default=104, help="horizontal field of view of the recording (deg)")
    p.add_argument("--zoom", type=float, default=1.4, help="crop that hides the stabilisation edges")
    p.add_argument("--out", type=Path, help="default: next to the video")
    p.add_argument("--stills", type=int, default=0, help="only save this many sample frames (PNG)")
    p.add_argument("--show-uid", action="store_true", help="leave the in-game User ID visible")
    args = parser.parse_args()
    if args.command == "load":
        load(args.pid)
    elif args.command == "sync":
        video_offset(args.video)
    else:
        real = {"distance_km": args.real_km, "time": args.real_time, "calories": args.calories}
        if args.command == "summary":
            summary(latest_cache(), args.run, args.title, real)
        else:
            timelapse(args.video, latest_cache(), args.run, args.length, args.title, real, args.fov, args.zoom,
                      args.out, args.stills, not args.show_uid)


if __name__ == "__main__":
    main()
