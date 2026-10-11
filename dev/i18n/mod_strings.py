#!/usr/bin/env python3
"""The VR mod's translations: find every English string the plugin shows, and
check that each of the ten catalogs translates all of them.

    python dev/i18n/mod_strings.py extract          rewrite en.json and contexts.json from the sources
    python dev/i18n/mod_strings.py check            exit 1 on anything missing or broken (release gate)
    python dev/i18n/mod_strings.py todo <lang>      the strings <lang> still needs, as JSON on stdout
    python dev/i18n/mod_strings.py merge <lang> <file.json>   add checked translations to <lang>.json

Catalogs: mod/localization/wuwa/<lang>.json, keyed by the English text. The
plugin embeds them (dev/bundle-languages.py); check also fails if that copy is stale.

What counts as shown (mod/uevr/src, WuWa-owned code):
  * the text argument of wuwa_ui:: calls (TextWrapped, Button, TreeNode, draw...),
    of wuwa_l10n::text / label / translate and of tr();
  * VR menu rows: setting_toggle / setting_slider / setting_choice / action /
    paragraph / menu_angle_slider / make, their choice lists, and Page{"name"};
  * the controls cards: header / rich / fit / pill / centered / draw_tip / draw_note;
  * anything wrapped in N_("...") (WuWaLocalization.hpp), for English kept in
    tables and drawn later;
  * the options of ModCombo settings drawn with wuwa_ui::draw.
Raw ImGui text calls with English in WuWa-owned files fail the check: route
them through wuwa_ui:: so they are translated.

A translation must keep the English printf formats (%d, %.1f...) in order, the
same {L3}-style button tokens and the same number of line breaks.
"""
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / 'mod/uevr/src'
CATALOGS = ROOT / 'mod/localization/wuwa'
LANGUAGES = ['zh-Hans', 'ja', 'ko', 'de', 'es', 'fr', 'pt-BR', 'ru', 'ar']

# Function name -> argument positions that hold shown English.
CALLS = {
    'tr': [0], 'text': [0], 'label': [0], 'translate': [0],
    'TextWrapped': [0], 'Text': [0], 'TextColored': [1], 'TextUnformatted': [0], 'Button': [0],
    'TreeNode': [0], 'CollapsingHeader': [0], 'TableSetupColumn': [0], 'Combo': [0], 'draw': [1],
    'setting_toggle': [0, 1], 'setting_slider': [0, 1], 'setting_int_slider': [0, 1], 'setting_choice': [0, 1, 3],
    'action': [0, 1], 'paragraph': [0], 'menu_angle_slider': [0, 1], 'make': [1, 2],
    'header': [1, 2], 'rich': [5], 'pieces': [0], 'fit': [1], 'pill': [4], 'pill_width': [1], 'centered': [5],
    'draw_tip': [3, 4], 'draw_note': [3, 4, 5], 'N_': [0],
}
# These names are only translating calls when qualified (wuwa_ui::draw, wuwa_l10n::text...).
QUALIFIED = {'text': 'wuwa_l10n', 'label': 'wuwa_l10n', 'translate': 'wuwa_l10n', 'draw': 'wuwa_ui',
             'TextWrapped': 'wuwa_ui', 'Text': 'wuwa_ui', 'TextColored': 'wuwa_ui', 'TextUnformatted': 'wuwa_ui',
             'Button': 'wuwa_ui', 'TreeNode': 'wuwa_ui', 'CollapsingHeader': 'wuwa_ui', 'TableSetupColumn': 'wuwa_ui',
             'Combo': 'wuwa_ui'}
RAW_IMGUI = re.compile(r'ImGui::(Text|TextWrapped|TextUnformatted|Button|TreeNode|CollapsingHeader|Checkbox|SliderFloat|Combo)\s*\(\s*"([^"]*)"')
CONFIG_KEY = re.compile(r'^[A-Za-z0-9]+(_[A-Za-z0-9]+)+$')
FORMAT = re.compile(r'%[-+ #0]*\d*(?:\.\d+)?[hljztL]*[diuoxXfFeEgGaAcsp]')


def formats(value):
    """printf conversions as the plugin parses them (WuWaLocalization.cpp formats());
    None for a stray %, which makes the plugin reject every translation of the string."""
    out, i = [], 0
    while i < len(value):
        if value[i] != '%':
            i += 1
            continue
        i += 1
        if i >= len(value):
            return None
        if value[i] == '%':
            i += 1
            continue
        while i < len(value) and value[i] in '-+ #0.123456789':
            i += 1
        start = i
        while i < len(value) and value[i] in 'hljztL':
            i += 1
        if i == len(value) or value[i] not in 'diuoxXfFeEgGaAcsp':
            return None
        out.append(value[start:i + 1])
        i += 1
    return out
