"""Sort what the plugin recorded as drawn without a translation (wuwa-l10n-missing.txt, written by the
VR plugin while a non-English language is active) into:
  untranslated   in the English catalog, but this language has no entry (merge one);
  stale bundle   the catalog has a translation the running DLL lacked (rebuild: the language bundle is embedded);
  not extracted  a literal in the mod's sources that the extractor missed (fix mod_strings.py or wrap N_());
  run time       text built while running (values, names); fine unless it reads as a sentence.
    python dev/i18n/missing_report.py [path to wuwa-l10n-missing.txt]"""
import json, os, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).parent))
import mod_strings  # noqa: E402

path = Path(sys.argv[1]) if len(sys.argv) > 1 else \
    Path(os.environ['APPDATA']) / 'UnrealVRMod/Client-Win64-Shipping/wuwa-l10n-missing.txt'
lines = path.read_text(encoding='utf-8').splitlines()
language = lines[0].split(' ', 1)[1] if lines and lines[0].startswith('language ') else '?'
recorded = [l for l in lines[1:] if l.strip()]
en = mod_strings.catalog('en')['strings']
target = mod_strings.catalog(language)['strings'] if (mod_strings.CATALOGS / f'{language}.json').exists() else {}
literals = mod_strings.literals()
groups = {'untranslated': [], 'stale bundle': [], 'not extracted': [], 'run time': []}
for text in recorded:
    if text in en:
        groups['stale bundle' if text in target else 'untranslated'].append(text)
    elif text in literals:
        groups['not extracted'].append(text)
    else:
        groups['run time'].append(text)
print(f'{path} ({language}): {len(recorded)} recorded')
for name, rows in groups.items():
    print(f'== {name}: {len(rows)}')
    for row in rows[:200]:
        print('  ', row[:140])
sys.exit(1 if groups['untranslated'] or groups['stale bundle'] or groups['not extracted'] else 0)
