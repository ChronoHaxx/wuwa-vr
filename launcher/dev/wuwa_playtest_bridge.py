"""Expiring in-headset playtest requests, owned by the existing local helper.

Import and construction have no side effects. run(stopped) serves an existing
profile only after inspect_game verifies its process. No capture, launch, paths,
commands or automatic checklist verdicts are accepted over this protocol.
"""
from pathlib import Path
import copy
import hashlib
import json
import os
import re
import secrets
import stat
import time


VERSION = 1
REQUEST_LIMIT = 16384
STATUS_LIMIT = 65536
MAX_PENDING_MS = 5000
_REQUEST_ID = re.compile(r'[A-Za-z0-9][A-Za-z0-9_-]{0,63}\Z')
_SESSION_ID = re.compile(r'[a-f0-9]{32}\Z')
_ITEM_ID = re.compile(r'[A-Za-z0-9][A-Za-z0-9_-]{0,95}\Z')
_STATUSES = ('pass', 'fail', 'blocked', 'not_tested')
_COMMON = {'version', 'bridge_session', 'id', 'pid', 'created_ms', 'expires_ms',
           'playtest_session', 'item_id', 'action'}
_FIELDS = {'result': {'status'}, 'note': {'note'}, 'finish': {'confirm_untested'}, 'link-recording': set()}
_MESSAGES = {'result': 'Playtest result saved.', 'note': 'Playtest note saved.',
             'finish': 'Playtest finished. Untested checks remain Not tested.',
             'link-recording': 'Latest recording linked. Review the video in the launcher.'}


def _reject_link(path):
    try:
        info = path.lstat()
    except FileNotFoundError:
        return
    if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
        raise ValueError('Linked playtest bridge paths are unsupported')
    if stat.S_ISREG(info.st_mode) and info.st_nlink > 1:
        raise ValueError('Hard-linked playtest bridge files are unsupported')


def read_object(path, limit=REQUEST_LIMIT):
    _reject_link(path)
    with path.open('rb') as source:
        raw = source.read(limit + 1)
    if len(raw) > limit:
        raise ValueError('Playtest message exceeds size limit')
    value = json.loads(raw)
    if not isinstance(value, dict):
        raise ValueError('Playtest message must be an object')
    return value


def atomic_json(path, value):
    _reject_link(path)
    encoded = json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(',', ':')).encode('utf-8')
    if len(encoded) > STATUS_LIMIT:
        raise ValueError('Playtest status exceeds size limit')
    temporary = path.with_name(path.name + '.' + secrets.token_hex(8) + '.tmp')
    try:
        with temporary.open('xb') as output:
            output.write(encoded)
            output.flush()
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def _game_identity(game):
    if (not isinstance(game, dict) or type(game.get('pid')) is not int or not 0 < game['pid'] <= 0xffffffff
            or type(game.get('created_ms')) is not int or game['created_ms'] <= 0):
        raise ValueError('The game process could not be verified')
    return {'pid': game['pid'], 'created_ms': game['created_ms']}


