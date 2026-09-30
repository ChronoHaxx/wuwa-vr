"""Port WuWa build-pinned addresses from one game build to another.

Inputs are two in-process module captures (wuwa-mapped-module-v1, offset == RVA) of
Client-Win64-Shipping.exe: the build the source is pinned to, and the new build. Take the
new one with WuWaModuleCapture's request file plus "any_build": true.

Read-only on the captures. Always writes a report. With --apply it rewrites constants in
the source tree, but only where the mapping is unambiguous:

* code RVAs: the new site is found by matching the bytes around the old site;
* data RVAs (globals, vtables): found through the rel32 references that point at them;
* {rva, size, hash} function ranges: re-hashed only when every differing byte belongs to
  a relocated call/jump/RIP-relative displacement. Any other change (a struct offset, an
  immediate, a different instruction) is reported as "changed" and left untouched, so the
  runtime gate keeps refusing that route until someone reviews it;
* the PE timestamp and image size constants.

    python wuwa_port_offsets.py --old old.bin --new new.bin --report port.json
    python wuwa_port_offsets.py --old old.bin --new new.bin --report port.json --apply
    python wuwa_port_offsets.py --old old.bin --self-test     # old vs old: every site maps to itself
"""
from __future__ import annotations

import argparse
import json
import mmap
import re
import struct
import sys
from collections import Counter
from dataclasses import dataclass, field, asdict
from pathlib import Path

import numpy as np

DEFAULT_SRC = Path(__file__).resolve().parents[2] / "mod" / "uevr" / "src"
WINDOW = 48          # bytes each side of a code site used for matching
ANCHOR = 12          # anchor length searched in the new image
MIN_SCORE = 0.70     # fraction of equal bytes in the window for an accepted match
MARGIN = 0.10        # required lead of the best match over the runner-up
SHIFT_TOLERANCE = 0x10000  # a tie is resolved only by a candidate this close to its neighbours' shift
# Literals in the scanned files that are not WuWa build pins (upstream UEVR test code).
IGNORED = {0x3D13420}


@dataclass
class Image:
    path: Path
    data: mmap.mmap
    view: np.ndarray
    timestamp: int
    size: int
    sections: list  # (name, rva, vsize, executable)
    base: int = 0x140000000

    def section(self, rva: int):
        for s in self.sections:
            if s[1] <= rva < s[1] + s[2]:
                return s
        return None


def load(path: Path) -> Image:
    f = open(path, "rb")
    data = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    timestamp = struct.unpack_from("<I", data, pe + 8)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    size = struct.unpack_from("<I", data, pe + 24 + 56)[0]
    table = pe + 24 + opt_size
    sections = []
    for i in range(count):
        raw = data[table + i * 40: table + (i + 1) * 40]
        name = raw[:8].rstrip(b"\0").decode("latin-1")
        vsize, rva = struct.unpack_from("<II", raw, 8)
        flags = struct.unpack_from("<I", raw, 36)[0]
        sections.append((name, rva, vsize, bool(flags & 0x20000000)))
    base = 0x140000000
    meta = Path(str(path) + ".json")
    if meta.is_file():
        try:
            base = int(json.loads(meta.read_text(encoding="utf-8"))["module"]["base"])
        except (KeyError, ValueError, TypeError):
            pass
    return Image(path, data, np.frombuffer(data, dtype=np.uint8), timestamp, size, sections, base)


# ---------------------------------------------------------------- source scan
HASH_RANGE = re.compile(r"\{\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+|\d+)\s*,\s*(0x[0-9a-fA-F]{16})(ULL)?\s*\}")
BYTE_LIST = re.compile(r"\{\s*((?:0x[0-9a-fA-F]{1,2}\s*,\s*){7,}0x[0-9a-fA-F]{1,2})\s*,?\s*\}")
HEX = re.compile(r"(?<![0-9A-Za-z_])0x[0-9a-fA-F]{6,8}(?![0-9A-Za-z_])")


@dataclass
class Site:
    file: str
    line: int
    start: int          # character offset of the literal in the file
    end: int
    old: int
    kind: str           # rva | hash_range | signature | timestamp | image_size
    size: int = 0
    old_hash: int = 0
    hash_start: int = 0
    hash_end: int = 0
    new: int | None = None
    new_hash: int | None = None
    status: str = "pending"
    detail: dict = field(default_factory=dict)


