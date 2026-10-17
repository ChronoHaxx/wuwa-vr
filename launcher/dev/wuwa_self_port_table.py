"""Build the self-port table: exact fingerprints of every build-pinned site, for the plugin to
find them again in a new game build at startup (utility/WuWaSelfPort.hpp).

The source constants stay pinned to one reference build. This tool reads a capture of that build
(wuwa-mapped-module-v1, offset == RVA) and, for every RVA, {rva, size, hash} range and byte
signature the port tool finds in the source (wuwa_port_offsets.collect), records:

* code sites: the reference bytes from the site on, with every rel32 call/jump target and every
  RIP-relative displacement masked (decoded with Capstone, so only those fields are masked),
  grown until the masked window is unique in the reference image's code;
* data sites (globals, vtables) and code sites without a unique window: the instructions that
  reach them through a rel32 field, each with its own unique window;
* pointer slots: the code site the slot points at;
* ranges and signatures: the masked FNV-1a hash and the masked field positions, so a moved
  function is accepted only when every byte outside those fields is unchanged.

    python wuwa_self_port_table.py --image ref.bin --header ../../mod/uevr/src/utility/WuWaSelfPortTable.hpp
    python wuwa_self_port_table.py --image ref.bin --simulate other.bin --expect port.json

--simulate runs the same matching the plugin runs (Python reference of WuWaSelfPort.hpp) on
another capture and reports every site; --expect compares the result with a port report
(wuwa_port_offsets.py) between the two images, in either direction.
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass, field, asdict
from pathlib import Path

import capstone
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import wuwa_port_offsets as port  # noqa: E402

WINDOWS = (32, 48, 64, 96, 128, 192, 256)  # masked window sizes tried in order
ANCHOR = 8          # bytes of the window's longest unmasked run used to find candidates
MAX_REFS = 4        # references kept per site reached through rel32 fields
TWIN_SPAN = 1 << 20 # a twin is resolved by a uniquely found site at most this far away
CS = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
CS.detail = True


@dataclass
class Window:
    begin: int          # reference RVA of the first byte
    data: bytes
    mask: bytes         # 1 = compare, 0 = moved field
    anchor: int         # offset of the ANCHOR-byte unmasked run used for the search
    twins: int = 1      # matches in the reference image (1 = unique)


@dataclass
class Reference:
    window: Window
    field: int          # offset in the window of the rel32 field
    end: int            # offset in the window of the end of that instruction (target = end + disp)


@dataclass
class Entry:
    rva: int
    kind: str           # code | refs | pointer | unresolved
    window: Window | None = None
    refs: list = field(default_factory=list)
    pointee: int = 0
    note: str = ""


@dataclass
class Range:
    rva: int
    size: int
    hash: int           # raw FNV-1a of the reference bytes
    masked: int         # FNV-1a with the fields zeroed
    fields: list        # (offset, length) inside the range


# ------------------------------------------------------------------ decoding
def moved_fields(code: bytes, address: int):
    """(offset, length, end) of every rel32 branch target and RIP-relative displacement,
    decoding linearly from code[0]. Stops at the first undecodable byte."""
    out, decoded = [], 0
    for insn in CS.disasm(code, address):
        start = insn.address - address
        # A RIP-relative displacement is always 4 bytes in 64-bit code; Capstone 5 reports
        # disp_size 2 when a 0x66 operand-size prefix is present (movdqa, mov word ptr).
        if insn.disp_offset and any(op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP
                                    for op in insn.operands):
            out.append((start + insn.disp_offset, 4, start + insn.size))
        if insn.imm_size == 4 and insn.group(capstone.x86.X86_GRP_BRANCH_RELATIVE):
            out.append((start + insn.imm_offset, 4, start + insn.size))
        decoded = start + insn.size
    return out, decoded


def mask_for(code: bytes, address: int, tail: bytes = b"") -> tuple[bytes, int]:
    """Mask of code; tail is the bytes after it, so an instruction cut by the window end still decodes."""
    fields, decoded = moved_fields(code + tail, address)
    mask = bytearray(b"\x01" * len(code))
    for off, length, _ in fields:
        for i in range(off, min(off + length, len(code))):
            mask[i] = 0
    return bytes(mask), min(decoded, len(code))


def anchor_of(code: bytes, mask: bytes) -> int | None:
    """The unmasked ANCHOR-byte run with the most distinct bytes (rarer than a prologue)."""
    best, best_score = None, -1
    for i in range(len(code) - ANCHOR + 1):
        if all(mask[i:i + ANCHOR]):
            score = len(set(code[i:i + ANCHOR]))
            if score > best_score:
                best, best_score = i, score
    return best


def masked_equal(img: port.Image, at: int, w: Window) -> bool:
    if at < 0 or at + len(w.data) > len(img.view):
        return False
    live = img.view[at:at + len(w.data)]
    ref = np.frombuffer(w.data, np.uint8)
    m = np.frombuffer(w.mask, np.uint8).astype(bool)
    return bool(np.array_equal(live[m], ref[m]))


def find_window(img: port.Image, w: Window, cap: int = 8) -> list[int]:
    needle = w.data[w.anchor:w.anchor + ANCHOR]
    hits = []
    for begin, end in port.exec_ranges(img):
        pos = img.data.find(needle, begin, end)
        while pos != -1:
            at = pos - w.anchor
            if masked_equal(img, at, w):
                hits.append(at)
                if len(hits) >= cap:
                    return hits
            pos = img.data.find(needle, pos + 1, end)
    return hits


def build_window(img: port.Image, rva: int) -> Window | None:
    sec = img.section(rva)
    best = None
    for size in WINDOWS:
        size = min(size, sec[1] + sec[2] - rva)
        code = bytes(img.data[rva:rva + size])
        mask, decoded = mask_for(code, rva, bytes(img.data[rva + size:rva + size + 15]))
        if decoded < min(size, 16):
            return None
        anchor = anchor_of(code, mask)
        if anchor is None:
            continue
        w = Window(rva, code, mask, anchor)
        hits = find_window(img, w, cap=64)
        assert rva in hits or len(hits) == 64, hex(rva)
        w.twins = len(hits)
        best = w
        if len(hits) == 1:
            break
    return best


# ------------------------------------------------------------------ references
def references(img: port.Image, targets: set[int]) -> dict[int, list[tuple[int, int, int]]]:
    """(instruction start, field offset, instruction end) of rel32 fields that reach a target."""
    want = np.array(sorted(targets), dtype=np.int64)
    found: dict[int, list] = defaultdict(list)
    chunk = 8 << 20
    for begin, end in port.exec_ranges(img):
        for a in range(begin, end - 4, chunk):
            b = min(a + chunk, end - 4)
            v = img.view[a:b + 3].astype(np.int64)
            d = v[:-3] | (v[1:-2] << 8) | (v[2:-1] << 16) | (v[3:] << 24)
            d = np.where(d >= 1 << 31, d - (1 << 32), d)
            pos = np.arange(a, b, dtype=np.int64)
            for tail in (4, 5, 6, 8):           # field end + immediate after it
                idx = np.nonzero(np.isin(pos + tail + d, want))[0]
                for i in idx:
                    q = a + int(i)
                    t = q + tail + int(d[i])
                    hit = decode_reference(img, q, t)
                    if hit and hit not in found[t]:
                        found[t].append(hit)
    return found


def decode_reference(img: port.Image, q: int, target: int):
    for back in range(1, 12):
        s = q - back
        code = bytes(img.data[s:s + 16])
        insn = next(CS.disasm(code, s, 1), None)
        if insn is None:
            continue
        fields, _ = moved_fields(code[:insn.size], s)
        for off, length, end in fields:
            if s + off == q:
                disp = struct.unpack_from("<i", img.data, q)[0]
                if s + end + disp == target:
                    return (s, back, end)
    return None


# ------------------------------------------------------------------ table
def in_comment(site) -> bool:
    text = Path(site.file).read_text(encoding="utf-8", errors="surrogateescape")
    line = text[text.rfind("\n", 0, site.start) + 1:site.start]
    return "//" in line


def build(img: port.Image, src: Path):
    sites = [s for s in port.collect(src, img) if not in_comment(s)]
    code_rvas, data_rvas, ranges = set(), set(), {}
    for s in sites:
        if s.kind in ("rva", "hash_range", "signature"):
            sec = img.section(s.old)
            if sec is None:
                continue
            (code_rvas if sec[3] else data_rvas).add(s.old)
        if s.kind in ("hash_range", "signature") and img.section(s.old) and img.section(s.old)[3]:
            ranges[(s.old, s.size)] = None
    entries: dict[int, Entry] = {}
    for rva in sorted(code_rvas):
        w = build_window(img, rva)
        entries[rva] = Entry(rva, "code", window=w) if w else Entry(rva, "unresolved", note="no decodable window")
    # sites reached through rel32 fields: data, and code without a unique window of its own
    need = {r for r in data_rvas} | {r for r, e in entries.items() if e.kind == "code" and e.window.twins > 1}
    refs = references(img, need) if need else {}
    for rva in sorted(need):
        kept = []
        for start, field_off, end in refs.get(rva, [])[:12]:
            w = build_window(img, start)
            if w and w.twins == 1:
                kept.append(Reference(w, field_off, end))
            if len(kept) >= MAX_REFS:
                break
        if rva in data_rvas:
            entries[rva] = Entry(rva, "refs", refs=kept) if kept else Entry(rva, "unresolved", note="no unique reference")
        elif kept:
            entries[rva].refs = kept       # twin code window: references decide
    for rva in sorted(data_rvas):
        if entries[rva].kind != "unresolved" or rva + 8 > len(img.view):
            continue
        value = struct.unpack_from("<Q", img.data, rva)[0] - img.base
        if 0 <= value < img.size and img.section(value) and img.section(value)[3]:
            if value not in entries:
                w = build_window(img, value)
                if w:
                    entries[value] = Entry(value, "code", window=w, note="pointee")
            if value in entries and entries[value].kind == "code":
                entries[rva] = Entry(rva, "pointer", pointee=value)
    out_ranges = []
    for (rva, size) in sorted(ranges):
        code = bytes(img.data[rva:rva + size])
        fields, _ = moved_fields(bytes(img.data[rva:rva + size + 16]), rva)
        fl = [(o, min(l, size - o)) for o, l, _ in fields if o < size]
        masked = bytearray(code)
        for o, l in fl:
            masked[o:o + l] = b"\0" * l
        out_ranges.append(Range(rva, size, port.fnv(code), port.fnv(bytes(masked)), fl))
    # The source must be pinned to this image: every hash in it is the reference bytes' hash.
    raw = {(r.rva, r.size): r.hash for r in out_ranges}
    stale = [s for s in sites if s.kind == "hash_range" and raw.get((s.old, s.size), s.old_hash) != s.old_hash]
    assert not stale, f"source hashes are not this image's: {[(hex(s.old), Path(s.file).name) for s in stale][:5]}"
    return sites, entries, out_ranges


# ------------------------------------------------------------------ matching (reference of WuWaSelfPort.hpp)
def simulate(new: port.Image, entries: dict[int, Entry], ranges: list[Range]):
    found: dict[int, dict] = {}
    for rva, e in entries.items():
        if e.kind == "code" and e.window:
            hits = find_window(new, e.window)
            found[rva] = {"hits": hits}
    mapped = {r: f["hits"][0] for r, f in found.items() if len(f["hits"]) == 1 and entries[r].window.twins == 1}
    result = {}
    for rva, e in entries.items():
        if rva in mapped:
            result[rva] = {"status": "ok", "new": mapped[rva], "by": "window"}
    # twins: the spot the nearest uniquely found site predicts (its exact shift) must match the window
    for rva, e in entries.items():
        if e.kind != "code" or rva in result or not e.window:
            continue
        near = sorted((abs(r - rva), r) for r in mapped if abs(r - rva) <= TWIN_SPAN)[:3]
        spot = next((rva + mapped[r] - r for _, r in near if masked_equal(new, rva + mapped[r] - r, e.window)), None)
        if spot is not None:
            result[rva] = {"status": "ok", "new": spot, "by": "twin_shift"}
        else:
            result[rva] = {"status": "refused", "hits": len(found[rva]["hits"])}
    # references (data, and twins the shift rule left open)
    for rva, e in entries.items():
        if not e.refs or (rva in result and result[rva]["status"] == "ok"):
            continue
        targets = []
        for ref in e.refs:
            hits = find_window(new, ref.window, cap=2)
            if len(hits) == 1:
                at = hits[0]
                disp = struct.unpack_from("<i", new.data, at + ref.field)[0]
                targets.append(at + ref.end + disp)
        votes = Counter(targets)
        if len(votes) == 1:
            result[rva] = {"status": "ok", "new": targets[0], "by": f"refs:{len(targets)}"}
        else:
            result[rva] = {"status": "refused", "votes": {hex(k): v for k, v in votes.items()}}
    for rva, e in entries.items():
        if e.kind == "pointer":
            p = result.get(e.pointee, {})
            if p.get("status") != "ok":
                result[rva] = {"status": "refused", "reason": "pointee"}
                continue
            needle = struct.pack("<Q", new.base + p["new"])
            hits = []
            for s in new.sections:
                if s[3]:
                    continue
                pos = new.data.find(needle, s[1], s[1] + s[2])
                while pos != -1 and len(hits) < 4:
                    if pos % 8 == 0:
                        hits.append(pos)
                    pos = new.data.find(needle, pos + 1, s[1] + s[2])
            result[rva] = {"status": "ok", "new": hits[0], "by": "pointer"} if len(hits) == 1 else \
                {"status": "refused", "slots": len(hits)}
        elif e.kind == "unresolved":
            result[rva] = {"status": "refused", "reason": e.note}
    checked = []
    for r in ranges:
        m = result.get(r.rva, {})
        if m.get("status") != "ok":
            checked.append({"rva": r.rva, "size": r.size, "status": "unmapped"})
            continue
        live = bytearray(new.data[m["new"]:m["new"] + r.size])
        for o, l in r.fields:
            live[o:o + l] = b"\0" * l
        ok = port.fnv(bytes(live)) == r.masked
        checked.append({"rva": r.rva, "size": r.size, "status": "ok" if ok else "changed", "new": m["new"],
                        "new_hash": port.fnv(bytes(new.data[m["new"]:m["new"] + r.size]))})
    return result, checked


# ------------------------------------------------------------------ header
def header(img: port.Image, entries: dict[int, Entry], ranges: list[Range]) -> str:
    pool = bytearray()
    windows = []           # (begin, size, offset into pool, anchor, twins)
    index = {}

    def add(w: Window) -> int:
        key = (w.begin, len(w.data))
        if key not in index:
            index[key] = len(windows)
            windows.append((w.begin, len(w.data), len(pool), w.anchor, w.twins))
            pool.extend(w.data)
            pool.extend(w.mask)
        return index[key]

    sites, refs = [], []
    kinds = {"code": 0, "refs": 1, "pointer": 2, "unresolved": 3}
    for rva in sorted(entries):
        e = entries[rva]
        first = len(refs)
        for r in e.refs:
            refs.append((add(r.window), r.field, r.end))
        sites.append((rva, kinds[e.kind], add(e.window) if e.window else 0xFFFF, first, len(e.refs), e.pointee))
    fields = []
    out_ranges = []
    for r in ranges:
        out_ranges.append((r.rva, r.size, r.hash, r.masked, len(fields), len(r.fields)))
        fields.extend(r.fields)

    def rows(items, fmt, per=1):
        lines = [", ".join(fmt(x) for x in items[i:i + per]) for i in range(0, len(items), per)]
        return ",\n".join("    " + line for line in lines)

    return f"""#pragma once
