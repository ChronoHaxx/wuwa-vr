#!/usr/bin/env python3
"""Controller smoke test for WuWa VR's screen UI, through the uevr-mcp HTTP API (127.0.0.1:8899).

Drives a fixed controller route (Terminal pages and shortcuts, Quests, Journey Log, a chapter, the
cursor in each corner, B back to the world) and records, per press, how much the screen changed
(mean absolute difference of 800 px backbuffer captures). In the chapter view it checks that the
LGUI pointer ray lands under the pointer. With --ab it runs the route twice in one session: with the
hidden VR_WuWaUiWindowSize off (1.2.2's eye-shaped full VR UI, the baseline) and on (the candidate),
and flags every press that reacts in one and not the other. A press that only fails in the candidate
is a regression, not flaky input. Only presses marked portable are compared: the d-pad route and the
A presses depend on the layout (the eye-shaped Terminal fits on one page, so RB/LB do nothing there).
Without --ab the route runs --repeat times (default 2) on the current build and every judged press
must react in every run; one-off game overlays (a "Connection error" reconnect) then show up as a run
that disagrees. Each run writes <tag>-sheet.jpg: look at it before trusting the numbers.

Each press is judged by LGUI's event system (its focused/selected item changes when a screen opens,
closes or moves focus) and by a screen change above the noise of two input-free captures. The game
window must stay in the foreground: UE ignores the pad otherwise, and the script stops if it is not.

Start in the open world, full VR, no menu open, controller untouched. Changes only VR_WuWaUiWindowSize
and VR_2DScreenMode (a view round trip makes the HUD check lay out again) and restores both; never
saves the UEVR config.
    python dev/ui-controller-smoke.py OUT_DIR [--ab] [--repeat N]      (Python with Pillow)
"""
import base64
import ctypes
import ctypes.wintypes
import io
import json
import sys
import time
import urllib.request
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageStat

API = 'http://127.0.0.1:8899'


def get(path):
    with urllib.request.urlopen(API + path, timeout=15) as r:
        return json.loads(r.read())


def post(path, body):
    req = urllib.request.Request(API + path, data=json.dumps(body).encode(), method='POST',
                                 headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=15) as r:
        return json.loads(r.read() or b'{}')


def game_in_front():
    user32, kernel32 = ctypes.windll.user32, ctypes.windll.kernel32
    pid = ctypes.wintypes.DWORD()
    user32.GetWindowThreadProcessId(user32.GetForegroundWindow(), ctypes.byref(pid))
    handle = kernel32.OpenProcess(0x1000, False, pid.value)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not handle:
        return False
    try:
        size = ctypes.wintypes.DWORD(1024)
        name = ctypes.create_unicode_buffer(1024)
        ok = kernel32.QueryFullProcessImageNameW(handle, 0, name, ctypes.byref(size))
        return bool(ok) and name.value.lower().endswith('client-win64-shipping.exe')
    finally:
        kernel32.CloseHandle(handle)


def require_front():
    if not game_in_front():
        raise SystemExit('Game window is not in front: click into the game, then leave the PC alone and rerun.')


def event_system():
    for o in objects('/Script/LGUI.LGUIEventSystem'):
        if 'PersistentLevel' in o['fullName']:
            return o['address']
    return None


def ui_focus(system):
    """Addresses of the event system's navigation/selected/pre-selected items: changes on any UI reaction."""
    f = fields(system)
    return tuple((f.get(k) or {}).get('address') for k in ('navigationComponent', 'selectedComponent', 'preSelectedComponent'))


def setting(key, value):
    return post('/api/vr/settings', {'key': key, 'value': str(value)})


def shot(width=800):
    d = get(f'/api/screenshot?maxWidth={width}')
    return Image.open(io.BytesIO(base64.b64decode(d.get('data') or d['result']['data']))).convert('RGB')


def pad(seconds=0.2, **state):
    post('/api/input/gamepad', dict(state, duration=seconds))
    time.sleep(seconds)


def objects(cls):
    return get('/api/explorer/objects_by_class?className=' + cls).get('results', [])


def fields(address):
    d = get(f'/api/explorer/object?address={address}&depth=1')
    return d.get('fields', d)


def method(address, name, args=None):
    return post('/api/explorer/method', {'address': address, 'methodName': name, 'args': args or {}}).get('result')


def ui_sizes():
    """Root canvas viewport and the scaler's cached viewport (+0x138), as (w, h) pairs."""
    scaler = objects('/Script/LGUI.LGUICanvasScaler')[0]['address']
    canvas = fields(scaler)['Canvas']['address']
    vp = method(canvas, 'GetViewportSize')
    dump = get(f'/api/explorer/memory?address={int(scaler, 16) + 0x138:#x}&size=8')['dump']
    raw = bytes.fromhex(''.join(dump.split('|')[0].split()[1:]))
    return (vp['X'], vp['Y']), (int.from_bytes(raw[:4], 'little'), int.from_bytes(raw[4:8], 'little'))


