#!/usr/bin/env python3
"""Every check that player-facing text is translated, in one command. Releases
run it before packaging (it exits 1 on any problem); run it after any change to
text a player can see. dev/i18n/README.md describes the workflow.

    python dev/i18n/check_all.py
    python dev/i18n/check_all.py --accept-english   after updating all ten languages

  * the VR mod: every string shown in the headset or the settings window, and
    the website's controls card, in all ten catalogs (dev/i18n/mod_strings.py);
  * the offline player guide the launcher opens (dev/i18n/build_guide.py);
  * the launcher app's own text (launcher/native/check-locales.py);
  * the portable web tool's text (launcher/localization/launcher), checked here.
The website's home pages are checked when the site is built (dev/build-home.cjs).

The guide, launcher, web tool and home pages are keyed by id, so an edited
English sentence would leave nine stale translations that still look complete.
dev/i18n/english-snapshot.json fingerprints every English value; a value that
changed (or a new key) fails here until all ten languages are updated and
--accept-english records the new English.
"""
import hashlib
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CHECKS = [
    ('VR mod and controls card', ['dev/i18n/mod_strings.py', 'check']),
    ('Player guide', ['dev/i18n/build_guide.py', '--check']),
    ('Launcher app', ['launcher/native/check-locales.py']),
]
WEB_TOOL = ROOT / 'launcher/localization/launcher'
SNAPSHOT = ROOT / 'dev/i18n/english-snapshot.json'
ENGLISH = {
    'player guide': 'dev/i18n/guide/en.json',
    'launcher app': 'launcher/native/locales/en.json',
    'web tool': 'launcher/localization/launcher/en.json',
    'home pages': 'dev/i18n/home/en.json',
}
LANGUAGES = ['en', 'zh-Hans', 'ja', 'ko', 'de', 'es', 'fr', 'pt-BR', 'ru', 'ar']


def shape(value, path=''):
    """{path: kind} for every leaf, with list lengths, so two files can be compared."""
    if isinstance(value, dict):
        out = {}
        for k, v in value.items():
            out.update(shape(v, f'{path}.{k}'))
        return out
    if isinstance(value, list):
        out = {f'{path}[]': f'list of {len(value)}'}
        for i, v in enumerate(value):
            out.update(shape(v, f'{path}[{i}]'))
        return out
    return {path: 'empty' if value in ('', None) else type(value).__name__}


def leaves(value, path=''):
    """{path: text} for every string in a JSON value."""
    if isinstance(value, dict):
        return {k: v for key, item in value.items() for k, v in leaves(item, f'{path}.{key}').items()}
    if isinstance(value, list):
        return {k: v for i, item in enumerate(value) for k, v in leaves(item, f'{path}[{i}]').items()}
    return {path: value} if isinstance(value, str) else {}


def fingerprints():
    out = {}
    for name, rel in ENGLISH.items():
        values = leaves(json.loads((ROOT / rel).read_text(encoding='utf-8-sig')))
        out[name] = {key: hashlib.sha1(text.encode('utf-8')).hexdigest()[:12] for key, text in sorted(values.items())}
    return out


def english_problems():
    known = json.loads(SNAPSHOT.read_text(encoding='utf-8')) if SNAPSHOT.exists() else {}
    problems = []
    for name, current in fingerprints().items():
        accepted = known.get(name, {})
        for key, digest in current.items():
            if key not in accepted:
                problems.append(f'{name}: new English at {key}; translate it, then run check_all.py --accept-english')
            elif accepted[key] != digest:
                problems.append(f'{name}: English changed at {key}; update all ten languages, then run check_all.py --accept-english')
    return problems


def web_tool_problems():
    problems = []
    english = shape(json.loads((WEB_TOOL / 'en.json').read_text(encoding='utf-8')))
    for code in LANGUAGES[1:]:
        path = WEB_TOOL / f'{code}.json'
        if not path.exists():
            problems.append(f'{path.relative_to(ROOT).as_posix()}: missing')
            continue
        theirs = shape(json.loads(path.read_text(encoding='utf-8')))
        for key, kind in english.items():
            if key in ('.language', '.name', '.direction'):
                continue
            if theirs.get(key) != kind:
                problems.append(f'{code}: {key or "(root)"} is {theirs.get(key, "missing")}, English has {kind}')
        problems += [f'{code}: {key} is not in en.json (stale?)' for key in theirs if key not in english]
    return problems


def main():
    if '--accept-english' in sys.argv:
        with open(SNAPSHOT, 'w', encoding='utf-8', newline='\n') as f:
            f.write(json.dumps(fingerprints(), indent=1, sort_keys=True) + '\n')
        print(f'Recorded the current English in {SNAPSHOT.relative_to(ROOT).as_posix()}.')
    failed = 0
    for name, command in CHECKS:
        run = subprocess.run([sys.executable, *command], cwd=ROOT, capture_output=True, text=True, encoding='utf-8')
        output = (run.stdout + run.stderr).strip()
        print(f'== {name}: {"OK" if run.returncode == 0 else "FAILED"}')
        if run.returncode:
            failed += 1
            print('\n'.join('   ' + line for line in output.splitlines()[:40]))
    problems = web_tool_problems()
    print(f'== Web tool: {"OK" if not problems else "FAILED"}')
    if problems:
        failed += 1
        print('\n'.join('   ' + p for p in problems[:40]))
    changed = english_problems()
    print(f'== English since last translation: {"OK" if not changed else "CHANGED"}')
    if changed:
        failed += 1
        print('\n'.join('   ' + p for p in changed[:40]))
    total = len(CHECKS) + 2
    print('All translations complete.' if not failed else f'{failed} of {total} translation checks failed.')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
