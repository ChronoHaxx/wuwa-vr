#!/usr/bin/env python3
"""WuWa VR acceptance matrix (qa/matrix.json) and results (qa/results.jsonl). Read-only on the game.

    python dev/qa.py check                       validate the matrix
    python dev/qa.py plan --build ID [--since REV] [--out FILE]
                                                 checklist for a candidate: every cell of a feature whose
                                                 code changed since REV, the smoke cells, and anything that
                                                 last failed or was never checked
    python dev/qa.py record FEATURE STATE pass|fail|blocked --build ID --by WHO [--dims k=v,..] [--note TEXT]
    python dev/qa.py session LOG [--build ID] [--record] [--json]
                                                 turn a UEVR log.txt into automatic results (game build,
                                                 runtime, game-code checks, shared camera, layout resyncs,
                                                 automatic cinema events, errors)
    python dev/qa.py report                      write docs/QA-MATRIX.md
    python dev/qa.py unit [--cpp] [--only TEXT]  run every offline unit test: dev/test_*.lua (under lupa),
                                                 dev and launcher/dev test_*.py, and with --cpp the
                                                 single-file C++ tests (MSVC); exits 1 on any failure

docs/QA.md explains the process; docs/GAME-UPDATE.md uses `session` after a game update.
"""
from __future__ import annotations

import argparse
import fnmatch
import json
import re
import subprocess
import sys
from collections import Counter, defaultdict
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MATRIX = ROOT / 'qa' / 'matrix.json'
RESULTS = ROOT / 'qa' / 'results.jsonl'
REPORT = ROOT / 'docs' / 'QA-MATRIX.md'
RESULT_KINDS = ('pass', 'fail', 'blocked')
CHECK_KINDS = ('unit', 'invariant', 'simulator', 'tool', 'manual')
# Game image sizes seen for build 0x6aa96d2d (WuWaCodeCompatibility.hpp): which copy a log came from.
COPIES = {'ad21000': 'launcher', 'ad1c000': 'steam'}


def load_matrix(path: Path = MATRIX) -> dict:
    return json.loads(path.read_text(encoding='utf-8'))


def load_results(path: Path = RESULTS) -> list[dict]:
    if not path.exists():
        return []
    return [json.loads(line) for line in path.read_text(encoding='utf-8').splitlines() if line.strip()]


def validate(m: dict, root: Path = ROOT) -> list[str]:
    problems, seen = [], set()
    states, dims = m.get('states', {}), m.get('dimensions', {})
    for f in m.get('features', []):
        fid = f.get('id')
        if not fid or fid in seen:
            problems.append(f'feature id missing or duplicated: {fid!r}')
        seen.add(fid)
        for key in ('area', 'name', 'states', 'how', 'checks'):
            if not f.get(key):
                problems.append(f'{fid}: no {key}')
        for s in f.get('states', []):
            if s not in states:
                problems.append(f'{fid}: unknown state {s}')
        for d, values in f.get('dims', {}).items():
            if d not in dims or any(v not in dims[d] for v in values):
                problems.append(f'{fid}: unknown dimension {d}={values}')
        for c in f.get('checks', []):
            kind, _, target = c.partition(':')
            if kind not in CHECK_KINDS:
                problems.append(f'{fid}: unknown check {c}')
            if kind in ('unit', 'tool') and target and not (root / target).exists():
                problems.append(f'{fid}: {kind} {target} does not exist')
        for pattern in f.get('code', []):
            if not any(root.glob(pattern)):
                problems.append(f'{fid}: code pattern {pattern} matches no file')
    return problems


def cells(feature: dict) -> list[tuple[str, dict]]:
    """Every (state, dims) cell of a feature: the cartesian product of its declared dimensions."""
    combos = [{}]
    for d, values in sorted(feature.get('dims', {}).items()):
        combos = [dict(c, **{d: v}) for c in combos for v in values]
    return [(s, c) for s in feature['states'] for c in combos]


def key(feature: str, state: str, dims: dict) -> str:
    return feature + '|' + state + '|' + ','.join(f'{k}={v}' for k, v in sorted(dims.items()))


def latest(results: list[dict]) -> dict[str, dict]:
    out = {}
    for r in results:
        out[key(r['feature'], r['state'], r.get('dims', {}))] = r
    return out


