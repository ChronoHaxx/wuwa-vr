"""Toggle ambient-occlusion CVars one at a time and score how differently the two eyes are shaded.

For each CVar: switch it to a test value through the WuWa test control (wuwa-test.py console,
which captures before/after and restores), then score stereo consistency: every block of one
eye is matched to the other eye (searching the horizontal offset between the eyes), and the
leftover difference is averaged. Shading computed differently per eye (for example AO from a
cache or history that belongs to the other eye) leaves residual that matching cannot remove.
A CVar that removes the mismatch lowers the score. Read-only apart from the timed, restored
CVar leases. Writes summary.json, a residual heat map per capture and a contact sheet.

    python wuwa_ao_batch.py --out <dir> [--only r.AmbientOcclusionLevels ...]
    python wuwa_ao_batch.py --score <image.png> [<image.png> ...]
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import warnings
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
warnings.filterwarnings("ignore", category=RuntimeWarning)  # blocks that leave the frame at one shift
# (name, test value, alternate when the game already uses the test value)
CANDIDATES = [
    ("r.AmbientOcclusionLevels", 0, 3),  # reference: screen-space AO off
    ("r.UsingDynamicCacheForKuroGTAOSpatialX", 0, 1),
    ("r.AOKuroGhostFix", 0, 1),
    ("r.AOKuroDownsampleMethod", 0, 1),
    ("r.AOKuroDownsampleMethodSplit", 0, 1),
    ("r.GTAO.TemporalFilter", 0, 1),
    ("r.GTAO.SpatialFilter", 0, 1),
    ("r.GTAO.PauseJitter", 1, 0),
    ("r.GTAO.Downsample", 0, 1),
    ("r.GTAO.Upsample", 0, 1),
    ("r.GTAO.Combined", 0, 1),
    ("r.GTAO.UseNormals", 0, 1),
    ("r.AmbientOcclusion.Method", 0, 1),
    ("r.AmbientOcclusion.Compute", 0, 1),
    ("r.AmbientOcclusion.HalfRes", 0, 1),
    ("r.AmbientOcclusion.Denoiser", 0, 1),
    ("r.AmbientOcclusion.Denoiser.TemporalAccumulation", 0, 1),
    ("r.XeGTAO.UseHzb", 0, 1),
    ("r.XeGTAO.Denoise", 0, 1),
    ("r.XeGTAO.UseSceneNormal", 0, 1),
    ("r.DistanceFieldAO", 0, 1),  # control: distance-field AO is separate from AmbientOcclusionLevels
]
BLOCK = 16          # pixels at half resolution
SEARCH_X = 24       # local horizontal search around the global eye offset, half-resolution pixels
SEARCH_Y = 3
TEXTURED = 2.0      # minimum block standard deviation (0-255 luminance) to count


def luminance(image: Path) -> tuple[np.ndarray, np.ndarray]:
    a = np.asarray(Image.open(image).convert("L"), dtype=np.float32)
    h, w = a.shape
    a = a[: h - h % 2, : w - w % 4]
    a = a.reshape(a.shape[0] // 2, 2, a.shape[1] // 2, 2).mean(axis=(1, 3))  # half resolution
    half = a.shape[1] // 2
    return a[:, :half], a[:, half:2 * half]


def global_offset(left: np.ndarray, right: np.ndarray) -> int:
    """Horizontal shift that best aligns the eyes' edges (positive: content further left in the right eye)."""
    el, er = np.abs(np.diff(left, axis=1)), np.abs(np.diff(right, axis=1))
    half = left.shape[1]
    best, shift = -1e9, 0
    for s in range(-half // 3, half // 3 + 1):
        x, y = (el[:, s:], er[:, : er.shape[1] - s]) if s >= 0 else (el[:, :s], er[:, -s:])
        score = float((x - x.mean()).ravel() @ (y - y.mean()).ravel()) / (x.std() * y.std() * x.size + 1e-6)
        if score > best:
            best, shift = score, s
    return shift


def residual_map(left: np.ndarray, right: np.ndarray) -> tuple[np.ndarray, np.ndarray, int]:
    """Per-block mean absolute difference after the best local match, and a mask of textured blocks."""
    shift = global_offset(left, right)
    h, w = left.shape
    rows, cols = h // BLOCK, w // BLOCK
    best = np.full((rows, cols), np.inf, dtype=np.float32)
    lb = left[: rows * BLOCK, : cols * BLOCK]
    for dy in range(-SEARCH_Y, SEARCH_Y + 1):
        for dx in range(shift - SEARCH_X, shift + SEARCH_X + 1):
            # Right-eye pixel matching left (x, y) sits at (x - dx, y + dy).
            shifted = np.full_like(left, np.nan)
            ys, yd = (slice(dy, h), slice(0, h - dy)) if dy >= 0 else (slice(0, h + dy), slice(-dy, h))
            xs, xd = (slice(0, w - dx), slice(dx, w)) if dx >= 0 else (slice(-dx, w), slice(0, w + dx))
            shifted[yd, xd] = right[ys, xs]
            diff = np.abs(lb - shifted[: rows * BLOCK, : cols * BLOCK])
            sad = diff.reshape(rows, BLOCK, cols, BLOCK)
            valid = ~np.isnan(sad).any(axis=(1, 3))
            mean = np.where(valid, np.nanmean(sad, axis=(1, 3)), np.inf)
            np.minimum(best, mean, out=best)
    std = lb.reshape(rows, BLOCK, cols, BLOCK).std(axis=(1, 3))
    return best, (std >= TEXTURED) & np.isfinite(best), shift


def score(image: Path) -> dict:
    left, right = luminance(image)
    best, mask, shift = residual_map(left, right)
    values = best[mask]
    return {"score": round(float(values.mean()), 3) if values.size else None,
            "p90": round(float(np.percentile(values, 90)), 3) if values.size else None,
            "blocks": int(values.size), "eye_offset": shift, "map": best, "mask": mask}


def heat(result: dict, image: Path, target: Path, scale: float = 12.0) -> None:
    left, _ = luminance(image)
    rgb = np.repeat(left[..., None], 3, axis=2) * 0.6
    best, mask = result["map"], result["mask"]
    for r in range(best.shape[0]):
        for c in range(best.shape[1]):
            if mask[r, c]:
                t = min(1.0, float(best[r, c]) / scale)
                rgb[r * BLOCK:(r + 1) * BLOCK, c * BLOCK:(c + 1) * BLOCK] += np.array([255 * t, 60 * (1 - t), 0]) * 0.6
    Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8)).save(target)