TOKEN = re.compile(r'\{[A-Za-z0-9]+\}')


def tokens(source):
    """C++ tokens: ('id', name), ('str', text), ('p', char). Comments dropped."""
    out, i, n = [], 0, len(source)
    while i < n:
        c = source[i]
        if source.startswith('//', i):
            i = source.find('\n', i)
            i = n if i < 0 else i
        elif source.startswith('/*', i):
            i = source.find('*/', i + 2)
            i = n if i < 0 else i + 2
        elif c == 'R' and source.startswith('R"', i):
            close = source.index('(', i)
            delim = ')' + source[i + 2:close] + '"'
            end = source.index(delim, close)
            out.append(('str', source[close + 1:end]))
            i = end + len(delim)
        elif c == '"':
            raw, i = bytearray(), i + 1
            while source[i] != '"':
                if source[i] == '\\':
                    e = source[i + 1]
                    if e == 'x':
                        j = i + 2
                        while j < n and source[j] in '0123456789abcdefABCDEF':
                            j += 1
                        raw.append(int(source[i + 2:j], 16) & 0xFF)
                        i = j
                        continue
                    raw += {'n': b'\n', 't': b'\t', '0': b'\0'}.get(e, e.encode('utf-8'))
                    i += 2
                else:
                    raw += source[i].encode('utf-8')
                    i += 1
            i += 1
            text = raw.decode('utf-8', errors='replace')
            if out and out[-1][0] == 'str':
                out[-1] = ('str', out[-1][1] + text)  # "a" "b" is one literal
            else:
                out.append(('str', text))
        elif c == "'":
            j = i + 1
            while source[j] != "'":
                j += 2 if source[j] == '\\' else 1
            i = j + 1
        elif c.isalpha() or c == '_':
            j = i
            while j < n and (source[j].isalnum() or source[j] == '_'):
                j += 1
            out.append(('id', source[i:j]))
            i = j
        elif c.isspace():
            i += 1
        else:
            out.append(('p', c))
            i += 1
    return out


def arguments(toks, start):
    """Argument token lists of the call whose '(' or '{' is at start."""
    opener = toks[start][1]
    closer = ')' if opener == '(' else '}'
    args, current, depth, i = [], [], 0, start + 1
    while i < len(toks):
        kind, value = toks[i]
        if kind == 'p' and value in '([{':
            depth += 1
        elif kind == 'p' and value in ')]}':
            if depth == 0 and value == closer:
                args.append(current)
                return args
            depth -= 1
        elif kind == 'p' and value == ',' and depth == 0:
            args.append(current)
            current = []
            i += 1
            continue
        current.append(toks[i])
        i += 1
    return args


def shown(text):
    return bool(re.search(r'[A-Za-z]', FORMAT.sub('', text))) and not CONFIG_KEY.match(text) and not TOKEN.fullmatch(text)


def literals():
    """Every string literal in the mod's sources: catalog entries for text built
    at run time (status messages shown through "%s") stay while their literal exists."""
    seen = set()
    for path in SRC.rglob('*'):
        if path.suffix in ('.cpp', '.hpp'):
            seen.update(t for kind, t in tokens(path.read_text(encoding='utf-8', errors='replace')) if kind == 'str')
    for path in (ROOT / 'mod/lua').glob('*.lua'):
        for m in re.finditer(r'"((?:[^"\\\n]|\\.)*)"', path.read_text(encoding='utf-8', errors='replace')):
            seen.add(m.group(1))
    return seen


DEFINITION = re.compile(r'^[ \t]*(?:static[ \t]+|inline[ \t]+)*[\w:<>]+[\w:<>\*& \t]*?\b(\w+)\s*\(([^()]*)\)\s*(?:const\s*)?\{', re.M)


def signature_problems(rel, source):
    """CALLS must match the helpers it names: every listed position is a string
    parameter, and a parameter named `english` (the convention for text a helper
    translates) is listed. A wrong position silently drops a call's strings."""
    problems = []
    for m in DEFINITION.finditer(source):
        name = m.group(1)
        if name in QUALIFIED or name in ('if', 'for', 'while', 'switch', 'catch', 'return'):
            continue
        params = [p.split('=')[0].strip() for p in m.group(2).split(',') if p.strip()]
        texts = [i for i, p in enumerate(params) if re.search(r'\b(string|char)\b', p)]
        english = [i for i, p in enumerate(params) if re.findall(r'\w+', p)[-1:] == ['english']]
        line = source.count('\n', 0, m.start()) + 1
        listed = CALLS.get(name)
        if listed is None:
            if english:
                problems.append(f'{rel}:{line}: {name}() takes `english` but is not in CALLS; add \'{name}\': {english}')
            continue
        wrong = [i for i in listed if i not in texts]
        unlisted = [i for i in english if i not in listed]
        if wrong or unlisted:
            problems.append(f'{rel}:{line}: CALLS[\'{name}\'] = {listed} does not match {name}({", ".join(params)}); '
                            f'text parameters are at {texts}')
    return problems


