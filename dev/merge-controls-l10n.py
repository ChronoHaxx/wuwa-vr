#!/usr/bin/env python3
"""Add the controller strings in dev/i18n/controls-l10n.json to the mod catalogs.

    python dev/merge-controls-l10n.py [--check]

Only missing keys are added; an existing translation is never replaced. English
maps each new key to itself. --check reports what would change and exits 1 if
anything is missing, without writing.
"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CATALOGS = ROOT / 'mod/localization/wuwa'
LANGS = ['en', 'zh-Hans', 'ja', 'ko', 'es', 'pt-BR', 'fr', 'de', 'ru', 'ar']


def dump(data):
    return json.dumps(data, ensure_ascii=False, indent=2) + '\n'


def main():
    check = '--check' in sys.argv
    source = json.loads((ROOT / 'dev/i18n/controls-l10n.json').read_text(encoding='utf-8'))['strings']
    pending = 0
    for lang in LANGS:
        path = CATALOGS / f'{lang}.json'
        raw = path.read_bytes().decode('utf-8')
        newline = '\r\n' if '\r\n' in raw else '\n'  # keep each catalog's own line endings
        text = raw.replace('\r\n', '\n')
        catalog = json.loads(text)
        assert dump(catalog) == text, f'{path.name}: unexpected formatting; refusing to rewrite'
        strings = catalog['strings']
        added = []
        for key, values in source.items():
            if key in strings:
                continue
            value = key if lang == 'en' else values.get(lang)
            if value is None:
                raise SystemExit(f'{lang}: no translation for {key!r}')
            strings[key] = value
            added.append(key)
        pending += len(added)
        print(f'{lang}: {len(added)} added')
        if added and not check:
            path.write_bytes(dump(catalog).replace('\n', newline).encode('utf-8'))
    if check and pending:
        sys.exit(1)


if __name__ == '__main__':
    main()