def changed_files(since: str, root: Path = ROOT) -> list[str]:
    run = lambda *a: subprocess.run(['git', '-C', str(root), *a], capture_output=True, text=True, check=True).stdout
    names = set(run('diff', '--name-only', f'{since}..HEAD').split()) | set(run('diff', '--name-only').split())
    return sorted(names)


def touched(feature: dict, files: list[str]) -> list[str]:
    return [f for f in files if any(fnmatch.fnmatch(f, p) for p in feature.get('code', []))]


def plan(m: dict, results: list[dict], build: str, files: list[str] | None) -> str:
    last = latest(results)
    lines = [f'# Acceptance checklist for {build}', '',
             'Generated by `python dev/qa.py plan`. Record each cell with `python dev/qa.py record`.', '']
    for f in m['features']:
        why = []
        hit = touched(f, files) if files is not None else []
        if hit:
            why.append('changed: ' + ', '.join(hit[:3]) + (' ...' if len(hit) > 3 else ''))
        if f.get('smoke'):
            why.append('smoke')
        todo = []
        for state, dims in cells(f):
            r = last.get(key(f['id'], state, dims))
            if r and r['result'] == 'pass' and r.get('build') == build:
                continue
            if hit or f.get('smoke') or not r or r['result'] != 'pass':
                status = 'never checked' if not r else f"last {r['result']} on {r.get('build', '?')}"
                todo.append((state, dims, status))
        if not todo:
            continue
        if not why:
            why.append('open or unchecked')
        lines += [f"## {f['area']}: {f['name']} (`{f['id']}`)", '', f"Why: {'; '.join(why)}. How: {f['how']}",
                  f"Checks: {', '.join(f['checks'])}" + (f"  Open: {f['open']}" if isinstance(f.get('open'), str) else ''), '']
        for state, dims, status in todo:
            extra = ' '.join(f'{k}={v}' for k, v in sorted(dims.items()))
            lines.append(f"- [ ] {m['states'][state]}{' · ' + extra if extra else ''} ({status})")
        lines.append('')
    return '\n'.join(lines)


# ---- session log -----------------------------------------------------------------------------

LINE = re.compile(r'^\[(\d{4}-\d\d-\d\d) (\d\d:\d\d:\d\d)\.\d+\] \[[^\]]*\] \[(\w+)\] (.*)$')


def session(text: str, known_off: list[dict] = ()) -> dict:
    pinned = [re.compile(k['match']) for k in known_off]
    s = {'runtime': None, 'copy': None, 'game_build': None, 'routes': {}, 'refused': [], 'known_off': [],
         'stereo_base': Counter(), 'hud': [], 'cinema': [], 'errors': Counter(), 'first': None, 'last': None,
         'pointer_rewrites': 0, 'pointer_layout': False, 'mouse_installed': None, 'mouse_rewrites': 0}
    for raw in text.splitlines():
        m = LINE.match(raw)
        if not m:
            continue
        day, clock, level, msg = m.groups()
        s['first'] = s['first'] or f'{day} {clock}'
        s['last'] = f'{day} {clock}'
        if 'OpenXR system Name:' in msg:
            name = msg.split('OpenXR system Name:', 1)[1].strip()
            s['runtime'] = 'simulator' if 'Simulator' in name else 'headset'
            s['runtime_name'] = name
        r = re.search(r'\[WuWaStereoCode\] route=(.+?) verified_ranges=\d+ image_size=(\w+) timestamp=(\w+)', msg)
        if r:
            s['routes'][r.group(1)] = 'verified'
            s['copy'] = COPIES.get(r.group(2), f'unknown image {r.group(2)}')
            s['game_build'] = r.group(3)
        if re.search(r'unavailable|refused|code mismatch|code changed', msg) and re.search(r'\[WuWa\w*\]', msg) \
                and 'WuWaCinema' not in msg and 'HUD refresh unavailable' not in msg:
            text_ = re.sub(r'^\[[^\]]*\] ', '', msg)[:160]
            (s['known_off'] if any(k.search(msg) for k in pinned) else s['refused']).append(f'{clock} {text_}')
        v = re.search(r'\[WuWaLguiViewport\] .*pointer=(\d+)x\d+ pointer_rewrites=(\d+)', msg)
        if v:
            s['pointer_layout'] |= v.group(1) != '0'
            s['pointer_rewrites'] = max(s['pointer_rewrites'], int(v.group(2)))
        mo = re.search(r'\[WuWaLguiViewport\] .* mouse=(true|false) mouse_rewrites=(\d+)', msg)
        if mo:
            s['mouse_installed'] = mo.group(1) == 'true'
            s['mouse_rewrites'] = max(s['mouse_rewrites'], int(mo.group(2)))
        b = re.search(r'\[WuWaStereoBase\] action=(\w+) reason=(\w+)', msg)
        if b:
            s['stereo_base'][f'{b.group(1)}/{b.group(2)}'] += 1
        if 'WuWaHud: ' in msg:
            s['hud'].append(f"{clock} {msg.split('WuWaHud: ', 1)[1][:140]}")
        if 'WuWaCinema: ' in msg:
            status = msg.split('WuWaCinema: ', 1)[1].split(' [detection')[0]
            head = status.split('. ')[0]
            if not s['cinema'] or s['cinema'][-1][1] != head:
                s['cinema'].append((clock, head))
        if level in ('error', 'critical'):
            s['errors'][re.sub(r'0x[0-9a-fA-F]+|\b[0-9a-fA-F]*\d[0-9a-fA-F]*\b|\d+', '#', msg)[:120]] += 1
    s['refused'] = sorted(set(s['refused']), key=s['refused'].index)
    s['known_off'] = sorted(set(s['known_off']), key=s['known_off'].index)
    return s


