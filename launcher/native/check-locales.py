"""Check every launcher locale against en.json: same keys, same {n} placeholders,
the same line breaks, and product/file names left untranslated.

    python check-locales.py
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent / 'locales'
KEEP = ['launcher.exe', 'Wuthering Waves.exe', 'Client-Win64-Shipping.exe', 'Setup.exe', 'WuWa VR Launcher.exe',
        'UEVR', 'OpenXR', 'SteamVR', 'HidHide', 'XInput', 'Reality Runner', 'Kuro Games', 'Ko-fi', 'PowerShell', 'VDXR', 'L3 / R3']
# Official local names that may stand in for a kept token.
ALIASES = {'Kuro Games': ['库洛', 'クロ', '쿠로']}
english = json.loads((ROOT / 'en.json').read_text(encoding='utf-8'))
problems = []
for path in sorted(ROOT.glob('*.json')):
    data = json.loads(path.read_text(encoding='utf-8'))
    code = path.stem
    if data.get('code') != code: problems.append(f'{code}: code field is {data.get("code")!r}')
    missing, extra = english.keys() - data.keys(), data.keys() - english.keys()
    if missing: problems.append(f'{code}: missing {sorted(missing)}')
    if extra: problems.append(f'{code}: extra {sorted(extra)}')
    for key, source in english.items():
        value = data.get(key)
        if not isinstance(value, str) or not value.strip():
            problems.append(f'{code}.{key}: empty'); continue
        if sorted(re.findall(r'\{\d\}', source)) != sorted(re.findall(r'\{\d\}', value)):
            problems.append(f'{code}.{key}: placeholders differ')
        if source.count('\n') != value.count('\n'):
            problems.append(f'{code}.{key}: line breaks differ')
        for token in KEEP:
            if token in source and token not in value and not any(a in value for a in ALIASES.get(token, [])):
                problems.append(f'{code}.{key}: lost {token!r}')
print('\n'.join(problems) if problems else f'OK: {len(list(ROOT.glob("*.json")))} locales, {len(english)} keys')
sys.exit(1 if problems else 0)