def source_files(src: Path):
    files = sorted((src / "utility").glob("WuWa*.hpp")) + sorted((src / "mods" / "vr").glob("WuWa*.hpp"))
    files += [src / "mods" / "vr" / "FFakeStereoRenderingHook.cpp", src / "Framework.cpp"]
    return [f for f in files if f.is_file()]


def collect(src: Path, old: Image) -> list[Site]:
    sites: list[Site] = []
    for path in source_files(src):
        text = path.read_text(encoding="utf-8", errors="surrogateescape")
        line_of = lambda pos: text.count("\n", 0, pos) + 1
        taken = set()
        for m in HASH_RANGE.finditer(text):
            rva = int(m.group(1), 16)
            if old.section(rva) is None:
                continue
            sites.append(Site(str(path), line_of(m.start()), m.start(1), m.end(1), rva, "hash_range",
                              size=int(m.group(2), 0), old_hash=int(m.group(3), 16),
                              hash_start=m.start(3), hash_end=m.end(3)))
            taken.add(m.start(1))
        for m in HEX.finditer(text):
            if m.start() in taken:
                continue
            value = int(m.group(0), 16)
            kind = None
            if value == old.timestamp:
                kind = "timestamp"
            elif value == old.size:
                kind = "image_size"
            elif value in IGNORED:
                continue
            elif 0x100000 < value < old.size and old.section(value) is not None:
                kind = "rva"
            if kind:
                sites.append(Site(str(path), line_of(m.start()), m.start(), m.end(), value, kind))
        # Inline byte signatures: a byte list equal to the old image at one of this file's RVAs.
        file_rvas = sorted({x.old for x in sites if x.file == str(path) and x.kind in ("rva", "hash_range")})
        # RVAs declared as named constants win when identical twin functions match one list
        named = {x.old for x in sites if x.file == str(path) and x.kind == "rva"
                 and "constexpr" in text[text.rfind(chr(10), 0, x.start) + 1:x.start]}
        for m in BYTE_LIST.finditer(text):
            body = m.group(1)
            values = bytes(int(v, 16) for v in re.findall(r"0x([0-9a-fA-F]{1,2})", body))
            matches = [rva for rva in file_rvas if bytes(old.data[rva:rva + len(values)]) == values]
            if len(matches) > 1:
                matches = [rva for rva in matches if rva in named] or matches
            if matches:
                site = Site(str(path), line_of(m.start()), m.start(1), m.end(1), matches[0], "signature",
                            size=len(values), detail={"spaced": ", " in body})
                if len(matches) > 1:
                    site.detail["twins"] = [hex(r) for r in matches]
                sites.append(site)
        for m in re.finditer(r"(?<![0-9])%d(?![0-9])" % old.size, text):
            sites.append(Site(str(path), line_of(m.start()), m.start(), m.end(), old.size, "image_size_dec"))
    return sites


# ---------------------------------------------------------------- code mapping
def exec_ranges(img: Image):
    return [(s[1], s[1] + s[2]) for s in img.sections if s[3]]


NEAR = 48 << 20      # first search this far either side of the old RVA


def find_all(img: Image, needle: bytes, cap: int = 64, near: int | None = None):
    hits = []
    for begin, end in exec_ranges(img):
        if near is not None:
            begin, end = max(begin, near - NEAR), min(end, near + NEAR)
            if begin >= end:
                continue
        pos = img.data.find(needle, begin, end)
        while pos != -1 and len(hits) < cap:
            hits.append(pos)
            pos = img.data.find(needle, pos + 1, end)
    return hits


def score(old: Image, new: Image, old_start: int, new_start: int, length: int) -> float:
    if new_start < 0 or new_start + length > len(new.view):
        return 0.0
    a = old.view[old_start:old_start + length]
    b = new.view[new_start:new_start + length]
    return float(np.count_nonzero(a == b)) / length