def mouse_pointer():
    for o in objects('/Script/LGUI.LGUIPointerEventData'):
        f = fields(o['address'])
        if f.get('pointerID') == 0:
            return o['address']
    return None


def ray_error(pointer, canvas_w, canvas_h):
    """Pointer (viewport px) against the ray hit on the canvas plane, in canvas px; None without a ray."""
    f = fields(pointer)
    p, w, o, r = f['pointerPosition'], f['worldPoint'], f['rayOrigin'], f['rayDirection']
    if not r['y']:
        return p, None
    t = 1 / abs(r['y'])  # the ray starts one unit in front of the camera, on the canvas axis
    hit = (w['x'] - (o['x'] - r['x'] * t), w['z'] - (o['z'] - r['z'] * t))
    want = (p['x'] - canvas_w / 2, canvas_h / 2 - p['y'])
    return p, max(abs(hit[0] - want[0]), abs(hit[1] - want[1]))


def relayout():
    setting('VR_2DScreenMode', 'true'); time.sleep(2.5)
    setting('VR_2DScreenMode', 'false'); time.sleep(4.0)


# (label, press, wait seconds, kind, portable): kind 'big' = a screen opens or closes, 'change' = a page
# or focus moves, None = not judged; portable = means the same in every layout (compared by --ab).
# Presses are gamepad state dicts for /api/input/gamepad.
ROUTE = [
    ('stick: controller mode', {'leftStick': {'x': 0.3, 'y': 0}}, 1.0, None, False),
    ('Start: Terminal', {'buttons': {'start': True}}, 2.5, 'big', True),
    ('RB: page 2', {'buttons': {'rb': True}}, 1.5, 'change', False),
    ('LB: page 1', {'buttons': {'lb': True}}, 1.5, 'change', False),
    ('X: mail', {'buttons': {'x': True}}, 2.5, 'big', True),
    ('B: back from mail', {'buttons': {'b': True}}, 2.0, 'big', True),
    ('RB: page 2 again', {'buttons': {'rb': True}}, 1.5, 'change', False),
    ('d-pad down', {'buttons': {'dpadDown': True}}, 1.0, 'change', False),
    ('d-pad right', {'buttons': {'dpadRight': True}}, 1.0, 'change', False),
    ('d-pad right', {'buttons': {'dpadRight': True}}, 1.0, 'change', False),
    ('d-pad right', {'buttons': {'dpadRight': True}}, 1.0, 'change', False),
    ('A: open focused (Quests)', {'buttons': {'a': True}}, 2.5, 'big', False),
    ('Y: Journey Log', {'buttons': {'y': True}}, 2.5, 'big', False),
    ('A: open chapter', {'buttons': {'a': True}}, 2.5, 'big', False),
    ('CURSOR', None, 0, None, False),
    ('B: chapter -> Journey Log', {'buttons': {'b': True}}, 2.5, 'big', False),
    ('B: Journey Log -> Quests', {'buttons': {'b': True}}, 2.5, 'big', False),
    ('B: quest list -> tabs', {'buttons': {'b': True}}, 1.5, None, False),
    ('B: Quests -> Terminal', {'buttons': {'b': True}}, 2.5, 'big', False),
    ('B: Terminal -> world', {'buttons': {'b': True}}, 2.5, 'big', False),
]
NOISE_FACTOR = 2.5  # a press "changes the screen" when the change exceeds 2.5x the input-free noise


def diff(a, b):
    return sum(ImageStat.Stat(ImageChops.difference(a, b).convert('L')).mean)


def cursor_corners(out, tag, canvas):
    pointer = mouse_pointer()
    rows, tiles = [], []
    for name, x, y in (('top-left', -1, 1), ('top-right', 1, 1), ('bottom-right', 1, -1), ('bottom-left', -1, -1)):
        pad(2.5, leftStick={'x': x, 'y': y}); pad(0.25, leftStick={'x': -x * 0.35, 'y': -y * 0.35}); time.sleep(0.4)
        p, err = ray_error(pointer, *canvas) if pointer else ({'x': 0, 'y': 0}, None)
        im = shot(1600)
        sx, sy = p['x'] * im.width / canvas[0], p['y'] * im.height / canvas[1]
        d = ImageDraw.Draw(im)
        d.line((sx - 40, sy, sx + 40, sy), fill=(255, 0, 255), width=2)
        d.line((sx, sy - 40, sx, sy + 40), fill=(255, 0, 255), width=2)
        left = int(max(0, min(im.width - 300, sx - 150))); top = int(max(0, min(im.height - 200, sy - 100)))
        tiles.append(im.crop((left, top, left + 300, top + 200)))
        rows.append({'corner': name, 'pointer': [round(p['x']), round(p['y'])],
                     'ray_error_px': None if err is None else round(err, 2)})
    sheet = Image.new('RGB', (1200, 200))
    for i, t in enumerate(tiles):
        sheet.paste(t, (i * 300, 0))
    sheet.save(out / f'{tag}-cursor.png')
    return rows