def extract():
    """{english: [files]} and raw-ImGui problems."""
    found, problems = {}, []
    files = sorted(p for p in SRC.rglob('*') if p.suffix in ('.cpp', '.hpp'))
    combos, drawn = {}, set()
    for path in files:
        source = path.read_text(encoding='utf-8', errors='replace')
        rel = path.relative_to(SRC).as_posix()
        if not any(m in source for m in ('wuwa_ui::', 'wuwa_l10n::', 'N_(', 'wuwa_menu', 'wuwa_sheet', 'ModCombo::create')):
            continue
        owned = Path(rel).name.startswith('WuWa')
        if owned:
            for m in RAW_IMGUI.finditer(source):
                if shown(m.group(2)):
                    problems.append(f'{rel}: untranslated ImGui::{m.group(1)}("{m.group(2)[:50]}"); use wuwa_ui::{m.group(1)}')
            problems += signature_problems(rel, source)
        toks = tokens(source)
        add = lambda text: found.setdefault(text, set()).add(rel) if shown(text) else None
        for i, (kind, value) in enumerate(toks):
            if kind != 'id' or i + 1 >= len(toks):
                continue
            nxt = toks[i + 1]
            if value == 'Page' and (nxt == ('p', '{') or (nxt[0] == 'id' and i + 2 < len(toks) and toks[i + 2] == ('p', '{'))):
                at = i + 1 if nxt == ('p', '{') else i + 2
                args = arguments(toks, at)
                if args and args[0] and args[0][0][0] == 'str':
                    add(args[0][0][1])
                continue
            if value == 'create' and nxt == ('p', '(') and i >= 3 and toks[i - 3] == ('id', 'ModCombo'):
                # const ModCombo::Ptr m_name{ModCombo::create("Key", {options}, default)}
                member = next((toks[j][1] for j in range(i - 4, max(i - 12, 0), -1) if toks[j][0] == 'id' and toks[j][1].startswith('m_')), None)
                args = arguments(toks, i + 1)
                if member and len(args) > 1:
                    combos.setdefault(member, []).append(([t[1] for t in args[1] if t[0] == 'str'], rel))
                continue
            qualifier = toks[i - 3][1] if i >= 3 and toks[i - 2] == ('p', ':') and toks[i - 1] == ('p', ':') else None
            if value == 'draw' and qualifier == 'wuwa_ui' and nxt == ('p', '('):
                args = arguments(toks, i + 1)
                if args and len(args[0]) >= 2 and args[0][0] == ('p', '*') and args[0][1][0] == 'id':
                    drawn.add(args[0][1][1])
            if value not in CALLS or nxt != ('p', '('):
                continue
            if value in QUALIFIED and qualifier != QUALIFIED[value]:
                continue
            args = arguments(toks, i + 1)
            for position in CALLS[value]:
                if position < len(args):
                    for kind2, text in args[position]:
                        if kind2 == 'str':
                            add(text)
    for member in drawn:
        for options, rel in combos.get(member, []):
            for text in options:
                if shown(text):
                    found.setdefault(text, set()).add(rel)
    # The website's controls card and tables (dev/controls-section.cjs) read these
    # from the same catalogs; key strings need words only when they hold words.
    controls = json.loads((ROOT / 'mod/controls.json').read_text(encoding='utf-8'))
    button = r'(?<![A-Za-z])(L3|R3|LB|RB|LT|RT|View|Menu|F7|A|B|X|Y)(?![A-Za-z])'
    found.setdefault('D-pad', set()).add('mod/controls.json')
    for group in controls['groups']:
        found.setdefault(group['title'], set()).add('mod/controls.json')
        for row in group['rows']:
            found.setdefault(row['action'], set()).add('mod/controls.json')
            words = re.sub(button, '', row['keys']).replace('0.8 s', '')
            if re.search(r'[a-z]{2,}', words) and not re.fullmatch(r'Double (L3|R3)', row['keys']):
                found.setdefault(row['keys'], set()).add('mod/controls.json')
    return {k: sorted(v) for k, v in sorted(found.items())}, problems


def catalog(code):
    return json.loads((CATALOGS / f'{code}.json').read_text(encoding='utf-8'))


def write_catalog(code, data):
    write_json(CATALOGS / f'{code}.json', data)


