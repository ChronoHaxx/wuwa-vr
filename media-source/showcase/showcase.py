#!/usr/bin/env python3
"""Cut a stabilised WuWa VR showcase video out of long gameplay recordings.

    analyze CLIP...        timeline of motion/sound, dead time, contact sheets
    sheet CLIP FROM TO     close-up contact sheet of one time range
    auto ANALYSIS...       pick the liveliest moments, write an edit list
    render EDIT.json       stabilise segments, add cards/captions/music, export

Needs ffmpeg and ffprobe on PATH (with libvidstab and NVENC), numpy and
opencv-python. README.md explains recording and the edit-list format.
"""
from __future__ import annotations

import argparse
import datetime
import glob
import hashlib
import json
import math
import shutil
import subprocess
import sys
import textwrap
import time
from pathlib import Path

import cv2
import numpy as np

RATE = 4                  # analysis samples per second
SMALL = (96, 54)          # analysis frame size
THUMB = (384, 216)        # contact sheet thumbnail size
GRID = (5, 6)             # contact sheet columns, rows
FPS = 60                  # output frame rate
FONTS = {"bold": "segoeuib.ttf", "semi": "seguisb.ttf"}
# vidstabtransform settings per segment "stabilize" mode.
STABILIZE = {
    "off": None,
    "light": "smoothing=10:optzoom=0:zoom=3",
    "normal": "smoothing=20:optzoom=0:zoom=5",
    "strong": "smoothing=40:optzoom=1",
    "lock": "tripod=1:optzoom=1",   # holds the first frame's framing: still scenery
}


def run(cmd, cwd=None) -> bytes:
    done = subprocess.run([str(part) for part in cmd], cwd=cwd, capture_output=True)
    if done.returncode:
        sys.exit(f"{Path(str(cmd[0])).name} failed ({done.returncode}):\n"
                 + done.stderr.decode(errors="replace")[-3000:])
    return done.stdout


def probe(path: Path) -> dict:
    data = json.loads(run(["ffprobe", "-v", "error", "-show_entries",
                           "stream=codec_type,width,height:format=duration", "-of", "json", path]))
    video = next(s for s in data["streams"] if s["codec_type"] == "video")
    return {"width": int(video["width"]), "height": int(video["height"]),
            "duration": float(data["format"]["duration"]),
            "audio": any(s["codec_type"] == "audio" for s in data["streams"])}