class Bridge:
    def __init__(self, profile, inspect_game, snapshot, action, *, clock=None):
        self.directory = Path(profile) / 'playtest-control'
        self.inspect_game, self.snapshot, self.action = inspect_game, snapshot, action
        self.clock = clock or (lambda: int(time.time() * 1000))
        self.session = secrets.token_hex(16)
        self.last_request = {}
        self._seen = {}

    def _directory_ready(self, create=False):
        if not self.directory.parent.is_dir():
            return False
        _reject_link(self.directory.parent)
        _reject_link(self.directory)
        if create:
            self.directory.mkdir(exist_ok=True)
        return self.directory.is_dir()

    def _base_request(self, request, game):
        action = request.get('action')
        if not isinstance(action, str) or action not in _FIELDS:
            raise ValueError('Unknown playtest action')
        if set(request) != _COMMON | _FIELDS[action]:
            raise ValueError('Unexpected or missing playtest request field')
        if type(request.get('version')) is not int or request['version'] != VERSION:
            raise ValueError('Unsupported playtest bridge version')
        if request.get('bridge_session') != self.session:
            raise ValueError('Playtest launcher connection changed; refresh before retrying')
        identity = request.get('id')
        if not isinstance(identity, str) or not _REQUEST_ID.fullmatch(identity):
            raise ValueError('Invalid playtest request ID')
        if (type(request.get('pid')) is not int or request['pid'] != game['pid']
                or type(request.get('created_ms')) is not int
                or abs(request['created_ms'] - game['created_ms']) > 2):
            raise ValueError('Playtest request belongs to another game process')
        expiry = request.get('expires_ms')
        if type(expiry) is not int or not 0 < expiry - self.clock() <= MAX_PENDING_MS:
            raise ValueError('Playtest request expired; refresh before retrying')
        if not isinstance(request.get('playtest_session'), str) or not _SESSION_ID.fullmatch(request['playtest_session']):
            raise ValueError('Invalid playtest session')
        if action in ('result', 'note'):
            if not isinstance(request.get('item_id'), str) or not _ITEM_ID.fullmatch(request['item_id']):
                raise ValueError('Invalid playtest checklist item')
        elif request.get('item_id') != '':
            raise ValueError('Session actions must not name a checklist item')
        if action == 'result' and request.get('status') not in _STATUSES:
            raise ValueError('Choose Pass, Fail, Blocked or Not tested')
        if action == 'note':
            note = request.get('note')
            if not isinstance(note, str) or not note.strip() or len(note) > 4000:
                raise ValueError('Write a playtest note of at most 4000 characters')
            note.encode('utf-8', errors='strict')
        if action == 'finish' and request.get('confirm_untested') is not True:
            raise ValueError('Confirm that untested checks remain Not tested before finishing')
        return identity

    def _dispatch(self, request, game, state):
        identity = self._base_request(request, game)
        fingerprint = hashlib.sha256(json.dumps(request, ensure_ascii=True, sort_keys=True,
                                                 allow_nan=False).encode('ascii')).hexdigest()
        prior = self._seen.get(identity)
        if prior:
            if prior['fingerprint'] != fingerprint:
                raise ValueError('Playtest request ID was already used for different input')
            return copy.deepcopy(prior['reply'])
        now = self.clock()
        self._seen = {key: value for key, value in self._seen.items() if value['expires_ms'] + 60000 >= now}
        if len(self._seen) >= 1024:
            raise ValueError('Too many recent playtest requests; wait before retrying')
        current = state.get('session')
        if (not isinstance(current, dict) or current.get('state') != 'active'
                or current.get('session_id') != request['playtest_session']):
            raise ValueError('This playtest is no longer active; open the current session in the launcher')
        if request['action'] in ('result', 'note') and request['item_id'] not in {
                check['id'] for check in current.get('checks', [])}:
            raise ValueError('Checklist item is not in the current playtest')
        # Recheck process identity directly before a mutation; pid reuse and
        # stale backend heartbeats must not apply a result to another run.
        fresh = _game_identity(self.inspect_game())
        if fresh != game:
            raise ValueError('The game process changed before the playtest request')
        body = {'session_id': request['playtest_session'], 'event_id': 'headset-' + identity}
        if request['action'] in ('result', 'note'):
            body['item_id'] = request['item_id']
        if request['action'] == 'result':
            body['status'] = request['status']
        if request['action'] == 'note':
            body['note'] = request['note']
        self.action(request['action'], body)
        reply = {'id': identity, 'ok': True, 'action': request['action'], 'message': _MESSAGES[request['action']]}
        self._seen[identity] = {'fingerprint': fingerprint, 'expires_ms': request['expires_ms'], 'reply': reply}
        return copy.deepcopy(reply)

    @staticmethod
    def _public_session(state):
        source = state.get('session')
        if source is None:
            return None
        if (not isinstance(source, dict) or not isinstance(source.get('session_id'), str)
                or not _SESSION_ID.fullmatch(source['session_id']) or source.get('state') not in ('active', 'finished')):
            raise ValueError('Invalid current playtest state')
        checks = source.get('checks')
        if not isinstance(checks, list) or not 1 <= len(checks) <= 32:
            raise ValueError('Current playtest checklist is unavailable or too large')
        result = []
        ids = set()
        for check in checks:
            if (not isinstance(check, dict) or not isinstance(check.get('id'), str)
                    or not _ITEM_ID.fullmatch(check['id']) or check['id'] in ids or check.get('status') not in _STATUSES):
                raise ValueError('Invalid current checklist item')
            ids.add(check['id'])
            localized = {}
            for key, limit in (('title', 200), ('instructions', 1200)):
                value = check.get(key)
                if not isinstance(value, dict) or not isinstance(value.get('en'), str):
                    raise ValueError('Invalid checklist text')
                localized[key] = {code: text[:limit] for code, text in value.items()
                                  if code in ('en', 'zh', 'zh-Hans') and isinstance(text, str)}
            result.append({'id': check['id'], 'status': check['status'], **localized})
        build = source.get('build', {})
        if not isinstance(build, dict) or not isinstance(source.get('recordings', []), list):
            raise ValueError('Invalid playtest build or recording metadata')
        return {'id': source['session_id'], 'state': source['state'], 'checks': result,
                'build_name': str(build.get('name') or build.get('id') or '')[:200],
                'counts': {key: sum(check['status'] == key for check in result) for key in _STATUSES},
                'recording_count': len(source.get('recordings', []))}

    def step(self):
        if not self._directory_ready():
            # Do not create profile/control files until a game is verified.
            try:
                _game_identity(self.inspect_game())
            except (OSError, ValueError, RuntimeError, KeyError):
                return
            if not self._directory_ready(create=True):
                return
        game = None
        error = ''
        state = {}
        try:
            game = _game_identity(self.inspect_game())
            state = self.snapshot()
            if not isinstance(state, dict):
                raise ValueError('Playtest helper state is unavailable')
        except (OSError, ValueError, RuntimeError, KeyError) as exc:
            state = {}
            error = str(exc)[:500]
        if game and not error:
            pending = self.directory / ('request-' + str(game['pid']) + '.json')
            claimed = pending.with_suffix('.' + self.session + '.processing')
            identity = ''
            action = ''
            try:
                _reject_link(pending)
                _reject_link(claimed)
                pending.replace(claimed)
            except FileNotFoundError:
                pass
            except (OSError, ValueError) as exc:
                error = str(exc)[:500]
            else:
                try:
                    request = read_object(claimed)
                    raw_id = request.get('id')
                    identity = raw_id if isinstance(raw_id, str) and _REQUEST_ID.fullmatch(raw_id) else ''
                    action = request.get('action') if request.get('action') in _FIELDS else ''
                    self.last_request = self._dispatch(request, game, state)
                except (OSError, ValueError, RuntimeError, KeyError, TypeError) as exc:
                    self.last_request = {'id': identity, 'ok': False, 'action': action, 'message': str(exc)[:500]}
                finally:
                    claimed.unlink(missing_ok=True)
                try:
                    state = self.snapshot()
                    if not isinstance(state, dict):
                        raise ValueError('Playtest helper state is unavailable')
                except (OSError, ValueError, RuntimeError, KeyError) as exc:
                    state = {}
                    error = str(exc)[:500]
        public = None
        voice_busy = False
        try:
            public = self._public_session(state) if not error else None
            voice = state.get('voice', {})
            if not isinstance(voice, dict):
                raise ValueError('Playtest voice activity is unavailable')
            voice_busy = bool(voice.get('active') or voice.get('transcribing'))
        except (ValueError, KeyError, TypeError) as exc:
            error = str(exc)[:500]
        value = {'version': VERSION, 'bridge_session': self.session, 'launcher_pid': os.getpid(),
                 'unix_ms': self.clock(), 'pid': game['pid'] if game else 0,
                 'created_ms': game['created_ms'] if game else 0,
                 'available': bool(game and not error and public and public['state'] == 'active'),
                 'playtest': public, 'voice_busy': voice_busy,
                 'connection_error': error, 'last_request': self.last_request}
        try:
            atomic_json(self.directory / 'server.json', value)
        except ValueError as exc:
            value.update(available=False, playtest=None, connection_error=str(exc)[:500])
            atomic_json(self.directory / 'server.json', value)

    def close(self):
        """Remove only this bridge's heartbeat; a replacement helper keeps its own."""
        try:
            if not self._directory_ready():
                return
            path = self.directory / 'server.json'
            if read_object(path, STATUS_LIMIT).get('bridge_session') == self.session:
                path.unlink()
        except (OSError, ValueError):
            pass

    def run(self, stopped):
        import msvcrt
        while not stopped.is_set():
            try:
                if self.directory.parent.is_dir() and _game_identity(self.inspect_game()):
                    break
            except (OSError, ValueError, RuntimeError, KeyError):
                pass
            stopped.wait(1)
        if stopped.is_set() or not self._directory_ready(create=True):
            return
        lock_path = self.directory / 'launcher.lock'
        _reject_link(lock_path)
        with lock_path.open('a+b') as lock:
            lock.seek(0)
            try:
                msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
            except OSError:
                return
            try:
                while not stopped.is_set():
                    try:
                        self.step()
                    except (OSError, ValueError, RuntimeError, KeyError, TypeError):
                        pass  # A missed/stale heartbeat disables native buttons.
                    stopped.wait(.5)
            finally:
                self.close()
                lock.seek(0)
                msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
