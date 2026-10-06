"""Explicit, read-only graphics snapshot through the packaged diagnostic bridge."""
from datetime import datetime, timezone
import importlib.util
import math
from pathlib import Path
import re


def validate_snapshot(reply):
    graphics = reply.get('graphics') if isinstance(reply, dict) else None
    if not isinstance(graphics, dict) or graphics.get('schema') != 1 or graphics.get('settings_changed') is not False:
        raise ValueError('This backend does not provide a verified read-only graphics snapshot. Use the updated graphics package.')
    values = graphics.get('values')
    if not isinstance(values, dict) or not 1 <= len(values) <= 48:
        raise ValueError('Invalid graphics snapshot size.')
    clean = {}
    for name, entry in values.items():
        if not isinstance(name, str) or not re.fullmatch(r'(?:r|sg)\.[A-Za-z0-9_.]{1,100}', name):
            raise ValueError('Invalid graphics variable name.')
        if not isinstance(entry, dict) or type(entry.get('available')) is not bool:
            raise ValueError('Graphics availability was not reported.')
        if entry['available']:
            integer, number = entry.get('int'), entry.get('float')
            if type(integer) is not int or type(number) not in (int, float) or not math.isfinite(number):
                raise ValueError('Invalid graphics value; unknown is not zero.')
            clean[name] = {'available': True, 'int': integer, 'float': number}
            if entry.get('flags_available') is True and type(entry.get('flags')) is int:
                clean[name]['flags'] = entry['flags']
        else:
            error = entry.get('error', 'Unavailable')
            if not isinstance(error, str) or len(error) > 1000:
                raise ValueError('Invalid graphics error.')
            clean[name] = {'available': False, 'error': error}
    return {'schema': 1, 'values': clean, 'settings_changed': False,
            'limitations': 'Numeric snapshot only. Saved settings and startup commands may differ. '
                           'This does not identify the last writer or prove visual correctness.'}


def read_live_graphics(profile, tool_path, *, client_factory=None):
    if client_factory is None:
        spec = importlib.util.spec_from_file_location('wuwa_graphics_bridge', Path(tool_path))
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        client_factory = module.LiveTest
    client = client_factory(profile=Path(profile), capture_source='simulator')
    before = client.assert_live()
    with client.exclusive():
        try:
            reply = client.request('graphics_snapshot', timeout=8)
        except RuntimeError as error:
            if any(word in str(error).lower() for word in ('unknown op', 'unknown request', 'unsupported')):
                raise RuntimeError('This running build lacks the graphics reader. Select the updated graphics package after closing the game.') from error
            raise
        result = validate_snapshot(reply)
        after = client.assert_live()
        if before.get('pid') != after.get('pid') or after.get('pid') != client.pid:
            raise RuntimeError('Game process changed during the graphics read.')
    result.update(pid=client.pid, captured_at=datetime.now(timezone.utc).isoformat())
    return result
