#!/usr/bin/env python3
"""Stick-cursor check for WuWa VR's screen UI, through the uevr-mcp HTTP API (127.0.0.1:8899).

Screens with a left-stick cursor (journey log chapters, the map) are the game's script: it moves
LGUI's pointer in canvas pixels and draws its ring there, and the ray must read those pixels with
the same rect. Test 17m's ray read window pixels in the taller layout and A missed the ring; the
older controller smoke test only compared the ray with the pointer, so it could not see that.

This drives the left stick through a loop (gamepad only, never the mouse) and after each pulse
measures three positions in canvas pixels: the pointer LGUI holds, the ring the game draws (found
in a backbuffer shot) and where the pointer's ray meets the canvas. Ring vs pointer catches a stale
script size (the layout or view changed while the screen was open: reopen it); ray vs pointer
catches a rect mismatch in our viewport hooks. Both must stay under --tolerance (default 12 px);
without rays for most steps the verdict is INCOMPLETE (exit 2), not PASS.

Start with such a screen open, the game in front and the controller untouched. Changes nothing.
    python dev/ui-stick-cursor.py OUT_DIR [--tolerance PX]        (Python with OpenCV and numpy)
"""
import base64
import ctypes
import ctypes.wintypes
import json
import sys
import time
import urllib.request
from pathlib import Path

import cv2
import numpy as np

API = 'http://127.0.0.1:8899'
SHOT_WIDTH = 1600


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


def objects(cls):
    return [o for o in get('/api/explorer/objects_by_class?className=' + cls).get('results', [])
            if 'Default__' not in o['fullName']]


def fields(address):
    d = get(f'/api/explorer/object?address={address}&depth=1')
    return d.get('fields', d)


def canvas_size():
    """The root canvas's viewport size (pixels the script's cursor works in, when fresh)."""
    scaler = objects('/Script/LGUI.LGUICanvasScaler')[0]['address']
    canvas = fields(scaler)['Canvas']['address']
    vp = post('/api/explorer/method', {'address': canvas, 'methodName': 'GetViewportSize', 'args': {}})['result']
    return vp['X'], vp['Y']


def window_size():
    d = get('/api/screenshot?maxWidth=64')
    d = d.get('result', d)
    return d['sourceWidth'], d['sourceHeight']


def pointer_address():
    for o in objects('/Script/LGUI.LGUIPointerEventData'):
        if fields(o['address']).get('pointerID') == 0:
            return o['address']
    raise SystemExit('no LGUI pointer 0')


def ring_template(squash):
    """A drawn stand-in for the game's cursor ring (outer ring, halo, dot) at 1600 px wide."""
    t = np.full((64, 64), 25, np.uint8)
    for radius, colour, thickness in ((25, 235, 2), (8, 110, 3), (3, 255, -1)):
        cv2.ellipse(t, (32, 32), (radius, max(1, round(radius * squash))), 0, 0, 360, colour, thickness, cv2.LINE_AA)
    return t


def find_ring(squash):
    d = get(f'/api/screenshot?maxWidth={SHOT_WIDTH}')
    img = cv2.imdecode(np.frombuffer(base64.b64decode(d.get('data') or d['result']['data']), np.uint8), cv2.IMREAD_COLOR)
    r = cv2.matchTemplate(cv2.cvtColor(img, cv2.COLOR_BGR2GRAY), ring_template(squash), cv2.TM_CCOEFF_NORMED)
    _, score, _, loc = cv2.minMaxLoc(r)
    return loc[0] + 32, loc[1] + 32, score, img


def ray_on_canvas(f, canvas):
    """Where the pointer's ray meets the canvas plane, in canvas pixels; None without a ray. As in
    ui-controller-smoke.py: the ray starts one unit in front of the camera, on the canvas axis."""
    w, o, r = f['worldPoint'], f['rayOrigin'], f['rayDirection']
    if not r['y']:
        return None
    t = 1 / abs(r['y'])
    x, z = w['x'] - (o['x'] - r['x'] * t), w['z'] - (o['z'] - r['z'] * t)
    return x + canvas[0] / 2, canvas[1] / 2 - z