def invariants(s: dict, baseline: dict | None = None) -> list[tuple[str, str, str]]:
    """(invariant, pass|fail|unknown, detail) from a parsed session; a baseline session adds what changed."""
    out = []
    out.append(('runtime', 'pass' if s['runtime'] else 'unknown', s.get('runtime_name') or 'no OpenXR system line'))
    out.append(('game-build', 'pass' if s['game_build'] else 'unknown',
                f"{s['copy']} copy, build {s['game_build']}" if s['game_build'] else 'no verified route line'))
    lost = sorted(set(baseline['routes']) - set(s['routes'])) if baseline else []
    out.append(('code-checks', 'fail' if s['refused'] else ('pass' if s['routes'] else 'unknown'),
                f"{len(s['routes'])} routes verified; refused: {len(s['refused'])}; known off: {len(s['known_off'])}"
                + (f"; verified in the baseline but not here (not used yet, or broken): {', '.join(lost)}" if lost else '')))
    base = s['stereo_base']
    bad = sum(n for k, n in base.items() if k.startswith('refused') or k == 'ignored/disabled')
    out.append(('camera-shared', 'unknown' if not base else ('fail' if bad else 'pass'),
                ', '.join(f'{k} {n}' for k, n in base.most_common()) or 'no [WuWaStereoBase] lines'))
    loops = [h for h in s['hud'] if 'keeps changing' in h]
    checks = [h for h in s['hud'] if '(screen check)' in h or '(full VR check)' in h]
    out.append(('layout-resyncs', 'fail' if loops else 'pass',
                f"{len(checks)} resyncs by the layout check; {len(loops)} 'keeps changing' stops"))
    # In the taller layout the ray takes the window size for the mouse alone (test 17o; mouse= is sites
    # bit 7). 17n logged converted mouse positions in the same fields; 17l/17m builds gave every ray the
    # window size and log no mouse= field.
    if not s['pointer_layout']:
        mouse = ('unknown', 'taller layout not used in this session')
    elif s['mouse_installed'] is False:
        mouse = ('fail', "taller layout used but the mouse's ray is off (sites bit 7) or not installed")
    elif s['mouse_rewrites']:
        mouse = ('pass', f"{s['mouse_rewrites']} mouse rays given the window size in the taller layout")
    elif s['mouse_installed'] is None and s['pointer_rewrites']:
        mouse = ('pass', f"{s['pointer_rewrites']} mouse rays given the window size in the taller layout (17l/17m build)")
    else:
        mouse = ('unknown', 'mouse not used in the taller layout')
    out.append(('mouse-ray', *mouse))
    auto = [c for c in s['cinema'] if c[1].startswith('Automatic')]
    out.append(('cinema-events', 'pass' if auto else 'unknown',
                '; '.join(f'{t} {h}' for t, h in auto[:8]) or 'no automatic switch in this session'))
    if baseline is None:
        out.append(('errors', 'unknown', f"{sum(s['errors'].values())} error lines of {len(s['errors'])} kinds (no baseline)"))
    else:
        new = [e for e in s['errors'] if e not in baseline['errors']]
        s['new_errors'] = new
        out.append(('errors', 'fail' if new else 'pass',
                    f"{len(new)} error kinds not in the baseline; {sum(s['errors'].values())} error lines in all"))
    return out