def crop_filter(info: dict, crop: str | None) -> str:
    """Filters (with a trailing comma) that turn the recording into the picture to edit.

    sbs-left / sbs-right take one eye of a side-by-side recording and its central 16:9.
    Anything else is passed to ffmpeg as written, e.g. "crop=2560:1080:0:180".
    """
    if not crop:
        return ""
    if crop in ("sbs-left", "sbs-right"):
        width = info["width"] // 2
        height = min(info["height"], int(width * 9 / 16) // 2 * 2)
        x = 0 if crop == "sbs-left" else width
        return f"crop={width}:{height}:{x}:{(info['height'] - height) // 2},"
    return crop.rstrip(",") + ","


def clock(seconds: float) -> str:
    return f"{int(seconds // 60)}:{seconds % 60:04.1f}"


def smooth(values, width):
    width = max(1, int(width))
    return np.convolve(values, np.ones(width) / width, "same")


# ---------------------------------------------------------------- analysis

def score_timeline(motion, luma, audio):
    """Per-sample interest 0..1, and a mask of dead time (menus, pauses, loading, black)."""
    ref = float(np.percentile(motion, 90)) or 1.0
    moving = np.clip(motion / ref, 0, 1)
    calm = smooth(motion, RATE * 3) < 0.06 * ref
    dark = luma < 14
    if audio is not None:
        low, high = np.percentile(audio, [10, 95])
        loud = np.clip((audio - low) / max(high - low, 1e-6), 0, 1)
        score = 0.6 * moving + 0.4 * loud
    else:
        score = moving
    score = smooth(score, RATE)
    dead = calm | dark
    score[dead] = 0
    for i in np.flatnonzero(motion > 4 * ref):   # hard cuts, teleports, loading flashes
        score[max(0, i - RATE // 2): i + RATE // 2 + 1] = 0
    return score, dead


def dead_ranges(dead, minimum=2.0):
    ranges, start = [], None
    for i, flag in enumerate(list(dead) + [False]):
        if flag and start is None:
            start = i
        elif not flag and start is not None:
            if (i - start) / RATE >= minimum:
                ranges.append([round(start / RATE, 2), round(i / RATE, 2)])
            start = None
    return ranges


def contact_sheets(images, times, scores, dead, prefix: Path, title: str):
    cols, rows = GRID
    width, height = THUMB
    label, top, per = 30, 44, cols * rows
    pages = []
    for page in range(math.ceil(len(images) / per)):
        sheet = np.full((top + rows * (height + label), cols * width, 3), 22, np.uint8)
        cv2.putText(sheet, f"{title}   sheet {page + 1}/{math.ceil(len(images) / per)}   "
                    "(bar = interest, dim = dead time)", (10, 30), cv2.FONT_HERSHEY_SIMPLEX,
                    0.75, (255, 255, 255), 2, cv2.LINE_AA)
        for slot, i in enumerate(range(page * per, min(len(images), (page + 1) * per))):
            row, col = divmod(slot, cols)
            x, y = col * width, top + row * (height + label)
            image = cv2.imread(str(images[i]))
            if image is None:
                continue
            image = cv2.resize(image, THUMB, interpolation=cv2.INTER_AREA)
            if dead[i]:
                image = (image * 0.35).astype(np.uint8)
            sheet[y:y + height, x + 2:x + width - 2] = image[:, 2:width - 2]
            bar = int((width - 110) * float(np.clip(scores[i], 0, 1)))
            cv2.rectangle(sheet, (x + 4, y + height + 8), (x + 4 + bar, y + height + 18), (90, 200, 120), -1)
            cv2.putText(sheet, clock(times[i]), (x + width - 102, y + height + 22),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.62, (235, 235, 235), 1, cv2.LINE_AA)
        path = prefix.with_name(f"{prefix.name}-{page + 1:02d}.jpg")
        cv2.imwrite(str(path), sheet, [cv2.IMWRITE_JPEG_QUALITY, 82])
        pages.append(path)
    return pages


def fresh_dir(path: Path) -> Path:
    shutil.rmtree(path, ignore_errors=True)
    path.mkdir(parents=True)
    return path


def analyze(clip: Path, crop: str | None, work: Path) -> Path:
    info = probe(clip)
    every = max(2, math.ceil(info["duration"] / 150))
    thumbs = fresh_dir(work / "thumbs" / clip.stem)
    (work / "sheets").mkdir(exist_ok=True)
    started = time.time()
    graph = (f"[0:v]{crop_filter(info, crop)}split=2[a][b];"
             f"[a]fps={RATE},scale={SMALL[0]}:{SMALL[1]}:flags=area,format=gray[small];"
             f"[b]fps=1/{every},scale={THUMB[0]}:{THUMB[1]}:flags=area[thumb]")
    raw = run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-hwaccel", "auto", "-i", clip,
               "-filter_complex", graph, "-map", "[small]", "-f", "rawvideo", "pipe:1",
               "-map", "[thumb]", "-q:v", "3", thumbs / "%05d.jpg"])
    frames = np.frombuffer(raw, np.uint8).reshape(-1, SMALL[1], SMALL[0]).astype(np.int16)
    motion = np.zeros(len(frames))
    motion[1:] = np.abs(np.diff(frames, axis=0)).mean(axis=(1, 2))
    luma = frames.mean(axis=(1, 2))
    audio = None
    if info["audio"]:
        pcm = run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-i", clip, "-map", "0:a:0",
                   "-ac", "1", "-ar", "8000", "-f", "s16le", "pipe:1"])
        samples = np.frombuffer(pcm, np.int16).astype(np.float32) / 32768
        hop = 8000 // RATE
        count = min(len(samples) // hop, len(motion))
        rms = np.sqrt((samples[:count * hop].reshape(count, hop) ** 2).mean(axis=1) + 1e-10)
        audio = np.full(len(motion), -90.0)
        audio[:count] = 20 * np.log10(rms)
    score, dead = score_timeline(motion, luma, audio)

    images = sorted(thumbs.glob("*.jpg"))
    times = [i * every for i in range(len(images))]
    at = [min(len(score) - 1, int(t * RATE)) for t in times]
    sheets = contact_sheets(images, times, [float(score[max(0, i - RATE):i + RATE + 1].mean()) for i in at],
                            [bool(dead[i]) for i in at], work / "sheets" / clip.stem, clip.name)
    out = work / f"analysis-{clip.stem}.json"
    out.write_text(json.dumps({
        "clip": str(clip.resolve()), "crop": crop, "duration": info["duration"], "rate": RATE,
        "thumb_every": every, "sheets": [str(p) for p in sheets],
        "dead": dead_ranges(dead),
        "score": [round(float(v), 3) for v in score],
        "motion": [round(float(v), 2) for v in motion],
        "audio_db": None if audio is None else [round(float(v), 1) for v in audio],
    }), encoding="utf-8")
    print(f"{clip.name}: {clock(info['duration'])} analysed in {time.time() - started:.0f} s, "
          f"{len(sheets)} sheet(s), {sum(e - s for s, e in dead_ranges(dead)):.0f} s dead -> {out}")
    return out


def range_sheet(clip: Path, start: float, end: float, every: float, crop: str | None, work: Path):
    info = probe(clip)
    thumbs = fresh_dir(work / "thumbs" / f"{clip.stem}-{int(start)}-{int(end)}")
    (work / "sheets").mkdir(exist_ok=True)
    run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-ss", f"{start:.3f}", "-t", f"{end - start:.3f}",
         "-i", clip, "-vf", f"{crop_filter(info, crop)}fps={1 / every:.4f},"
         f"scale={THUMB[0]}:{THUMB[1]}:flags=area", "-q:v", "3", thumbs / "%05d.jpg"])
    images = sorted(thumbs.glob("*.jpg"))
    times = [start + i * every for i in range(len(images))]
    scores, dead = [0.0] * len(images), [False] * len(images)
    analysis = work / f"analysis-{clip.stem}.json"
    if analysis.is_file():
        data = json.loads(analysis.read_text(encoding="utf-8"))
        values = data["score"]
        scores = [values[min(len(values) - 1, int(t * RATE))] for t in times]
        dead = [any(s <= t < e for s, e in data["dead"]) for t in times]
    for path in contact_sheets(images, times, scores, dead,
                               work / "sheets" / f"{clip.stem}-{int(start)}-{int(end)}", clip.name):
        print(path)