def run(name: str, value: float, out: Path) -> Path:
    target = out / f"{name}={value:g}"
    subprocess.run([sys.executable, str(HERE / "wuwa-test.py"), "console", name, "--value", str(value),
                    "--output", str(target), "--layer", "projection"], capture_output=True, text=True)
    return target


def report(target: Path) -> dict:
    path = target / "comparison.json"
    return json.loads(path.read_text(encoding="utf-8")) if path.exists() else {}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path)
    ap.add_argument("--only", nargs="*", default=None)
    ap.add_argument("--score", nargs="*", type=Path, help="only score existing side-by-side captures")
    args = ap.parse_args()
    if args.score:
        for image in args.score:
            r = score(image)
            print(f"{image}: score {r['score']} p90 {r['p90']} blocks {r['blocks']} eye offset {r['eye_offset']}")
        return 0
    if not args.out:
        ap.error("--out is required")
    args.out.mkdir(parents=True, exist_ok=True)
    rows, sheet = [], []
    for name, value, alternate in CANDIDATES:
        if args.only and name not in args.only:
            continue
        target = run(name, value, args.out)
        if (report(target).get("baseline") or {}).get("float") == value:
            target = run(name, alternate, args.out)
        data = report(target)
        row = {"name": name, "dir": str(target), "status": data.get("status", "missing"),
               "baseline_value": (data.get("baseline") or {}).get("float"), "test_value": data.get("test_value")}
        for phase in ("baseline", "changed", "restored"):
            image = target / phase / "image.png"
            if image.exists():
                r = score(image)
                row[phase] = {k: r[k] for k in ("score", "p90", "blocks", "eye_offset")}
                heat(r, image, target / f"{phase}-residual.png")
                if phase != "restored":
                    sheet.append((f"{target.name} {phase} {r['score']}", target / f"{phase}-residual.png"))
        if data.get("error"):
            row["error"] = data["error"]
        rows.append(row)
        b, c = row.get("baseline", {}).get("score"), row.get("changed", {}).get("score")
        print(f"{target.name:58} {row['status'][:24]:24} score {b} -> {c}", flush=True)
    (args.out / "summary.json").write_text(json.dumps(rows, indent=2), encoding="utf-8")
    if sheet:
        tiles = []
        for label, image in sheet:
            im = Image.open(image).convert("RGB")
            im = im.resize((640, int(640 * im.height / im.width)))
            ImageDraw.Draw(im).text((6, 4), label, fill=(255, 255, 0))
            tiles.append(im)
        out = Image.new("RGB", (1280, sum(t.height for t in tiles[0::2])))
        y = 0
        for i in range(0, len(tiles), 2):
            out.paste(tiles[i], (0, y))
            if i + 1 < len(tiles):
                out.paste(tiles[i + 1], (640, y))
            y += tiles[i].height
        out.save(args.out / "sheet.png")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