def session_text(s: dict, inv: list) -> str:
    lines = [f"Session {s['first']} - {s['last']}: {s.get('runtime_name') or '?'}; {s['copy'] or '?'} copy, game build {s['game_build'] or '?'}"]
    for name, result, detail in inv:
        lines.append(f'  {result.upper():8s} {name:15s} {detail}')
    if s['refused']:
        lines.append('  Refused routes (a game update or a regression; docs/GAME-UPDATE.md):')
        lines += [f'    {r}' for r in s['refused'][:20]]
    if s['known_off']:
        lines.append('  Known off (qa/matrix.json known_off: pinned on purpose):')
        lines += [f'    {r}' for r in s['known_off'][:10]]
    if s['cinema']:
        lines.append('  Automatic cinema status changes:')
        lines += [f'    {t} {h}' for t, h in s['cinema'][:30]]
    if s.get('new_errors'):
        lines.append('  Errors not in the baseline:')
        lines += [f"    {s['errors'][e]:4d}x {e}" for e in s['new_errors'][:12]]
    elif s['errors']:
        lines.append('  Errors (most frequent):')
        lines += [f'    {n:4d}x {e}' for e, n in s['errors'].most_common(8)]
    return '\n'.join(lines)


def session_results(m: dict, parsed: dict, inv: list, build: str, today: str) -> list[dict]:
    """Results for the `log` column: one per feature that names an invariant the session could decide."""
    by_inv = {name: (res, detail) for name, res, detail in inv}
    dims = {d: parsed[d] for d in ('copy', 'runtime') if parsed.get(d)}
    out = []
    for f in m['features']:
        names = [c.partition(':')[2] for c in f['checks'] if c.startswith('invariant:')]
        decided = [(n, *by_inv[n]) for n in names if by_inv.get(n, ('unknown',))[0] != 'unknown']
        if decided:
            res = 'fail' if any(r == 'fail' for _, r, _ in decided) else 'pass'
            out.append(dict(feature=f['id'], state='log', dims=dims, result=res, build=build, by='session log',
                            date=today, note='; '.join(f'{n}: {d}' for n, _, d in decided)))
    return out


# ---- unit tests --------------------------------------------------------------------------------

# Lua tests run under lupa (Lua 5.4) with arg = {[0] = test, [1] = the script its header names.
LUA_RUNNER = '''
import sys
from pathlib import Path
import lupa.lua54 as lupa
test, module = sys.argv[1], sys.argv[2]
lua = lupa.LuaRuntime(unpack_returned_tuples=True)
here = str(Path(module).parent).replace("\\\\", "/")
lua.execute('package.path = "' + here + '/?.lua;" .. package.path')
lua.execute('arg = {[0] = "' + test + '", [1] = "' + module + '"}')
try:
    lua.execute(Path(test).read_text(encoding="utf-8"))
except lupa.LuaError as e:
    print("LUA ERROR:", e); sys.exit(1)
'''
VCVARS = Path(r'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat')


def lua_python() -> str | None:
    """A Python with lupa: QA_LUA_PYTHON, the workspace's dev-tools/lua-venv, or this one."""
    import os
    for candidate in (os.environ.get('QA_LUA_PYTHON'), ROOT.parent / 'dev-tools/lua-venv/Scripts/python.exe', sys.executable):
        if candidate and Path(candidate).exists() and subprocess.run([str(candidate), '-c', 'import lupa.lua54'],
                                                                      capture_output=True).returncode == 0:
            return str(candidate)
    return None