# ---------------------------------------------------------------- auto edit

def auto(analyses: list[Path], length: float, piece: float) -> dict:
    """Greedy: best-scoring windows first, at least 10 s apart, then in recording order."""
    width = int(piece * RATE)
    windows, data = [], []
    for analysis in analyses:
        item = json.loads(analysis.read_text(encoding="utf-8"))
        score = np.array(item["score"])
        dead = np.zeros(len(score), bool)
        for start, end in item["dead"]:
            dead[int(start * RATE):int(end * RATE)] = True
        if len(score) < width:
            windows.append(np.zeros(0))
        else:
            mean = np.convolve(score, np.ones(width) / width, "valid")
            mean[np.convolve(dead, np.ones(width), "valid") > 0] = 0
            windows.append(mean)
        data.append(item)
    picks, total = [], 0.0
    while total < length:
        best = max(((w.max(), n, int(w.argmax())) for n, w in enumerate(windows) if len(w)), default=None)
        if best is None or best[0] < 0.05:
            break
        _, n, i = best
        picks.append((n, i / RATE))
        total += piece
        spread = int((piece + 10) * RATE)
        windows[n][max(0, i - spread):i + spread] = 0
    picks.sort()
    return {
        "title": "WuWa VR", "subtitle": "Wuthering Waves in VR",
        "outro": "Free on PC VR", "outro_sub": "chronohaxx.github.io/wuwa-vr", "music": None,
        "segments": [{"clip": data[n]["clip"], "crop": data[n]["crop"], "in": round(t, 2),
                      "out": round(t + piece, 2), "caption": "", "stabilize": "off"} for n, t in picks],
    }


# ---------------------------------------------------------------- render

