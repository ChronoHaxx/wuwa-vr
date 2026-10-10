"""List console-variable names in the game executable with the text stored beside them.

UE registers cvars as (name, help) literal pairs, which the compiler usually places side by
side in .rdata, so the help is very often the string just before or just after the name. That
pairing is a heuristic: the output gives both neighbours as "text_before" and "text_after",
not as confirmed help.

    python -I launcher/dev/wuwa_cvar_inventory.py <Client-Win64-Shipping.exe> <out.tsv>

The output is extracted from the game: keep it private (for example under extracted/).
"""
import re
import sys

exe, out = sys.argv[1], sys.argv[2]
data = open(exe, "rb").read()
utf16 = re.compile(rb"(?:[\x09\x0a\x0d\x20-\x7e]\x00){4,}")
name_rx = re.compile(r"^(r|a|fx|foliage|grass|sg|p|t|ui|lgui|kuro|vr|hmd|s|g|net|slate|streaming|tick|landscape|niagara|anim)\.[A-Za-z0-9_.]+$", re.I)

strings = [m.group().decode("utf-16le") for m in utf16.finditer(data)]


def neighbour(index):
    """The string at index if it reads like prose (help text), else empty."""
    if 0 <= index < len(strings):
        s = strings[index]
        if not name_rx.match(s) and len(s) > 12 and " " in s:
            return " ".join(s.split())[:400]
    return ""


rows = {}
for i, s in enumerate(strings):
    if not name_rx.match(s) or len(s) > 96:
        continue
    found = (neighbour(i - 1), neighbour(i + 1))
    if s not in rows or (any(found) and not any(rows[s])):
        rows[s] = found

with open(out, "w", encoding="utf-8", newline="\n") as f:
    f.write("name\tkuro\ttext_before\ttext_after\n")
    for name in sorted(rows, key=str.lower):
        before, after = rows[name]
        f.write(f"{name}\t{int('kuro' in name.lower())}\t{before}\t{after}\n")
print(f"{len(rows)} names ({sum('kuro' in n.lower() for n in rows)} with 'kuro')")