def map_code(old: Image, new: Image, rva: int, cache: dict) -> dict:
    if rva in cache:
        return cache[rva]
    begin = max(rva - WINDOW, 0)
    length = rva + WINDOW - begin
    window = bytes(old.data[begin:begin + length])

    def search(near):
        votes = Counter()
        for off in range(0, length - ANCHOR + 1, 6):
            anchor = window[off:off + ANCHOR]
            if len(set(anchor)) < 5:
                continue
            hits = find_all(new, anchor, near=near)
            if len(hits) > 16:      # too common to discriminate
                continue
            for h in hits:
                votes[h - off] += 1
            # stop early once one candidate is clearly confirmed by several anchors
            if votes and votes.most_common(1)[0][1] >= 4 and len(votes) == 1:
                break
        return sorted(((score(old, new, begin, c, length), c) for c in votes), reverse=True)

    ranked = search(rva)
    if not ranked or ranked[0][0] < MIN_SCORE:
        ranked = search(None)
    result = {"status": "unmapped", "candidates": len(ranked)}
    if ranked:
        best, cand = ranked[0]
        second = ranked[1][0] if len(ranked) > 1 else 0.0
        result = {"status": "ok" if best >= MIN_SCORE and best - second >= MARGIN else "ambiguous",
                  "new": cand + (rva - begin), "score": round(best, 3), "runner_up": round(second, 3),
                  "candidates": len(ranked),
                  # every candidate within MARGIN of the best, kept for tie resolution by shift
                  "ties": [c + (rva - begin) for sc, c in ranked if best >= MIN_SCORE and best - sc < MARGIN]}
    cache[rva] = result
    return result


# ---------------------------------------------------------------- data mapping
def rel32_references(img: Image, targets: set[int], per_target: int = 6) -> dict[int, list[int]]:
    """Positions of rel32 displacements (target = pos + 4 + disp) that point at targets."""
    want = np.array(sorted(targets), dtype=np.int64)
    found: dict[int, list[int]] = {t: [] for t in targets}
    chunk = 8 << 20
    for begin, end in exec_ranges(img):
        for a in range(begin, end - 4, chunk):
            b = min(a + chunk, end - 4)
            v = img.view[a:b + 3].astype(np.int64)
            d = v[:-3] | (v[1:-2] << 8) | (v[2:-1] << 16) | (v[3:] << 24)
            d = np.where(d >= 1 << 31, d - (1 << 32), d)
            tgt = np.arange(a, b, dtype=np.int64) + 4 + d
            idx = np.nonzero(np.isin(tgt, want))[0]
            for i in idx:
                t = int(tgt[i])
                if len(found[t]) < per_target:
                    found[t].append(a + int(i))
    return found


def map_data(old: Image, new: Image, rva: int, refs: list[int], cache: dict) -> dict:
    votes = Counter()
    used = 0
    for q in refs:
        m = map_code(old, new, q, cache)
        if m["status"] != "ok":
            continue
        qn = m["new"]
        disp = struct.unpack_from("<i", new.data, qn)[0]
        votes[qn + 4 + disp] += 1
        used += 1
    if not votes:
        return {"status": "unmapped", "references": len(refs)}
    (best, n), *rest = votes.most_common()
    ok = n >= 2 and (not rest or rest[0][1] < n) or (n == 1 and used == 1 and len(refs) == 1)
    return {"status": "ok" if ok else "ambiguous", "new": best, "votes": dict(votes.most_common(3)),
            "references": len(refs)}