def stabilised(segment: dict, cache: Path) -> Path:
    """One trimmed, stabilised, audio-carrying intermediate per segment; cached by its settings."""
    clip = Path(segment["clip"])
    info = probe(clip)
    start = float(segment["in"])
    duration = float(segment["out"]) - start
    mode = segment.get("stabilize", "off")
    if mode not in STABILIZE:
        sys.exit(f"Unknown stabilize mode {mode!r}; use one of {', '.join(STABILIZE)}")
    pre = crop_filter(info, segment.get("crop"))
    key = hashlib.sha1(json.dumps([str(clip.resolve()), start, duration, pre, STABILIZE[mode]])
                       .encode()).hexdigest()[:12]
    out = cache / f"seg-{key}.mp4"
    if out.is_file():
        return out
    seek = ["-ss", f"{start:.3f}", "-t", f"{duration:.3f}", "-i", clip]
    video = pre
    if STABILIZE[mode]:
        detect = f"vidstabdetect=shakiness=6:accuracy=15:result=seg-{key}.trf"
        if mode == "lock":
            detect += ":tripod=1"
        run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", *seek, "-vf", pre + detect,
             "-f", "null", "-"], cwd=cache)
        video += f"vidstabtransform=input=seg-{key}.trf:{STABILIZE[mode]}:interpol=bicubic,unsharp=5:5:0.4,"
    command = ["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", *seek]
    if not info["audio"]:
        command += ["-f", "lavfi", "-t", f"{duration:.3f}", "-i", "anullsrc=r=48000:cl=stereo"]
    command += ["-vf", video + "format=yuv420p", "-map", "0:v:0", "-map", "0:a:0" if info["audio"] else "1:a:0",
                "-c:v", "hevc_nvenc", "-preset", "p6", "-rc", "vbr", "-cq", "16", "-b:v", "0",
                "-c:a", "aac", "-b:a", "320k", "-ar", "48000", "-shortest", f"seg-{key}.part.mp4"]
    run(command, cwd=cache)
    (cache / f"seg-{key}.part.mp4").replace(out)
    return out


def text_file(cache: Path, text: str, width: int) -> str:
    lines = textwrap.fill(text, width=width, break_long_words=False, break_on_hyphens=False) if text else ""
    name = "text-" + hashlib.sha1(lines.encode()).hexdigest()[:10] + ".txt"
    (cache / name).write_text(lines, encoding="utf-8")
    return name


def fitted(text: str, size: int, width: int, wrap: int) -> int:
    """Shrink a font size until the longest wrapped line fits in 90% of the frame width."""
    longest = max((len(line) for line in textwrap.wrap(text, wrap, break_long_words=False,
                                                         break_on_hyphens=False)), default=1)
    return min(size, int(width * 0.9 / (0.56 * longest)))


def drawtext(name, font, size, x, y, start, end, fade=0.4, box=False):
    alpha = (f"if(lt(t,{start + fade:.3f}),(t-{start:.3f})/{fade},"
             f"if(gt(t,{end - fade:.3f}),({end:.3f}-t)/{fade},1))")
    backing = f"box=1:boxcolor=black@0.38:boxborderw={size // 3}:" if box else ""
    return (f"drawtext=fontfile={font}:textfile={name}:expansion=none:fontsize={size}:fontcolor=white:"
            f"x={x}:y={y}:{backing}borderw={max(2, size // 20)}:bordercolor=black@0.55:shadowx=0:"
            f"shadowy={max(2, size // 16)}:shadowcolor=black@0.45:line_spacing={size // 4}:"
            f"alpha='{alpha}':enable='between(t,{start:.3f},{end:.3f})'")


def still(source: Path, at: float, name: str, cache: Path) -> str:
    run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-ss", f"{max(0.0, at):.3f}", "-i", source,
         "-frames:v", "1", name], cwd=cache)
    return name