def run(out, tag):
    canvas, cache = ui_sizes()
    system = event_system()
    results = {'canvas': canvas, 'scaler_cache': cache, 'steps': [], 'cursor': None}
    for i, (label, press, wait, kind, portable) in enumerate(ROUTE):
        require_front()
        if label == 'CURSOR':
            results['cursor'] = cursor_corners(out, tag, canvas)
            continue
        # Noise floor: two captures without input, just before the press.
        a = shot(); time.sleep(0.3); before = shot()
        noise = max(diff(a, before), 0.05)
        focus_before = ui_focus(system) if system else None
        pad(0.2, **press); time.sleep(wait)
        require_front()
        after = shot()
        after.save(out / f'{tag}-{i:02d}.jpg')
        change = diff(before, after)
        focus_changed = bool(system) and ui_focus(system) != focus_before
        reacted = focus_changed or change > NOISE_FACTOR * noise
        ok = None if kind is None else reacted
        results['steps'].append({'step': label, 'change': round(change, 2), 'noise': round(noise, 2),
                                 'focus_changed': focus_changed, 'kind': kind, 'ok': ok, 'portable': portable})
        verdict = '' if ok is None else ('ok' if ok else 'NO EFFECT')
        print(f'  [{tag}] {label:28s} change {change:6.2f} noise {noise:5.2f} focus {"moved" if focus_changed else "same "} {verdict}', flush=True)
    frames = sorted(out.glob(f'{tag}-[0-9][0-9].jpg'))
    sheet = Image.new('RGB', (320 * 5, 180 * ((len(frames) + 4) // 5)), (30, 30, 30))
    for k, f in enumerate(frames):
        im = Image.open(f).convert('RGB').resize((320, 180))
        ImageDraw.Draw(im).text((4, 4), f.stem.split('-')[-1], fill=(255, 0, 0))
        sheet.paste(im, ((k % 5) * 320, (k // 5) * 180))
    sheet.save(out / f'{tag}-sheet.jpg')
    return results


def main():
    out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
    ab = '--ab' in sys.argv
    require_front()
    report = {'started': time.strftime('%Y-%m-%d %H:%M:%S'), 'runs': {}}
    try:
        repeat = int(sys.argv[sys.argv.index('--repeat') + 1]) if '--repeat' in sys.argv else 2
        modes = [('baseline', 'false'), ('candidate', 'true')] if ab else [(f'run{n + 1}', None) for n in range(repeat)]
        for tag, value in modes:
            if value is not None:
                setting('VR_WuWaUiWindowSize', value); relayout()
            canvas, cache = ui_sizes()
            print(f'{tag}: canvas viewport {canvas}, scaler cache {cache}', flush=True)
            # The baseline must really be the eye-shaped UI and the candidate the window size,
            # with the scaler laid out for it; otherwise the comparison proves nothing.
            if cache != canvas:
                raise SystemExit(f'{tag}: scaler cache {cache} does not match the canvas {canvas}; layout stale')
            if value == 'false' and canvas[0] * 9 == canvas[1] * 16:
                raise SystemExit(f'{tag}: canvas is still 16:9 {canvas}; VR_WuWaUiWindowSize did not take effect')
            if value == 'true' and canvas[0] * 9 != canvas[1] * 16:
                raise SystemExit(f'{tag}: canvas {canvas} is not the 16:9 window size')
            report['runs'][tag] = run(out, tag)
    finally:
        if ab:
            setting('VR_WuWaUiWindowSize', 'true'); relayout()
    if ab:
        base, cand = report['runs']['baseline']['steps'], report['runs']['candidate']['steps']
        report['regressions'] = [c['step'] for b, c in zip(base, cand) if c['portable'] and b['ok'] and c['ok'] is False]
        report['fixed'] = [c['step'] for b, c in zip(base, cand) if c['portable'] and b['ok'] is False and c['ok']]
    for tag, r in report['runs'].items():
        bad = [s['step'] for s in r['steps'] if s['ok'] is False]
        errs = [c['ray_error_px'] for c in r['cursor'] or [] if c['ray_error_px'] is not None]
        print(f'{tag}: {len(bad)} presses without effect {bad}; cursor ray error max '
              f'{max(errs) if errs else "n/a"} px over {len(errs)} corners')
    if ab:
        print('regressions (work in baseline, not in candidate):', report['regressions'] or 'none')
    (out / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