def resolve_ties(mapped: dict[int, dict]) -> None:
    """Pick among equally good candidates the one whose shift matches nearby confident sites."""
    confident = sorted((r, m["new"] - r) for r, m in mapped.items() if m["status"] == "ok")
    for rva, m in mapped.items():
        if m["status"] != "ambiguous" or not m.get("ties") or not confident:
            continue
        near = sorted(confident, key=lambda c: abs(c[0] - rva))[:4]
        shifts = [sh for _, sh in near]
        expected = sorted(shifts)[len(shifts) // 2]
        fits = [t for t in m["ties"] if abs((t - rva) - expected) <= SHIFT_TOLERANCE]
        if len(fits) > 1:
            # adjacent twin functions: keep only the one moved exactly like the nearest confident site
            nearest_rva, nearest_shift = near[0]
            if abs(nearest_rva - rva) <= 1 << 20:
                exact = [t for t in fits if t - rva == nearest_shift]
                if len(exact) == 1:
                    m.update(status="ok", new=exact[0], resolved_by="nearest_exact_shift",
                             expected_shift=nearest_shift, review=True)
                    continue
        if len(fits) == 1:
            m.update(status="ok", new=fits[0], resolved_by="neighbour_shift", expected_shift=expected)


def local_search(old: Image, new: Image, rva: int, mapped: dict, span: int = 0x40000, width: int = 96) -> dict:
    """Fuzzy match of the old bytes near the RVA its nearest confident neighbours predict."""
    confident = sorted(((abs(r - rva), m["new"] - r) for r, m in mapped.items()
                        if m["status"] == "ok" and m.get("resolved_by") != "local_search"))[:3]
    if not confident:
        return {"status": "unmapped"}
    best_hits = []
    for _, shift in confident:
        pred = rva + shift
        lo = max(pred - span, 0)
        hi = min(pred + span + width, len(new.view))
        region = new.view[lo:hi]
        if len(region) <= width:
            continue
        window = old.view[rva:rva + width]
        count = np.zeros(len(region) - width, np.int32)
        for k in range(width):
            count += region[k:k + len(count)] == window[k]
        order = np.argsort(count)[::-1]
        top = int(order[0])
        runner = next((int(i) for i in order[1:64] if abs(int(i) - top) > 16), None)
        best_hits.append((int(count[top]) / width, (int(count[runner]) / width) if runner is not None else 0.0, lo + top))
    if not best_hits:
        return {"status": "unmapped"}
    best, second, where = max(best_hits)
    ok = best >= 0.85 and best - second >= 0.08
    return {"status": "ok" if ok else "ambiguous", "new": where, "score": round(best, 3),
            "runner_up": round(second, 3), "resolved_by": "local_search", "review": True}


def map_pointer_slot(old: Image, new: Image, rva: int, mapped: dict, cache: dict) -> dict:
    """A data slot that holds a pointer into the image (vtable slot, registered object):
    map the pointee, then find the unique slot in the new image holding the new pointer."""
    if rva + 8 > len(old.view):
        return {"status": "unmapped"}
    value = struct.unpack_from("<Q", old.data, rva)[0] - old.base
    if not (0 <= value < old.size) or old.section(value) is None:
        return {"status": "unmapped", "reason": "slot does not hold an image pointer"}
    sec = old.section(value)
    pointee = mapped.get(value) or (map_code(old, new, value, cache) if sec[3] else None)
    if not pointee or pointee.get("status") != "ok":
        return {"status": "unmapped", "reason": "pointee not mapped"}
    needle = struct.pack("<Q", new.base + pointee["new"])
    hits = []
    for s in new.sections:
        if s[3]:
            continue
        pos = new.data.find(needle, s[1], s[1] + s[2])
        while pos != -1 and len(hits) < 4096:
            if pos % 8 == 0:
                hits.append(pos)
            pos = new.data.find(needle, pos + 1, s[1] + s[2])
    # prefer the hit whose shift matches the slot's neighbours when several slots hold the pointer
    if len(hits) == 1:
        return {"status": "ok", "new": hits[0], "resolved_by": "pointer_slot"}
    confident = [(r, m["new"] - r) for r, m in mapped.items() if m["status"] == "ok"]
    if hits and confident:
        expected = sorted(confident, key=lambda c: abs(c[0] - rva))[0][1]
        fits = [h for h in hits if abs((h - rva) - expected) <= SHIFT_TOLERANCE]
        if len(fits) > 1:
            fits = [h for h in fits if h - rva == expected]
        if len(fits) == 1:
            return {"status": "ok", "new": fits[0], "resolved_by": "pointer_slot+shift", "review": True}
    return {"status": "ambiguous" if hits else "unmapped", "reason": f"{len(hits)} slots hold the pointer"}


# ---------------------------------------------------------------- function ranges
def fnv(buf: bytes) -> int:
    h = 0xCBF29CE484222325
    for x in buf:
        h = ((h ^ x) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def displacement_field(code: bytes, start: int) -> bool:
    """True when code[start:start+4] is a rel32 displacement (call/jmp/jcc/RIP-relative)."""
    if start >= 1 and code[start - 1] in (0xE8, 0xE9):
        return True
    if start >= 2 and code[start - 2] == 0x0F and 0x80 <= code[start - 1] <= 0x8F:
        return True
    if start >= 1 and (code[start - 1] & 0xC7) == 0x05:   # ModRM mod=00 rm=101: [rip+disp32]
        return True
    return False


def compare_function(old: Image, new: Image, rva: int, nrva: int, size: int) -> dict:
    a = bytes(old.data[rva:rva + size])
    b = bytes(new.data[nrva:nrva + size])
    diffs = [i for i in range(size) if a[i] != b[i]]
    unexplained = []
    for i in diffs:
        # a differing byte is explained if it sits inside some 4-byte displacement field
        # the field may run past the end of the compared span; only its opcode must be inside
        if not any(displacement_field(a, s) and displacement_field(b, s)
                   for s in range(max(i - 3, 0), i + 1)):
            unexplained.append(i)
    return {"differing_bytes": len(diffs), "unexplained": unexplained[:16],
            "unexplained_count": len(unexplained), "new_hash": fnv(b)}


# ---------------------------------------------------------------- driver
def run(old: Image, new: Image, src: Path) -> list[Site]:
    sites = collect(src, old)
    cache: dict = {}
    code, data = set(), set()
    for s in sites:
        if s.kind in ("rva", "hash_range"):
            sec = old.section(s.old)
            (code if sec and sec[3] else data).add(s.old)
    import time
    t = time.monotonic()
    log = lambda what: print(f"[{time.monotonic() - t:6.1f}s] {what}", file=sys.stderr, flush=True)
    log(f"{len(sites)} sites, {len(code)} code RVAs, {len(data)} data RVAs")
    refs = rel32_references(old, data | code) if (data or code) else {}
    log("references scanned")
    mapped: dict[int, dict] = {}
    for rva in sorted(code):
        mapped[rva] = map_code(old, new, rva, cache)
    log("code sites mapped")
    for rva in sorted(data):
        mapped[rva] = map_data(old, new, rva, refs.get(rva, []), cache)
    log("data sites mapped")
    resolve_ties(mapped)
    # Code sites still open: first through their callers (call/jmp/lea rel32 that target them),
    # then by a local fuzzy search around the position their confident neighbours predict.
    for rva in sorted(code):
        if mapped[rva]["status"] == "ok":
            continue
        via = map_data(old, new, rva, refs.get(rva, []), cache) if refs.get(rva) else {"status": "unmapped"}
        if via["status"] == "ok":
            via["resolved_by"] = "callers"
            mapped[rva] = via
            continue
        local = local_search(old, new, rva, mapped)
        if local["status"] == "ok":
            mapped[rva] = local
    log("callers and local search done")
    for rva in sorted(data):
        if mapped[rva]["status"] != "ok":
            fallback = map_pointer_slot(old, new, rva, mapped, cache)
            if fallback["status"] == "ok":
                mapped[rva] = fallback
    log("ties and pointer slots resolved")
    for s in sites:
        if s.kind == "timestamp":
            s.new, s.status = new.timestamp, "ok"
        elif s.kind == "signature":
            m = mapped.get(s.old, {"status": "unmapped"})
            spaced, twins = s.detail.get("spaced", False), s.detail.get("twins")
            s.status, s.new = m["status"], m.get("new")
            s.detail = {"spaced": spaced, "site_status": m["status"]}
            if twins:
                s.detail["twins"] = twins
                s.status = "ambiguous"
            if s.status == "ok":
                cmp = compare_function(old, new, s.old, s.new, s.size)
                s.detail["bytes"] = {k: v for k, v in cmp.items() if k != "new_hash"}
                if cmp["unexplained_count"]:
                    s.status = "changed"
        elif s.kind in ("image_size", "image_size_dec"):
            s.new, s.status = new.size, "ok"
        else:
            m = mapped[s.old]
            s.detail = {k: v for k, v in m.items() if k not in ("new",)}
            s.status = m["status"]
            s.new = m.get("new")
            if s.kind == "hash_range" and s.status == "ok":
                cmp = compare_function(old, new, s.old, s.new, s.size)
                s.detail["function"] = {k: v for k, v in cmp.items() if k != "new_hash"}
                if cmp["unexplained_count"]:
                    s.status = "changed"
                else:
                    s.new_hash = cmp["new_hash"]
    return sites


def apply(sites: list[Site], new_image: Image) -> dict[str, int]:
    by_file: dict[str, list[tuple[int, int, str]]] = {}
    for s in sites:
        if s.status != "ok" or s.new is None:
            continue
        if s.kind == "signature":
            sep = ", " if s.detail.get("spaced") else ","
            new_bytes = bytes(new_image.data[s.new:s.new + s.size])
            by_file.setdefault(s.file, []).append((s.start, s.end, sep.join(f"0x{b:02x}" for b in new_bytes)))
            continue
        literal = str(s.new) if s.kind == "image_size_dec" else f"0x{s.new:x}"
        by_file.setdefault(s.file, []).append((s.start, s.end, literal))
        if s.kind == "hash_range" and s.new_hash is not None:
            by_file[s.file].append((s.hash_start, s.hash_end, f"0x{s.new_hash:016x}"))
    changed = {}
    for file, edits in by_file.items():
        crlf = b"\r\n" in Path(file).read_bytes()
        text = Path(file).read_text(encoding="utf-8", errors="surrogateescape")   # offsets are in LF text
        for start, end, literal in sorted(set(edits), reverse=True):
            text = text[:start] + literal + text[end:]
        with open(file, "w", encoding="utf-8", errors="surrogateescape", newline="\r\n" if crlf else "\n") as f:
            f.write(text)
        changed[file] = len(edits)
    return changed


def summary(sites: list[Site]) -> dict:
    c = Counter((s.kind, s.status) for s in sites)
    return {f"{k}:{st}": n for (k, st), n in sorted(c.items())}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--old", type=Path, required=True)
    ap.add_argument("--new", type=Path)
    ap.add_argument("--src", type=Path, default=DEFAULT_SRC)
    ap.add_argument("--report", type=Path)
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--self-test", action="store_true", help="map the old image onto itself; every site must be identity")
    ap.add_argument("--from-report", type=Path, help="apply an existing report instead of mapping again")
    ap.add_argument("--files", nargs="*", help="apply only to these file names (e.g. WuWaLguiRedirect.hpp)")
    args = ap.parse_args()
    if args.from_report:
        if not (args.apply and args.new):
            ap.error("--from-report needs --new and --apply")
        data = json.loads(args.from_report.read_text(encoding="utf-8"))
        sites = [Site(**d) for d in data["sites"]]
        if args.files:
            sites = [x for x in sites if Path(x.file).name in set(args.files)]
        print(json.dumps(apply(sites, load(args.new)), indent=1))
        return 0
    old = load(args.old)
    new = old if args.self_test else load(args.new)
    if new is None:
        ap.error("--new is required unless --self-test")
    sites = run(old, new, args.src)
    report = {"old": {"path": str(old.path), "timestamp": hex(old.timestamp), "size": old.size},
              "new": {"path": str(new.path), "timestamp": hex(new.timestamp), "size": new.size},
              "summary": summary(sites), "sites": [asdict(s) for s in sites]}
    if args.self_test:
        bad = [s for s in sites if s.kind in ("rva", "hash_range", "signature") and (s.status != "ok" or s.new != s.old
               or (s.kind == "hash_range" and s.new_hash != s.old_hash))]
        report["self_test_failures"] = [f"{Path(s.file).name}:{s.line} {s.old:#x} {s.status}" for s in bad]
    if args.apply and not args.self_test:
        report["applied"] = apply(sites, new)
    if args.report:
        args.report.write_text(json.dumps(report, indent=1, default=str), encoding="utf-8")
    print(json.dumps(report["summary"], indent=1))
    if args.self_test:
        print("self-test failures:", len(report["self_test_failures"]))
        for line in report["self_test_failures"][:20]:
            print("  ", line)
        return 1 if report["self_test_failures"] else 0
    todo = [s for s in sites if s.status != "ok"]
    for s in todo[:40]:
        print(f"REVIEW {Path(s.file).name}:{s.line} {s.kind} {s.old:#x} -> {s.status} {s.detail}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