def render(edit_path: Path, out: Path | None, vertical: bool, size: tuple[int, int]) -> Path:
    started = time.time()
    edit = json.loads(edit_path.read_text(encoding="utf-8"))
    cache = edit_path.parent / "render-cache"
    cache.mkdir(exist_ok=True)
    for font in FONTS.values():
        if not (cache / font).is_file():
            shutil.copy(Path("C:/Windows/Fonts") / font, cache / font)
    width, height = (1080, 1920) if vertical else size
    blend = float(edit.get("transition", 0.5))
    wrap = 24 if vertical else 52

    items = list(edit["segments"])
    if edit.get("title"):
        items.insert(0, {"card": edit["title"], "sub": edit.get("subtitle", ""), "duration": 3.0})
    if edit.get("outro"):
        items.append({"card": edit["outro"], "sub": edit.get("outro_sub", ""), "duration": 3.5})
    clips = {i: stabilised(item, cache) for i, item in enumerate(items) if "card" not in item}
    print(f"segments ready ({len(clips)}) after {time.time() - started:.0f} s")

    fit = (f"scale={width}:{height}:force_original_aspect_ratio=increase:flags=lanczos,"
           f"crop={width}:{height},setsar=1")
    inputs, graph, parts = [], [], []
    for i, item in enumerate(items):
        index = sum(1 for part in inputs if part == "-i")
        if "card" in item:
            duration = float(item.get("duration", 3.0))
            near = [k for k in clips if k > i] if i == 0 else [k for k in clips if k < i]
            source = clips[(min if i == 0 else max)(near)] if near else None
            if source is None:
                inputs += ["-f", "lavfi", "-t", f"{duration}", "-i", f"color=c=0x101418:s={width}x{height}:r={FPS}"]
                base = f"[{index}:v]"
            else:
                at = probe(source)["duration"] / 2 if i == 0 else probe(source)["duration"] - 0.2
                inputs += ["-loop", "1", "-framerate", str(FPS), "-t", f"{duration}",
                           "-i", still(source, at, f"card-{i}.png", cache)]
                base = f"[{index}:v]{fit},boxblur=24:2,eq=brightness=-0.16:saturation=0.85,"
            inputs += ["-f", "lavfi", "-t", f"{duration}", "-i", "anullsrc=r=48000:cl=stereo"]
            big, small = (int(width * 0.12), int(width * 0.05)) if vertical else (int(height * 0.085), int(height * 0.034))
            title_wrap = 16 if vertical else 32
            text = [drawtext(text_file(cache, item["card"], title_wrap), FONTS["bold"],
                             fitted(item["card"], big, width, title_wrap),
                             "(w-text_w)/2", f"(h-text_h)/2-{int(height * 0.04)}", 0.2, duration)]
            if item.get("sub"):
                text.append(drawtext(text_file(cache, item["sub"], wrap), FONTS["semi"],
                                     fitted(item["sub"], small, width, wrap),
                                     "(w-text_w)/2", f"h/2+{int(height * (0.03 if vertical else 0.06))}", 0.6, duration))
            graph.append(f"{base}fps={FPS},format=yuv420p,{','.join(text)}[v{i}]")
            graph.append(f"[{index + 1}:a]aformat=sample_fmts=fltp:sample_rates=48000:channel_layouts=stereo[a{i}]")
        else:
            speed = float(item.get("speed", 1.0))
            if not 0.5 <= speed <= 2.0:
                sys.exit("speed must be between 0.5 and 2")
            duration = (float(item["out"]) - float(item["in"])) / speed
            inputs += ["-i", clips[i]]
            chain = [f"[{index}:v]setpts=(PTS-STARTPTS)/{speed}", f"fps={FPS}", fit, "format=yuv420p"]
            if item.get("caption"):
                position = ("(w-text_w)/2", "h*0.70") if vertical else \
                           (f"{int(width * 0.055)}", f"h-text_h-{int(height * 0.09)}")
                chain.append(drawtext(text_file(cache, item["caption"], wrap), FONTS["semi"],
                                      int(width * 0.055) if vertical else int(height * 0.042),
                                      *position, 0.35, duration - 0.35, box=True))
            graph.append(",".join(chain) + f"[v{i}]")
            tempo = f"atempo={speed}," if speed != 1.0 else ""
            graph.append(f"[{index}:a]asetpts=PTS-STARTPTS,{tempo}"
                         f"aformat=sample_fmts=fltp:sample_rates=48000:channel_layouts=stereo[a{i}]")
        parts.append((f"v{i}", f"a{i}", duration))

    video, audio, total = parts[0]
    for k, (v, a, duration) in enumerate(parts[1:], 1):
        graph.append(f"[{video}][{v}]xfade=transition=fade:duration={blend}:offset={total - blend:.3f}[xv{k}]")
        graph.append(f"[{audio}][{a}]acrossfade=d={blend}[xa{k}]")
        video, audio, total = f"xv{k}", f"xa{k}", total + duration - blend
    graph.append(f"[{video}]fade=t=in:st=0:d=0.6,fade=t=out:st={total - 0.8:.3f}:d=0.8[vout]")
    game = float(edit.get("game_volume", 0.5 if edit.get("music") else 1.0))
    if edit.get("music"):
        index = sum(1 for part in inputs if part == "-i")
        inputs += ["-stream_loop", "-1", "-i", Path(edit["music"]).resolve()]
        graph.append(f"[{index}:a]atrim=0:{total:.3f},asetpts=PTS-STARTPTS,"
                     f"aformat=sample_fmts=fltp:sample_rates=48000:channel_layouts=stereo,"
                     f"volume={float(edit.get('music_volume', 0.8))},afade=t=in:d=1.5,"
                     f"afade=t=out:st={total - 3:.3f}:d=3[music]")
        graph.append(f"[{audio}]volume={game}[game]")
        graph.append("[game][music]amix=inputs=2:duration=first:normalize=0[mixed]")
        audio = "mixed"
    elif game != 1.0:
        graph.append(f"[{audio}]volume={game}[game]")
        audio = "game"
    graph.append(f"[{audio}]afade=t=out:st={total - 1.2:.3f}:d=1.2,loudnorm=I=-14:TP=-1.5:LRA=11,"
                 f"aresample=48000[aout]")

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M")
    out = (out or edit_path.with_name(f"{edit_path.stem}-{stamp}{'-vertical' if vertical else ''}.mp4")).resolve()
    script = cache / f"graph-{'vertical' if vertical else 'wide'}.txt"
    script.write_text(";\n".join(graph), encoding="utf-8")
    run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", *inputs,
         "-filter_complex_script", script.name, "-map", "[vout]", "-map", "[aout]",
         "-c:v", "h264_nvenc", "-preset", "p7", "-tune", "hq", "-rc", "vbr", "-cq", "18", "-b:v", "0",
         "-maxrate", "90M", "-bufsize", "180M", "-profile:v", "high", "-pix_fmt", "yuv420p", "-g", "120",
         "-spatial-aq", "1", "-c:a", "aac", "-b:a", "320k", "-movflags", "+faststart", out], cwd=cache)
    preview = out.with_suffix(".preview.jpg")
    run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", out, "-vf",
         f"fps=12/{total:.3f},scale={480 if not vertical else 270}:-2,tile=4x3", "-frames:v", "1", preview])
    result = probe(out)
    print(f"{out}\n  {clock(result['duration'])} (planned {clock(total)}), "
          f"{out.stat().st_size / 1e6:.0f} MB, {time.time() - started:.0f} s; preview {preview}")
    return out


