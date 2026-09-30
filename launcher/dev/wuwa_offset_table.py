"""Build an old->new struct-offset table from aligned instructions of ported functions.

Reads a port report (wuwa_port_offsets.py) and both captures, disassembles every mapped
hash range and signature site in the old and new image, aligns them instruction by
instruction, and records memory-operand displacements and immediates that differ while
the rest of the instruction is identical. Displacements that appear unchanged are
recorded too, so an offset can be confirmed as stable. Read-only.

    python wuwa_offset_table.py --report port.json --old old.bin --new new.bin --out table.json
"""
import argparse, json, mmap, re
from collections import defaultdict, Counter
from pathlib import Path
import capstone
from capstone import x86

ap = argparse.ArgumentParser()
ap.add_argument("--report", type=Path, required=True)
ap.add_argument("--old", type=Path, required=True)
ap.add_argument("--new", type=Path, required=True)
ap.add_argument("--out", type=Path, required=True)
ap.add_argument("--extra", nargs="*", default=[], help="old:new:size triples in hex")
args = ap.parse_args()
fo = open(args.old, "rb"); om = mmap.mmap(fo.fileno(), 0, access=mmap.ACCESS_READ)
fn = open(args.new, "rb"); nm = mmap.mmap(fn.fileno(), 0, access=mmap.ACCESS_READ)
cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64); cs.detail = True
report = json.loads(args.report.read_text(encoding="utf-8"))
pairs = {}
for s in report["sites"]:
    if s["kind"] == "hash_range" and s["new"] is not None and s["status"] in ("ok", "changed"):
        pairs[s["old"]] = (s["new"], s["size"])
for t in args.extra:
    a, b, c = (int(x, 16) for x in t.split(":"))
    pairs[a] = (b, c)

def operands(ins):
    out = []
    for op in ins.operands:
        if op.type == x86.X86_OP_MEM:
            base = ins.reg_name(op.mem.base) if op.mem.base else ""
            index = ins.reg_name(op.mem.index) if op.mem.index else ""
            out.append(("mem", base, index, op.mem.scale, op.mem.disp))
        elif op.type == x86.X86_OP_IMM:
            out.append(("imm", op.imm))
        elif op.type == x86.X86_OP_REG:
            out.append(("reg", ins.reg_name(op.reg)))
    return out

changed = defaultdict(Counter)   # old value -> Counter(new value)
stable = Counter()
mismatch = 0
import difflib
def shape(ins):
    return (ins.mnemonic,) + tuple(o[:4] if o[0] == "mem" else (o[0],) if o[0] == "imm" else o for o in operands(ins))
for a, (b, size) in sorted(pairs.items()):
    oi = [i for i in cs.disasm(om[a:a + size], a)]
    ni = [i for i in cs.disasm(nm[b:b + size + 64], b)]
    if oi:
        end = oi[-1].address + oi[-1].size - a
        ni = [i for i in ni if i.address < b + end + 32]
    sm = difflib.SequenceMatcher(a=[shape(i) for i in oi], b=[shape(i) for i in ni], autojunk=False)
    aligned = []
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "equal":
            aligned += list(zip(oi[i1:i2], ni[j1:j2]))
        else:
            mismatch += max(i2 - i1, j2 - j1)
    for x, y in aligned:
        ox, oy = operands(x), operands(y)
        if len(ox) != len(oy):
            mismatch += 1
            continue
        for p, q in zip(ox, oy):
            if p[0] != q[0]:
                continue
            if p[0] == "mem" and p[1:4] == q[1:4] and p[1] not in ("rip",):
                (stable.update([(p[1], p[4])]) if p[4] == q[4] else changed[(p[1], p[4])].update([q[4]]))
            elif p[0] == "imm" and not ("call" in x.mnemonic or x.mnemonic.startswith("j")):
                if p[1] != q[1] and abs(p[1]) < 0x100000:
                    changed[("imm", p[1])].update([q[1]])
table = {"pairs": len(pairs), "unaligned_instructions": mismatch,
         "changed": {f"{k[0]}+{k[1]:#x}": {f"{v:#x}": n for v, n in c.items()} for k, c in sorted(changed.items(), key=lambda kv: (kv[0][0], kv[0][1]))},
         "stable": {f"{k[0]}+{k[1]:#x}": n for k, n in sorted(stable.items(), key=lambda kv: (kv[0][0], kv[0][1]))}}
args.out.write_text(json.dumps(table, indent=1), encoding="utf-8")
print(f"{len(pairs)} ranges, {len(changed)} changed operands, {len(stable)} stable, {mismatch} unaligned")
for k, v in list(table["changed"].items())[:80]:
    print(" ", k, "->", v)