def main():
    if len(sys.argv) < 2 or sys.argv[1].startswith('-'):
        sys.exit('usage: ui-stick-cursor.py OUT_DIR')
    out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
    tolerance = float(sys.argv[sys.argv.index('--tolerance') + 1]) if '--tolerance' in sys.argv else 12.0
    if not game_in_front():
        raise SystemExit('Game window is not in front: click into the game, then leave the PC alone and rerun.')
    canvas, window = canvas_size(), window_size()
    squash = (window[1] / canvas[1]) / (window[0] / canvas[0])  # the canvas as the window shows it
    to_shot = (SHOT_WIDTH / canvas[0], SHOT_WIDTH * window[1] / window[0] / canvas[1])
    pointer = pointer_address()
    print(f'canvas {canvas[0]}x{canvas[1]}, window {window[0]}x{window[1]}', flush=True)
    route = [(1, 0)] * 3 + [(0, -1)] * 2 + [(-1, 0)] * 5 + [(0, 1)] * 2 + [(1, 0)] * 2
    rows, worst_ring, worst_ray = [], 0.0, 0.0
    post('/api/input/gamepad', {'leftStick': {'x': 0.3, 'y': 0}, 'duration': 0.2}); time.sleep(0.8)
    for i, (dx, dy) in enumerate(route):
        if not game_in_front():
            raise SystemExit('Game window left the front; stopped.')
        post('/api/input/gamepad', {'leftStick': {'x': 0.6 * dx, 'y': 0.6 * dy}, 'duration': 0.35})
        time.sleep(0.95)
        f = fields(pointer)
        p = f['pointerPosition']
        x, y, score, img = find_ring(squash)
        ring = (x / to_shot[0], y / to_shot[1])
        ray = ray_on_canvas(f, canvas)
        row = {'step': i, 'input_type': (f.get('inputType') or {}).get('value'), 'pointer': [p['x'], p['y']],
               'ring': [round(ring[0], 1), round(ring[1], 1)] if score > 0.55 else None, 'ring_match': round(score, 2),
               'ray': None if ray is None else [round(ray[0], 1), round(ray[1], 1)]}
        if row['ring']:
            row['ring_error'] = round(max(abs(ring[0] - p['x']), abs(ring[1] - p['y'])), 1)
            worst_ring = max(worst_ring, row['ring_error'])
        if ray is not None:
            row['ray_error'] = round(max(abs(ray[0] - p['x']), abs(ray[1] - p['y'])), 1)
            worst_ray = max(worst_ray, row['ray_error'])
        rows.append(row)
        sx, sy = p['x'] * to_shot[0], p['y'] * to_shot[1]
        cv2.drawMarker(img, (int(sx), int(sy)), (255, 0, 255), cv2.MARKER_CROSS, 40, 2)
        cv2.imwrite(str(out / f'stick-{i:02d}.jpg'), img)
        print(f"  {i:2d} pointer ({p['x']:7.1f},{p['y']:7.1f}) ring {row['ring']} (match {score:.2f}) "
              f"ray {row['ray']} errors ring {row.get('ring_error')} ray {row.get('ray_error')}", flush=True)
    measured = [r for r in rows if r['ring']]
    rays = [r for r in rows if r['ray'] is not None]
    ok = len(measured) >= len(rows) // 2 and worst_ring <= tolerance and worst_ray <= tolerance
    # Without rays the ray check proves nothing: LGUI casts none for the pointer while the game keeps it in
    # mouse mode (15 Oct: after mouse use, injected stick input left inputType 0 until the cursor was re-centred).
    verdict = 'FAIL' if not ok else ('PASS' if len(rays) >= len(rows) // 2 else 'INCOMPLETE')
    report = {'canvas': canvas, 'window': window, 'tolerance': tolerance, 'worst_ring_error': worst_ring,
              'worst_ray_error': worst_ray, 'rings_found': len(measured), 'rays_found': len(rays), 'steps': rows,
              'verdict': verdict}
    (out / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(f"{verdict}: ring vs pointer max {worst_ring} px ({len(measured)}/{len(rows)} rings found), "
          f"ray vs pointer max {worst_ray} px ({len(rays)}/{len(rows)} rays found); frames and report.json in {out}")
    if verdict == 'INCOMPLETE':
        print('No ray for the pointer: the game kept it in mouse mode. Reopen the screen with the controller and rerun.')
    return {'PASS': 0, 'FAIL': 1, 'INCOMPLETE': 2}[verdict]


if __name__ == '__main__':
    sys.exit(main())