def unit_tests(cpp: bool) -> list[tuple[str, list[str] | str]]:
    """(name, command) per test; a string command is a reason it cannot run here."""
    tests, lua = [], lua_python()
    for t in sorted((ROOT / 'dev').glob('test_*.lua')):
        m = re.search(r'mod/lua/[A-Za-z0-9_]+\.lua', t.read_text(encoding='utf-8', errors='replace')[:4000])
        if not m:
            tests.append((t.name, 'no mod/lua script named in its header'))
        elif not lua:
            tests.append((t.name, 'no Python with lupa (pip install lupa, or set QA_LUA_PYTHON)'))
        else:
            tests.append((t.name, [lua, '-c', LUA_RUNNER, f'dev/{t.name}', m.group(0)]))
    for t in sorted(list((ROOT / 'dev').glob('test_*.py')) + list((ROOT / 'launcher/dev').glob('test_*.py'))):
        tests.append((t.relative_to(ROOT).as_posix(), [sys.executable, str(t)]))
    if cpp:
        for t in sorted((ROOT / 'dev').glob('test_*.cpp')):
            if t.with_suffix('.ps1').exists():
                tests.append((t.name, f'built by {t.with_suffix(".ps1").name} (needs its own libraries)'))
            elif not VCVARS.exists():
                tests.append((t.name, 'MSVC not found'))
            else:
                tests.append((t.name, 'cpp'))
    return tests


def run_unit(cpp: bool, only: str | None) -> int:
    import tempfile
    env = dict(__import__('os').environ, PYTHONIOENCODING='utf-8')
    failed, skipped, passed = [], [], 0
    work = Path(tempfile.mkdtemp(prefix='wuwa-unit-'))
    for name, cmd in unit_tests(cpp):
        if only and only not in name:
            continue
        if isinstance(cmd, str) and cmd != 'cpp':
            skipped.append((name, cmd)); print(f'SKIP {name}: {cmd}'); continue
        if cmd == 'cpp':
            exe = work / (Path(name).stem + '.exe')
            script = work / (Path(name).stem + '.cmd')
            script.write_text(f'@call "{VCVARS}" >nul\r\n@cl /nologo /utf-8 /std:c++latest /EHsc /MT /Od '
                              f'"{ROOT / "dev" / name}" /Fo"{work}\\" /Fe"{exe}" >"{exe}.log" 2>&1 || exit /b 9\r\n'
                              f'@"{exe}"\r\n', encoding='utf-8')
            cmd = ['cmd', '/c', str(script)]
        r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, encoding='utf-8', errors='replace', env=env, timeout=600)
        text = (r.stdout + r.stderr).strip().splitlines()
        bad = r.returncode != 0 or any(l.startswith(('FAIL', 'LUA ERROR')) for l in text)
        missing = sorted(set(re.findall(r"ModuleNotFoundError: No module named '([\w.]+)'", '\n'.join(text))))
        if bad and missing:
            skipped.append((name, 'needs ' + ', '.join(missing)))
            print(f'SKIP {name}: this Python lacks {", ".join(missing)}')
        elif bad:
            failed.append(name)
            print(f'FAIL {name} (exit {r.returncode})')
            for l in [l for l in text if l.startswith(('FAIL', 'LUA ERROR', 'Assertion', 'Traceback', 'AssertionError'))][:6] or text[-4:]:
                print('     ' + l[:200])
        else:
            passed += 1
            print(f'pass {name}' + (f'  ({text[-1][:90]})' if text else ''))
    print(f'\n{passed} passed, {len(failed)} failed, {len(skipped)} skipped')
    return 1 if failed else 0


# ---- report ------------------------------------------------------------------------------------

def mark(rs: list) -> str:
    if any(r and r['result'] == 'fail' for r in rs):
        return '✗'
    if any(r and r['result'] == 'blocked' for r in rs):
        return '◐'
    if rs and all(r and r['result'] == 'pass' for r in rs):
        newest = max(rs, key=lambda r: r.get('date', ''))
        return f"✓ {newest.get('build', '')[-9:]} {newest.get('date', '')[5:]}"
    return '?'

