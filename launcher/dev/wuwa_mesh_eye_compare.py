"""Compare per-eye mesh bindings from a WuWa LOD trace (mesh-bindings.jsonl). Read-only.

For each captured frame, pairs the bindings of the two eye slots by primitive and reports:
primitives drawn by only one eye, and for primitives drawn by both, which binding fields
differ (mesh batch, element, vertex-factory type, shader, first instance, element flags,
uniform-buffer names). A frozen far object that one eye draws with a different mesh batch
(another LOD) or a different shader shows up here.

    python wuwa_mesh_eye_compare.py trace/mesh-bindings.jsonl
"""
from __future__ import annotations

import json
import sys
from collections import Counter, defaultdict


def main(path: str, eye1_offset: int = 0) -> int:
    rows = [json.loads(line) for line in open(path, encoding="utf-8") if line.strip()]
    binds = [r for r in rows if r.get("type") == "mesh_binding"]
    print(f"{len(binds)} bindings; per (eye, frame): {Counter((b['eye_slot'], b['frame']) for b in binds).most_common(8)}")
    by_frame = defaultdict(lambda: defaultdict(list))
    for b in binds:
        # eye 1 frame f+offset is paired with eye 0 frame f (3.7 without the shared scene frame: offset 1)
        by_frame[b["frame"] - (eye1_offset if b["eye_slot"] == 1 else 0)][b["eye_slot"]].append(b)
    fields = ("mesh", "element", "vf_type", "shader", "first_instance", "element_flags", "code_index")
    only = Counter()
    diffs = Counter()
    examples = defaultdict(list)
    paired_frames = 0
    for frame, eyes in sorted(by_frame.items()):
        if 0 not in eyes or 1 not in eyes:
            continue
        paired_frames += 1
        e0 = defaultdict(list); e1 = defaultdict(list)
        for b in eyes[0]: e0[b["primitive"]].append(b)
        for b in eyes[1]: e1[b["primitive"]].append(b)
        only["eye0 only"] += len(set(e0) - set(e1)); only["eye1 only"] += len(set(e1) - set(e0))
        for prim in set(e0) & set(e1):
            a, c = e0[prim], e1[prim]
            if len(a) != len(c):
                diffs["binding count"] += 1
                examples["binding count"].append((frame, hex(prim), len(a), len(c)))
            for x, y in zip(sorted(a, key=lambda b: b["element"]), sorted(c, key=lambda b: b["element"])):
                for f in fields:
                    if x.get(f) != y.get(f):
                        diffs[f] += 1
                        if len(examples[f]) < 6:
                            examples[f].append((frame, hex(prim), x.get(f), y.get(f)))
                rx = sorted((r["name_hash"] or 0, r.get("size") or 0) for r in x.get("resources", []))
                ry = sorted((r["name_hash"] or 0, r.get("size") or 0) for r in y.get("resources", []))
                if rx != ry:
                    diffs["uniform buffer names/sizes"] += 1
    print(f"frames with both eyes: {paired_frames}")
    print("primitives drawn by one eye only:", dict(only))
    # Order-free comparison: each primitive's per-eye multiset of draw signatures.
    sig = lambda b: (b.get("vf_type"), b.get("shader"), b.get("code_index"), b.get("element_flags"))
    kinds = Counter(); shown = defaultdict(list)
    for frame, eyes in sorted(by_frame.items()):
        if 0 not in eyes or 1 not in eyes:
            continue
        s0 = defaultdict(Counter); s1 = defaultdict(Counter)
        for b in eyes[0]: s0[b["primitive"]][sig(b)] += 1
        for b in eyes[1]: s1[b["primitive"]][sig(b)] += 1
        for prim in set(s0) & set(s1):
            a, c = s0[prim], s1[prim]
            if a == c:
                kinds["identical"] += 1
                continue
            if set(a) == set(c):
                kind = "same signatures, different counts"
            elif {k[0] for k in a} != {k[0] for k in c}:
                kind = "different vertex-factory types"
            elif {k[1] for k in a} != {k[1] for k in c}:
                kind = "different shaders"
            else:
                kind = "different flags/code index"
            kinds[kind] += 1
            if len(shown[kind]) < 4:
                shown[kind].append((frame, hex(prim), dict(a), dict(c)))
    print("primitives drawn by both eyes:", dict(kinds))
    for kind, items in shown.items():
        print(f"== {kind}")
        for frame, prim, a, c in items:
            print(f"  frame {frame} primitive {prim}")
            print(f"    eye0 {a}")
            print(f"    eye1 {c}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 0))
