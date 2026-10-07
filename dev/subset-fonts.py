#!/usr/bin/env python3
"""Subset the game's own open-licence fonts into site/fonts/ as WOFF2.

    python dev/subset-fonts.py [GAME_DIR]

Wuthering Waves ships these with its Kuro SDK (Client/Binaries/Win64/ThirdParty/KrPcSdk_Global):
  Kanit Medium  - the wide geometric sans of the game's Latin UI text (OFL 1.1, Cadson Demak)
  SUITE Bold    - the game's Korean UI font (OFL 1.1, Sunn)
Only these two are used: the SDK's Lagu Sans, Arphic and Motoya fonts are commercial and stay out.
Kanit is cut to Latin; SUITE to the Hangul in the Korean home page copy (other Hangul falls back to
the system font). Name tables are kept, so each file carries its copyright and licence.
Needs fontTools with brotli (Anaconda has both).
"""
import hashlib
import json
import sys
from pathlib import Path

from fontTools import subset

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_GAME = Path(r"C:\Program Files (x86)\Steam\steamapps\common\Wuthering Waves")
SDK = Path("Client/Binaries/Win64/ThirdParty/KrPcSdk_Global")
LATIN = "U+0020-007E,U+00A0-024F,U+02BB-02BC,U+02C6,U+02DA,U+02DC,U+1E00-1EFF,U+2000-206F,U+20AC,U+2122,U+2190-2193,U+2212,U+2215,U+FEFF,U+FFFD"


def korean_text():
    data = json.loads((ROOT / "dev/i18n/home/ko.json").read_text(encoding="utf-8"))
    chars = set()

    def walk(value):
        if isinstance(value, str):
            chars.update(c for c in value if "\uac00" <= c <= "\ud7a3" or "\u3130" <= c <= "\u318f")
        elif isinstance(value, dict):
            for v in value.values():
                walk(v)
        elif isinstance(value, list):
            for v in value:
                walk(v)

    walk(data)
    return "".join(sorted(chars))


def cut(source: Path, out: Path, unicodes=None, text=None):
    options = subset.Options()
    options.flavor = "woff2"
    options.name_IDs = ["*"]
    options.name_languages = ["*"]
    options.layout_features = ["*"]
    options.notdef_outline = True
    font = subset.load_font(str(source), options)
    subsetter = subset.Subsetter(options)
    subsetter.populate(unicodes=subset.parse_unicodes(unicodes) if unicodes else [], text=text or "")
    subsetter.subset(font)
    out.parent.mkdir(parents=True, exist_ok=True)
    subset.save_font(font, str(out), options)
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    return {"source": f"{SDK.as_posix()}/{source.name}", "source_sha256": sha(source), "file": f"site/fonts/{out.name}",
            "sha256": sha(out), "bytes": out.stat().st_size}


def main():
    sdk = (Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_GAME) / SDK
    fonts = {
        "kanit-500": cut(sdk / "Kanit-Medium.ttf", ROOT / "site/fonts/kanit-500.woff2", unicodes=LATIN),
        "suite-700-ko": cut(sdk / "SUITE-Bold.otf", ROOT / "site/fonts/suite-700-ko.woff2", text=korean_text()),
    }
    fonts["kanit-500"]["license"] = "LICENSES/fonts/Kanit-OFL.txt"
    fonts["suite-700-ko"]["license"] = "LICENSES/fonts/SUITE-OFL.txt"
    manifest = ROOT / "LICENSES/fonts/manifest.json"
    manifest.write_text(json.dumps({"fonts": fonts}, indent=2) + "\n", encoding="utf-8")
    for name, info in fonts.items():
        print(f"{name}: {info['bytes']:,} bytes -> {info['file']}")


if __name__ == "__main__":
    main()