def report(m: dict, results: list[dict]) -> str:
    last = latest(results)
    states = list(m['states'])
    out = ['# Acceptance matrix', '',
           'Generated from `qa/matrix.json` and `qa/results.jsonl` by `python dev/qa.py report`; do not edit by hand. '
           'Process: [QA.md](QA.md).', '',
           'Legend: ✓ passed (build, date) · ✗ failed · ◐ blocked · ? not yet checked · blank: not applicable. '
           'A cell with dimensions (game copy, runtime, input) shows the worst of them. '
           'The `log` column is the newest automatic result from `dev/qa.py session` (worst over game copy and runtime).', '']
    by_area = defaultdict(list)
    for f in m['features']:
        by_area[f['area']].append(f)
    for area, features in by_area.items():
        used = [s for s in states if any(s in f['states'] for f in features)]
        logged = any(c.startswith('invariant:') for f in features for c in f['checks'])
        head = used + (['log'] if logged else [])
        out += [f'## {area}', '', '| Feature | ' + ' | '.join(head) + ' |', '| --- |' + ' --- |' * len(head)]
        for f in features:
            row = []
            for s in used:
                if s not in f['states']:
                    row.append('')
                    continue
                row.append(mark([last.get(key(f['id'], st, d)) for st, d in cells(f) if st == s]))
            if logged:
                rs = [r for k, r in last.items() if k.startswith(f"{f['id']}|log|")]
                row.append(mark(rs) if rs else ('?' if any(c.startswith('invariant:') for c in f['checks']) else ''))
            name = f"{f['name']} (`{f['id']}`)" + (' — open' if f.get('open') else '')
            out.append(f'| {name} | ' + ' | '.join(row) + ' |')
        out.append('')
    out += ['## States', ''] + [f'- `{k}`: {v}' for k, v in m['states'].items()]
    return '\n'.join(out) + '\n'


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('check')
    p = sub.add_parser('plan'); p.add_argument('--build', required=True); p.add_argument('--since'); p.add_argument('--out')
    r = sub.add_parser('record'); r.add_argument('feature'); r.add_argument('state'); r.add_argument('result', choices=RESULT_KINDS)
    r.add_argument('--build', required=True); r.add_argument('--by', required=True); r.add_argument('--dims', default='')
    r.add_argument('--note', default=''); r.add_argument('--date', default=str(date.today()))
    s = sub.add_parser('session'); s.add_argument('log'); s.add_argument('--build'); s.add_argument('--record', action='store_true')
    s.add_argument('--baseline', help='a log from the last good session (same copy), to show what changed')
    s.add_argument('--json', action='store_true')
    sub.add_parser('report')
    u = sub.add_parser('unit'); u.add_argument('--cpp', action='store_true'); u.add_argument('--only')
    a = ap.parse_args(argv)
    if a.cmd == 'unit':
        return run_unit(a.cpp, a.only)
    m = load_matrix()
    if a.cmd == 'check':
        problems = validate(m)
        print('\n'.join(problems) if problems else f"qa/matrix.json: {len(m['features'])} features, "
              f"{sum(len(cells(f)) for f in m['features'])} cells, OK")
        return 1 if problems else 0
    if a.cmd == 'plan':
        text = plan(m, load_results(), a.build, changed_files(a.since) if a.since else None)
        Path(a.out).write_text(text, encoding='utf-8') if a.out else print(text)
        return 0
    if a.cmd == 'record':
        ids = {f['id']: f for f in m['features']}
        if a.feature not in ids or a.state not in ids[a.feature]['states']:
            print(f'unknown feature/state {a.feature}/{a.state}', file=sys.stderr)
            return 1
        dims = dict(kv.split('=', 1) for kv in a.dims.split(',') if kv)
        entry = dict(feature=a.feature, state=a.state, dims=dims, result=a.result, build=a.build, by=a.by,
                     date=a.date, note=a.note)
        with RESULTS.open('a', encoding='utf-8', newline='\n') as out:
            out.write(json.dumps(entry, ensure_ascii=False) + '\n')
        print('recorded', key(a.feature, a.state, dims), a.result)
        return 0
    if a.cmd == 'session':
        read = lambda f: session(Path(f).read_text(encoding='utf-8', errors='replace'), m.get('known_off', []))
        parsed = read(a.log)
        inv = invariants(parsed, read(a.baseline) if a.baseline else None)
        if a.json:
            print(json.dumps({'session': {k: (dict(v) if isinstance(v, Counter) else v) for k, v in parsed.items()},
                              'invariants': inv}, ensure_ascii=False, indent=1))
        else:
            print(session_text(parsed, inv))
        if a.record:
            if not a.build:
                print('--record needs --build', file=sys.stderr)
                return 1
            rows = session_results(m, parsed, inv, a.build, str(date.today()))
            with RESULTS.open('a', encoding='utf-8', newline='\n') as out:
                out.writelines(json.dumps(r, ensure_ascii=False) + '\n' for r in rows)
            print(f'recorded {len(rows)} automatic results')
        return 0
    if a.cmd == 'report':
        REPORT.write_text(report(m, load_results()), encoding='utf-8', newline='\n')
        print('wrote', REPORT.relative_to(ROOT))
        return 0
    return 1


if __name__ == '__main__':
    sys.exit(main())