// Generated by launcher/dev/wuwa_self_port_table.py from a capture of the reference build. Do not edit.
// The fingerprints of every build-pinned site, for WuWaSelfPort.hpp to find them in another build.
#include <cstdint>

namespace wuwa_self_port_table {{
constexpr uint32_t reference_timestamp = 0x{img.timestamp:08x};
constexpr uint32_t reference_image_size = 0x{img.size:x};
struct Window {{ uint32_t begin; uint16_t size; uint32_t pool; uint16_t anchor; uint16_t twins; }};
struct Site {{ uint32_t rva; uint8_t kind; uint16_t window; uint16_t first_ref, refs; uint32_t pointee; }};
struct Ref {{ uint16_t window; uint16_t field; uint16_t end; }};
struct Range {{ uint32_t rva, size; uint64_t hash, masked; uint32_t first_field; uint16_t fields; }};
struct Field {{ uint16_t offset; uint8_t length; }};
enum Kind : uint8_t {{ kind_code = 0, kind_refs = 1, kind_pointer = 2, kind_unresolved = 3 }};
constexpr uint16_t no_window = 0xffff;
constexpr uint32_t anchor_size = {ANCHOR};
constexpr uint32_t twin_span = 0x{TWIN_SPAN:x};
// Window bytes, each followed by its mask (1 = compare, 0 = moved field).
inline constexpr uint8_t pool[] = {{
{rows(list(pool), lambda b: f"0x{b:02x}", 24)}
}};
inline constexpr Window windows[] = {{
{rows(windows, lambda w: f"{{0x{w[0]:x}, {w[1]}, {w[2]}, {w[3]}, {w[4]}}}")}
}};
inline constexpr Site sites[] = {{
{rows(sites, lambda s: f"{{0x{s[0]:x}, {s[1]}, {s[2]}, {s[3]}, {s[4]}, 0x{s[5]:x}}}")}
}};
inline constexpr Ref refs[] = {{
{rows(refs, lambda r: f"{{{r[0]}, {r[1]}, {r[2]}}}") if refs else "    {0xffff, 0, 0}"}
}};
inline constexpr Range ranges[] = {{
{rows(out_ranges, lambda r: f"{{0x{r[0]:x}, {r[1]}, 0x{r[2]:016x}ULL, 0x{r[3]:016x}ULL, {r[4]}, {r[5]}}}")}
}};
inline constexpr Field fields[] = {{
{rows(fields, lambda f: f"{{{f[0]}, {f[1]}}}", 8) if fields else "    {0, 0}"}
}};
}} // namespace wuwa_self_port_table
"""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--image", type=Path, required=True, help="capture of the reference build")
    ap.add_argument("--src", type=Path, default=port.DEFAULT_SRC)
    ap.add_argument("--header", type=Path, help="write the C++ table here")
    ap.add_argument("--report", type=Path, help="write the table and any simulation as JSON")
    ap.add_argument("--simulate", type=Path, help="match the table in this capture")
    ap.add_argument("--expect", type=Path, help="port report between --image and --simulate (either direction)")
    a = ap.parse_args()
    img = port.load(a.image)
    sites, entries, ranges = build(img, a.src)
    kinds = Counter(e.kind for e in entries.values())
    twins = sum(1 for e in entries.values() if e.window and e.window.twins > 1)
    print(f"reference {img.timestamp:#x}: {len(entries)} sites {dict(kinds)}, {twins} twin windows, {len(ranges)} ranges")
    for e in entries.values():
        if e.kind == "unresolved" or (e.window and e.window.twins > 1 and not e.refs):
            print(f"  note {e.rva:#x} {e.kind} {e.note} twins={e.window.twins if e.window else '-'}")
    report = {"reference": {"path": str(img.path), "timestamp": hex(img.timestamp), "size": img.size},
              "sites": {hex(r): {"kind": e.kind, "window": len(e.window.data) if e.window else None,
                                 "twins": e.window.twins if e.window else None, "refs": len(e.refs),
                                 "pointee": hex(e.pointee) if e.pointee else None, "note": e.note}
                        for r, e in sorted(entries.items())}}
    if a.header:
        a.header.write_bytes(header(img, entries, ranges).encode("utf-8"))
        print("wrote", a.header)
    failures = 0
    if a.simulate:
        new = port.load(a.simulate)
        result, checked = simulate(new, entries, ranges)
        st = Counter(v["status"] for v in result.values())
        rs = Counter(c["status"] for c in checked)
        print(f"simulate on {new.timestamp:#x}: sites {dict(st)}; ranges {dict(rs)}")
        for rva, v in sorted(result.items()):
            if v["status"] != "ok":
                print(f"  refused {rva:#x}: {v}")
        report["simulation"] = {"image": str(new.path), "timestamp": hex(new.timestamp),
                                "sites": {hex(k): v for k, v in sorted(result.items())}, "ranges": checked}
        if a.expect:
            exp = json.loads(a.expect.read_text(encoding="utf-8"))
            forward = int(exp["old"]["timestamp"], 16) == img.timestamp
            want = {}
            for s in exp["sites"]:
                if s["kind"] in ("rva", "hash_range", "signature") and s["status"] == "ok":
                    a_, b_ = (s["old"], s["new"]) if forward else (s["new"], s["old"])
                    want[a_] = b_
            mism = [(r, v.get("new"), want[r]) for r, v in result.items() if v["status"] == "ok" and r in want and v["new"] != want[r]]
            missing = [r for r in want if r in result and result[r]["status"] != "ok"]
            changed = [c for c in checked if c["status"] == "changed"]
            print(f"expect ({'forward' if forward else 'reverse'}): {len(want)} sites in the port report; "
                  f"{len(mism)} disagree, {len(missing)} refused here, {len(changed)} ranges changed")
            for r, got, w in mism[:20]:
                print(f"  DISAGREE {r:#x}: self-port {got:#x} vs port {w:#x}")
            failures = len(mism)
            report["expect"] = {"disagree": [[hex(r), hex(g), hex(w)] for r, g, w in mism],
                                "refused": [hex(r) for r in missing]}
    if a.report:
        a.report.write_text(json.dumps(report, indent=1), encoding="utf-8")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
