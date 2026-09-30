"""Merge partial in-process module captures of one game build into a single image.

Each capture is a wuwa-mapped-module-v1 image (offset == RVA) plus metadata listing the
captured ranges; an interrupted capture has only checkpoint files, and the newest one is
used. Later captures fill ranges the earlier ones missed; bytes already captured are kept.
All inputs must describe the same build (timestamp and image size).

    python wuwa_merge_captures.py --out merged.bin first.bin second.bin
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def metadata(image: Path) -> dict:
    final = Path(str(image) + ".json")
    if final.is_file():
        return json.loads(final.read_text(encoding="utf-8"))
    checkpoints = sorted(image.parent.glob(image.name + ".checkpoint-*.json"))
    if not checkpoints:
        raise SystemExit(f"{image}: no metadata or checkpoint")
    return json.loads(checkpoints[-1].read_text(encoding="utf-8"))


def identity(meta: dict) -> tuple:
    live = meta.get("live") or {}
    return live.get("image_size") or meta["module"]["size"], meta["module"]["base"]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("images", type=Path, nargs="+")
    args = ap.parse_args()
    metas = [metadata(p) for p in args.images]
    ids = {identity(m) for m in metas}
    if len(ids) != 1:
        raise SystemExit(f"captures describe different builds: {ids}")
    size, base = ids.pop()
    if args.out.exists():
        raise SystemExit("output exists")
    merged = bytearray(size)
    have: list[list[int]] = []

    def covered(a: int, b: int) -> list[list[int]]:
        """Parts of [a, b) not yet in the merged image."""
        parts = [[a, b]]
        for x, y in have:
            nxt = []
            for p, q in parts:
                if y <= p or x >= q:
                    nxt.append([p, q])
                    continue
                if x > p:
                    nxt.append([p, x])
                if y < q:
                    nxt.append([y, q])
            parts = nxt
        return parts

    for image, meta in zip(args.images, metas):
        data = image.read_bytes()
        added = 0
        for a, b in meta.get("captured_ranges", []):
            b = min(b, len(data), size)
            for p, q in covered(a, b):
                merged[p:q] = data[p:q]
                added += q - p
            have.append([a, b])
        print(f"{image.name}: {added} new bytes")
    have.sort()
    joined: list[list[int]] = []
    for a, b in have:
        if joined and a <= joined[-1][1]:
            joined[-1][1] = max(joined[-1][1], b)
        else:
            joined.append([a, b])
    args.out.write_bytes(merged)
    meta = dict(metas[0])
    meta.update(image=str(args.out), captured_ranges=joined, merged_from=[str(p) for p in args.images],
                captured_bytes=sum(b - a for a, b in joined), status="merged")
    Path(str(args.out) + ".json").write_text(json.dumps(meta, indent=1), encoding="utf-8")
    missing, pos = [], 0
    for a, b in joined:
        if a > pos:
            missing.append([hex(pos), hex(a)])
        pos = b
    if pos < size:
        missing.append([hex(pos), hex(size)])
    print(f"merged {sum(b - a for a, b in joined)} of {size} bytes; still missing: {missing[:12]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