def expand(paths: list[Path]) -> list[Path]:
    """Expand wildcards here: PowerShell passes them to programs unexpanded."""
    out = []
    for path in paths:
        text = str(path)
        if any(c in text for c in "*?["):
            matches = [Path(match) for match in sorted(glob.glob(text))]
            if not matches:
                sys.exit(f"No files match {text}")
            out += matches
        else:
            out.append(path)
    return [path.resolve() for path in out]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    a = sub.add_parser("analyze", help="timeline and contact sheets for each recording")
    a.add_argument("clips", nargs="+", type=Path)
    a.add_argument("--crop", help="sbs-left, sbs-right or an ffmpeg crop=... filter")
    a.add_argument("--work", type=Path, help="output folder (default: showcase-work next to the clip)")
    s = sub.add_parser("sheet", help="close-up contact sheet of FROM..TO seconds")
    s.add_argument("clip", type=Path)
    s.add_argument("start", type=float)
    s.add_argument("end", type=float)
    s.add_argument("--every", type=float, default=1.0)
    s.add_argument("--crop")
    s.add_argument("--work", type=Path)
    p = sub.add_parser("auto", help="write an edit list from analysis files")
    p.add_argument("analyses", nargs="+", type=Path)
    p.add_argument("--length", type=float, default=75, help="target seconds of gameplay")
    p.add_argument("--piece", type=float, default=5, help="seconds per segment")
    p.add_argument("--out", type=Path)
    r = sub.add_parser("render", help="render an edit list")
    r.add_argument("edit", type=Path)
    r.add_argument("--out", type=Path)
    r.add_argument("--vertical", action="store_true", help="1080x1920 for Shorts/TikTok")
    r.add_argument("--size", default="2560x1440", help="landscape output size")
    args = parser.parse_args()

    if args.command == "analyze":
        for clip in expand(args.clips):
            analyze(clip, args.crop, (args.work or clip.parent / "showcase-work"))
    elif args.command == "sheet":
        clip = args.clip.resolve()
        range_sheet(clip, args.start, args.end, args.every, args.crop, args.work or clip.parent / "showcase-work")
    elif args.command == "auto":
        analyses = expand(args.analyses)
        edit = auto(analyses, args.length, args.piece)
        out = args.out or analyses[0].parent / "edit.json"
        out.write_text(json.dumps(edit, indent=2, ensure_ascii=False), encoding="utf-8")
        print(f"{len(edit['segments'])} segments -> {out}")
    else:
        width, height = (int(v) for v in args.size.lower().split("x"))
        render(args.edit.resolve(), args.out, args.vertical, (width, height))


if __name__ == "__main__":
    main()
