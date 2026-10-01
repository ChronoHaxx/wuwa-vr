"""Toggle lighting CVars one at a time and score the left/right brightness gap of far objects.

For each CVar: switch it to a test value through the WuWa test control (wuwa-test.py graphics,
which captures before/after and restores), then measure the mean luminance of a band of the
image in each eye. The band defaults to the upper middle, where far objects sit. A CVar that
removes the far-lighting mismatch closes the left/right gap. Read-only apart from the timed,
restored CVar leases. Ends with a contact sheet of the band, before and after, per CVar.

    python wuwa_light_batch.py --out <dir> [--band 0.15,0.35]
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
CANDIDATES = [
    ("r.CLV.Enable", (0, 1)),
    ("r.CLV.TriggerRefresh", (1, 0)),
    ("r.CLV.RefreshEveryFrame", (1, 0)),
    ("r.CLV.UpdateEveryFrame", (1, 0)),
    ("r.CLV.UpdateAllBlock", (1, 0)),
    ("r.CLV.MaxUpdateCountPerFrame", (4096, 64)),
    ("r.CLV.Freeze", (1, 0)),
    ("r.Kuro.SkyVisibilityIntensity", (0, 1)),
    ("r.Kuro.IndirectLightIntensity", (0, 1)),
    ("r.Kuro.IndirectLightingIntensity", (0, 1)),
    ("r.Kuro.GlobalGIRenderQuality", (0, 1)),
    ("r.Kuro.DisableGlobalGITransition", (1, 0)),
    ("r.Kuro.GlobalPointCloudStreamEnabled", (0, 1)),
    ("r.SkylightIntensityMultiplier", (0, 1)),
    ("r.Kuro.KuroEnableLocalExposure", (0, 1)),
    ("r.DistanceFieldAO", (0, 1)),
    ("r.AOGlobalDistanceField", (0, 1)),
    ("r.DistanceFieldShadowing", (0, 1)),
    ("r.Shadow.CacheDirectLightShadow", (0, 1)),
    ("r.Shadow.CacheWholeSceneShadows", (0, 1)),
    ("r.Shadow.DirectLightCacheIncludeDFShadow", (0, 1)),
    ("r.Shadow.MaxNumDirectLightCSMCacheUpdatesPerLightPerFrame", (64, 0)),
    ("r.VolumetricFog", (0, 1)),
    ("r.Kuro.EnableCacheWeatherData", (0, 1)),
    ("r.Kuro.SuperFarFogTickIntervalMax", (0, 1)),
    ("r.Kuro.LightFunction", (0, 1)),
    ("r.KuroVolumetricLight.Enable", (0, 1)),
]


def gap(image: Path, band: tuple[float, float]) -> tuple[float, float]:
    """Mean luminance of the same far content in each eye: align the halves on edges, then compare."""
    a = np.asarray(Image.open(image).convert("L"), dtype=np.float32)
    h, w = a.shape
    half = w // 2
    y0, y1 = int(h * band[0]), int(h * band[1])
    left, right = a[y0:y1, :half], a[y0:y1, half:2 * half]
    edges = lambda m: np.abs(np.diff(m, axis=1))
    el, er = edges(left), edges(right)
    best, shift = -1e9, 0
    for s in range(-half // 2, half // 2 + 1, 2):
        if s >= 0:
            x, y = el[:, s:], er[:, :er.shape[1] - s]
        else:
            x, y = el[:, :s], er[:, -s:]
        if x.shape[1] < half // 3:
            continue
        score = float((x - x.mean()).ravel() @ (y - y.mean()).ravel()) / (x.std() * y.std() * x.size + 1e-6)
        if score > best:
            best, shift = score, s
    if shift >= 0:
        lo, ro = left[:, shift:], right[:, :right.shape[1] - shift]
    else:
        lo, ro = left[:, :shift], right[:, -shift:]
    return float(lo.mean()), float(ro.mean())


def run(name: str, value: int, out: Path) -> dict:
    target = out / f"{name}={value}"
    proc = subprocess.run([sys.executable, str(HERE / "wuwa-test.py"), "console", name, "--value", str(value),
                           "--output", str(target), "--layer", "projection"], capture_output=True, text=True)
    report = json.loads((target / "comparison.json").read_text(encoding="utf-8")) if (target / "comparison.json").exists() else {}
    return {"dir": target, "exit": proc.returncode, "status": report.get("status", "missing"),
            "error": report.get("error") or (proc.stdout + proc.stderr).strip().splitlines()[-1:] }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--band", default="0.15,0.35")
    ap.add_argument("--only", nargs="*", default=None)
    args = ap.parse_args()
    band = tuple(float(v) for v in args.band.split(","))
    args.out.mkdir(parents=True, exist_ok=True)
    rows, sheet = [], []
    for name, values in CANDIDATES:
        if args.only and name not in args.only:
            continue
        result = run(name, values[0], args.out)
        before = (json.loads((result["dir"] / "comparison.json").read_text(encoding="utf-8")).get("baseline") or {})             if (result["dir"] / "comparison.json").exists() else {}
        if before.get("float") == values[0]:
            result = run(name, values[1], args.out)
        row = {"name": name, "status": result["status"], "dir": str(result["dir"])}
        for phase in ("baseline", "changed", "restored"):
            image = result["dir"] / phase / "image.png"
            if image.exists():
                left, right = gap(image, band)
                row[phase] = {"left": round(left, 1), "right": round(right, 1), "gap": round(right - left, 1)}
                if phase != "restored":
                    sheet.append((f"{result['dir'].name} {phase}", image))
        if result["status"] not in ("captured_and_restored_visual_review_pending", "already_at_test_value"):
            row["error"] = result["error"]
        rows.append(row)
        b, c = row.get("baseline", {}), row.get("changed", {})
        print(f"{result['dir'].name:62} {row['status'][:24]:24} gap {b.get('gap', '-'):>6} -> {c.get('gap', '-'):>6}", flush=True)
    (args.out / "summary.json").write_text(json.dumps(rows, indent=2), encoding="utf-8")
    if sheet:
        tiles = []
        for label, image in sheet:
            im = Image.open(image).convert("RGB")
            im = im.crop((0, int(im.height * band[0]), im.width, int(im.height * band[1]))).resize((900, int(900 * (band[1] - band[0]) * im.height / im.width)))
            ImageDraw.Draw(im).text((6, 4), label, fill=(255, 255, 0))
            tiles.append(im)
        out = Image.new("RGB", (900, sum(t.height for t in tiles)))
        y = 0
        for t in tiles:
            out.paste(t, (0, y)); y += t.height
        out.save(args.out / "sheet.png")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