def write_json(path, data):
    # Keep the file's own line endings (the catalogs differ), LF for a new file.
    newline = '\r\n' if path.exists() and b'\r\n' in path.read_bytes() else '\n'
    with open(path, 'w', encoding='utf-8', newline=newline) as out:
        out.write(json.dumps(data, ensure_ascii=False, indent=2) + '\n')


def translation_problem(english, translated):
    if not isinstance(translated, str) or not translated.strip():
        return 'empty'
    if '##' in translated or '\0' in translated:
        return 'contains ## or NUL'
    if formats(english) is None:
        return 'the English has a stray % (write %% or rephrase); the plugin would reject every translation'
    if formats(english) != formats(translated):
        return f'formats {formats(english)} became {formats(translated)}'
    if sorted(TOKEN.findall(english)) != sorted(TOKEN.findall(translated)):
        return f'button tokens {sorted(TOKEN.findall(english))} became {sorted(TOKEN.findall(translated))}'
    if english.count('\n') != translated.count('\n'):
        return 'line breaks differ'
    return None


def check():
    strings, problems = extract()
    en = catalog('en')['strings']
    for text, files in strings.items():
        if formats(text) is None:
            problems.append(f'en: "{text[:70]}" ({files[0]}) has a stray %: write %% or rephrase, or no translation can load')
        if text not in en:
            problems.append(f'en: "{text[:70]}" ({files[0]}) is shown but not in en.json; run: python dev/i18n/mod_strings.py extract')
    for code in LANGUAGES:
        data = catalog(code)['strings']
        missing = [k for k in en if k not in data]
        if missing:
            problems.append(f'{code}: {len(missing)} of {len(en)} strings untranslated, e.g. "{missing[0][:60]}"; '
                            f'run: python dev/i18n/mod_strings.py todo {code}')
        for k, v in data.items():
            if k not in en:
                problems.append(f'{code}: "{k[:60]}" is not an English source string (stale?)')
                continue
            issue = translation_problem(k, v)
            if issue:
                problems.append(f'{code}: "{k[:60]}": {issue}')
    bundle = subprocess.run([sys.executable, str(ROOT / 'dev/bundle-languages.py'), '--check'], capture_output=True, text=True)
    if bundle.returncode:
        problems.append('mod/uevr/src/assets/wuwa-languages.json is out of date; run: python dev/bundle-languages.py')
    alive = literals()
    dead = [k for k in en if k not in strings and k not in alive]
    if dead:
        problems.append(f'en: {len(dead)} strings are no longer in the sources, e.g. "{dead[0][:60]}"; '
                        'run: python dev/i18n/mod_strings.py extract')
    print('\n'.join(problems) if problems else f'OK: {len(en)} strings, {len(LANGUAGES) + 1} languages, all translated')
    return 1 if problems else 0


def main():
    command = sys.argv[1] if len(sys.argv) > 1 else 'check'
    if command == 'extract':
        strings, problems = extract()
        en = catalog('en')
        before = set(en['strings'])
        alive = literals()
        dead = sorted(k for k in before if k not in strings and k not in alive)
        en['strings'] = {k: k for k in sorted((before - set(dead)) | set(strings))}
        write_catalog('en', en)
        for code in LANGUAGES:
            data = catalog(code)
            data['strings'] = {k: v for k, v in data['strings'].items() if k in en['strings']}
            write_catalog(code, data)
        print(f'removed {len(dead)} strings no longer in the sources')
        contexts = {k: strings.get(k, []) for k in sorted(en['strings']) if k in strings}
        write_json(CATALOGS / 'contexts.json', contexts)
        added = sorted(set(strings) - before)
        print(f'en.json: {len(en["strings"])} strings ({len(added)} new)')
        for k in added:
            print('  +', k[:100])
        for p in problems:
            print(p)
        return 0
    if command == 'check':
        return check()
    if command == 'todo':
        code = sys.argv[2]
        en, data = catalog('en')['strings'], catalog(code)['strings']
        todo = {k: '' for k in en if k not in data}
        print(json.dumps(todo, ensure_ascii=False, indent=2))
        return 0
    if command == 'merge':
        code, path = sys.argv[2], Path(sys.argv[3])
        incoming = json.loads(path.read_text(encoding='utf-8'))
        data = catalog(code)
        en = catalog('en')['strings']
        rejected = 0
        for k, v in incoming.items():
            issue = 'not an English source string' if k not in en else translation_problem(k, v)
            if issue:
                rejected += 1
                print(f'rejected "{k[:60]}": {issue}')
                continue
            data['strings'][k] = v
        data['strings'] = {k: data['strings'][k] for k in en if k in data['strings']}
        write_catalog(code, data)
        print(f'{code}: {len(data["strings"])} of {len(en)} translated ({rejected} rejected)')
        return 1 if rejected else 0
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main())
