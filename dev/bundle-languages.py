#!/usr/bin/env python3
"""Rebuild the translations compiled into the VR plugin from the catalogs.

    python dev/bundle-languages.py [--check]

The plugin embeds mod/uevr/src/assets/wuwa-languages.json (RCDATA 4830) and
reads that copy, plus any per-user files in the profile's wuwa-languages
folder; it does not read mod/localization/wuwa directly. Run this after any
catalog change, then rebuild the plugin for the change to reach the headset.
--check exits 1 if the embedded copy is out of date, without writing.
"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CATALOGS = ROOT / 'mod/localization/wuwa'
BUNDLE = ROOT / 'mod/uevr/src/assets/wuwa-languages.json'


def main():
    codes = sorted(p.stem for p in CATALOGS.glob('*.json') if p.stem != 'contexts')
    languages = {code: json.loads((CATALOGS / f'{code}.json').read_text(encoding='utf-8')) for code in codes}
    data = json.dumps(languages, ensure_ascii=False, separators=(',', ':')).encode('utf-8')
    current = BUNDLE.read_bytes() if BUNDLE.exists() else b''
    print(f'{BUNDLE.relative_to(ROOT).as_posix()}: {len(codes)} languages, '
          + ('up to date' if data == current else 'out of date' if '--check' in sys.argv else 'rebuilt'))
    if data != current:
        if '--check' in sys.argv:
            sys.exit(1)
        BUNDLE.write_bytes(data)


if __name__ == '__main__':
    main()
