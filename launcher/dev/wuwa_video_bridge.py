"""Local, expiring recording commands from the in-game UEVR menu.

The launcher owns output paths and recorder processes. Requests cannot supply a
command, path or runtime change. This bridge never starts by importing it.
"""
from pathlib import Path
import json
import os
import re
import secrets
import time


def read_object(path, limit=16384):
    with Path(path).open('rb') as source:
        raw = source.read(limit + 1)
    if len(raw) > limit:
        raise ValueError('Recording message exceeds size limit')
    value = json.loads(raw)
    if not isinstance(value, dict):
        raise ValueError('Recording message must be an object')
    return value


def atomic_json(path, value):
    temporary = path.with_name(path.name + '.' + secrets.token_hex(4) + '.tmp')
    try:
        temporary.write_text(json.dumps(value, ensure_ascii=True), encoding='utf-8')
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


class Bridge:
    def __init__(self, profile, inspect_game, start, stop, snapshot, *, clock=None):
        self.directory = Path(profile) / 'video-control'
        self.inspect_game = inspect_game
        self.start = start
        self.stop = stop
        self.snapshot = snapshot
        self.clock = clock or (lambda: int(time.time() * 1000))
        self.session = secrets.token_hex(16)
        self.last_request = {}
        self.seen = set()

    def validate(self, request, game):
        allowed = {'version', 'session', 'id', 'pid', 'created_ms', 'expires_ms',
                   'action', 'fps', 'eye_width', 'telemetry', 'recording_id'}
        if set(request) - allowed:
            raise ValueError('Unknown recording request field')
        if type(request.get('version')) is not int or request['version'] != 1 or request.get('session') != self.session:
            raise ValueError('Recording launcher session changed')
        identity = request.get('id')
        if not isinstance(identity, str) or not re.fullmatch(r'[A-Za-z0-9-]{1,64}', identity):
            raise ValueError('Invalid recording request ID')
        if identity in self.seen:
            raise ValueError('Recording request already handled')
        if (type(request.get('pid')) is not int or request['pid'] != game['pid']
                or type(request.get('created_ms')) is not int
                or abs(request['created_ms'] - game['created_ms']) > 2):
            raise ValueError('Recording request belongs to another game process')
        expires = request.get('expires_ms')
        if type(expires) is not int or not 0 < expires - self.clock() <= 5000:
            raise ValueError('Recording request expired')
        action = request.get('action')
        if action == 'start':
            for name, choices in (('fps', (30, 45, 60)), ('eye_width', (720, 1024, 1280))):
                if type(request.get(name)) is not int or request[name] not in choices:
                    raise ValueError('Unsupported recording ' + name)
            if type(request.get('telemetry')) is not bool:
                raise ValueError('Choose whether to include camera/controller data')
        elif action == 'stop':
            if not isinstance(request.get('recording_id'), str) or not request['recording_id']:
                raise ValueError('Missing recording identity')
        else:
            raise ValueError('Unknown recording action')
        return identity

    def step(self):
        # A removed profile is not recreated by a background recording service.
        if not self.directory.parent.is_dir():
            return
        self.directory.mkdir(exist_ok=True)
        game = None
        error = ''
        try:
            game = self.inspect_game()
        except (OSError, ValueError, RuntimeError, KeyError) as exc:
            error = str(exc)
        if game:
            pending = self.directory / ('request-' + str(game['pid']) + '.json')
            claimed = pending.with_suffix('.' + self.session + '.processing')
            try:
                pending.replace(claimed)
            except FileNotFoundError:
                pass
            else:
                identity = ''
                try:
                    request = read_object(claimed, 4096)
                    raw_id = request.get('id', '')
                    identity = raw_id if isinstance(raw_id, str) and len(raw_id) <= 64 else ''
                    identity = self.validate(request, game)
                    # Bound memory and make replayed commands harmless even after
                    # a recording has completed. Requests also expire in 5 s.
                    if len(self.seen) >= 1024:
                        self.seen.clear()
                    self.seen.add(identity)
                    if request['action'] == 'start':
                        self.start(not request['telemetry'], request['fps'],
                                   request['eye_width'], game['pid'])
                        message = 'Starting recording'
                    else:
                        self.stop(request['recording_id'])
                        message = 'Finishing recording'
                    self.last_request = {'id': identity, 'ok': True, 'message': message}
                except (OSError, ValueError, RuntimeError, KeyError) as exc:
                    self.last_request = {'id': identity, 'ok': False, 'message': str(exc)[:500]}
                finally:
                    claimed.unlink(missing_ok=True)
        current = self.snapshot()
        atomic_json(self.directory / 'server.json', {
            **current, 'version': 1, 'session': self.session, 'launcher_pid': os.getpid(),
            'unix_ms': self.clock(), 'pid': game['pid'] if game else 0,
            'created_ms': game['created_ms'] if game else 0,
            'available': bool(game) and current.get('available') is True,
            'connection_error': error[:500], 'last_request': self.last_request})

    def run(self, stopped):
        import msvcrt
        # One writer per profile, even if two launcher data folders were used.
        while not stopped.is_set():
            try:
                if self.directory.parent.is_dir() and self.inspect_game():
                    break
            except (OSError,ValueError,RuntimeError,KeyError):
                pass
            stopped.wait(1)
        if stopped.is_set():
            return
        self.directory.mkdir(exist_ok=True)
        with (self.directory / 'launcher.lock').open('a+b') as lock:
            lock.seek(0)
            try:
                msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
            except OSError:
                return
            try:
                while not stopped.is_set():
                    try:
                        self.step()
                    except (OSError, ValueError):
                        pass  # A missed heartbeat disables the in-game buttons.
                    stopped.wait(.5)
            finally:
                try:
                    path = self.directory / 'server.json'
                    if read_object(path).get('session') == self.session:
                        path.unlink()
                except (OSError, ValueError):
                    pass
                lock.seek(0)
                msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
