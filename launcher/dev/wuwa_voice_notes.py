"""Explicit developer microphone notes and offline, post-session transcription.

Import, construction and enumeration never open an input device. Only start()
with literal consent=True can do so. The HTTP owner must authenticate requests,
show the selected microphone and serialize its own session/game start actions
against transcription. There is no network, download or model discovery code.

Capture uses one bounded WinMM input buffer, never a desktop/loopback API.
Reference: https://learn.microsoft.com/windows/win32/api/mmeapi/nf-mmeapi-waveinreset
Whisper flags: https://github.com/ggml-org/whisper.cpp/blob/master/examples/cli/cli.cpp
"""
from pathlib import Path
import copy
import ctypes
import json
import os
import re
import secrets
import subprocess
import threading
import time
import wave


SAMPLE_RATE = 16000
SAMPLE_BYTES = 2
MAX_SECONDS = 120
MAX_TRANSCRIPT_BYTES = 128 * 1024
_IDENTITY = re.compile(r'[A-Za-z0-9][A-Za-z0-9_-]{0,79}\Z')
_NOTE_ID = re.compile(r'voice-[a-f0-9]{32}\Z')
_IN_PROGRESS = {'recording', 'stopping', 'transcribing'}
_RESERVED_NAMES = {'CON', 'PRN', 'AUX', 'NUL'} | {
    prefix + str(number) for prefix in ('COM', 'LPT') for number in range(1, 10)}
# Retain memory if a broken native driver refuses to release its prepared
# buffer. Freeing memory still owned by WinMM would be unsafe. Further capture
# is disabled in this process after such a failure.
_UNRELEASED_NATIVE = []


class VoiceNoteError(RuntimeError):
    pass


def _identity(value, note=False):
    pattern = _NOTE_ID if note else _IDENTITY
    if not isinstance(value, str) or not pattern.fullmatch(value):
        raise ValueError('Invalid voice note ID' if note else 'Invalid playtest session ID')
    if value.upper() in _RESERVED_NAMES:
        raise ValueError('Reserved playtest session ID')
    return value


def _milliseconds():
    return int(time.time() * 1000)


def _atomic_json(path, value):
    temporary = path.with_name(path.name + '.' + secrets.token_hex(6) + '.tmp')
    try:
        with temporary.open('x', encoding='utf-8') as output:
            json.dump(value, output, ensure_ascii=False, indent=2)
            output.flush()
            os.fsync(output.fileno())
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def _read_json(path):
    with path.open('rb') as source:
        raw = source.read(MAX_TRANSCRIPT_BYTES + 16385)
    if len(raw) > MAX_TRANSCRIPT_BYTES + 16384:
        raise VoiceNoteError('Voice note metadata exceeds the size limit')
    value = json.loads(raw)
    if not isinstance(value, dict):
        raise VoiceNoteError('Invalid voice note metadata')
    return value


def _wave_duration(path):
    if not path.is_file() or path.stat().st_size > SAMPLE_RATE * SAMPLE_BYTES * MAX_SECONDS + 4096:
        raise VoiceNoteError('Voice WAV is missing or exceeds the duration limit')
    try:
        with wave.open(str(path), 'rb') as source:
            frames = source.getnframes()
            if (source.getnchannels() != 1 or source.getsampwidth() != SAMPLE_BYTES
                    or source.getframerate() != SAMPLE_RATE or source.getcomptype() != 'NONE'
                    or not 0 < frames <= SAMPLE_RATE * MAX_SECONDS
                    or len(source.readframes(frames)) != frames * SAMPLE_BYTES):
                raise VoiceNoteError('Voice WAV must be bounded mono 16 kHz 16-bit PCM')
            return round(frames * 1000 / SAMPLE_RATE)
    except (wave.Error, EOFError) as exc:
        raise VoiceNoteError('Voice WAV is incomplete or invalid') from exc


class _WaveFormat(ctypes.Structure):
    _pack_ = 1
    _fields_ = [('tag', ctypes.c_uint16), ('channels', ctypes.c_uint16),
                ('samples', ctypes.c_uint32), ('bytes_per_second', ctypes.c_uint32),
                ('align', ctypes.c_uint16), ('bits', ctypes.c_uint16),
                ('extra', ctypes.c_uint16)]


class _WaveHeader(ctypes.Structure):
    _fields_ = [('data', ctypes.c_void_p), ('length', ctypes.c_uint32),
                ('recorded', ctypes.c_uint32), ('user', ctypes.c_size_t),
                ('flags', ctypes.c_uint32), ('loops', ctypes.c_uint32),
                ('next', ctypes.c_void_p), ('reserved', ctypes.c_size_t)]


class _WaveCaps(ctypes.Structure):
    _fields_ = [('manufacturer', ctypes.c_uint16), ('product', ctypes.c_uint16),
                ('version', ctypes.c_uint32), ('name', ctypes.c_wchar * 32),
                ('formats', ctypes.c_uint32), ('channels', ctypes.c_uint16),
                ('reserved', ctypes.c_uint16)]


def _microphone_name(name):
    # WinMM does not expose endpoint form factor. Fail closed on unidentified
    # or mixing inputs; device labels are a practical filter, not certification
    # that a virtual driver cannot masquerade as a physical microphone.
    lower = name.casefold()
    excluded = ('mix', 'loopback', 'stereo', 'what u hear', 'what you hear',
                'cable', 'voicemeeter', 'monitor', '立体声', '混音',
                'ステレオ', '믹스')
    # Virtual Desktop forwards the explicitly named headset microphone. Its
    # transport is not a desktop-loopback source. Other anonymous virtual
    # devices remain unsupported because their routing cannot be identified.
    virtual_ok = 'virtual' not in lower or 'virtual desktop' in lower
    return (virtual_ok and not any(part in lower for part in excluded)
            and any(part in lower for part in ('microphone', 'headset', 'mic ', 'mic(', 'mic-',
                                                '麦克风', '麥克風', 'マイク', '마이크')))


class WinMMCapture:
    """Lazy Windows input adapter; injectable WinMM API enables hardware-free QA."""

    def __init__(self, api=None, *, monotonic=None):
        self._api = api
        self._clock = monotonic or time.monotonic

    def _native(self):
        if self._api is not None:
            return self._api
        if os.name != 'nt':
            raise VoiceNoteError('Microphone notes require Windows WinMM')
        try:
            api = ctypes.WinDLL('winmm', use_last_error=True)
        except OSError as exc:
            raise VoiceNoteError('Windows microphone input is unavailable') from exc
        uint, ptr, size = ctypes.c_uint32, ctypes.c_void_p, ctypes.c_size_t
        signatures = {
            'waveInGetNumDevs': ([], uint),
            'waveInGetDevCapsW': ([size, ctypes.POINTER(_WaveCaps), uint], uint),
            'waveInOpen': ([ctypes.POINTER(ptr), uint, ctypes.POINTER(_WaveFormat), size, size, uint], uint),
            'waveInPrepareHeader': ([ptr, ctypes.POINTER(_WaveHeader), uint], uint),
            'waveInAddBuffer': ([ptr, ctypes.POINTER(_WaveHeader), uint], uint),
            'waveInStart': ([ptr], uint), 'waveInStop': ([ptr], uint),
            'waveInReset': ([ptr], uint), 'waveInClose': ([ptr], uint),
            'waveInUnprepareHeader': ([ptr, ctypes.POINTER(_WaveHeader), uint], uint),
        }
        for name, (arguments, result) in signatures.items():
            function = getattr(api, name)
            function.argtypes, function.restype = arguments, result
        self._api = api
        return api

    @staticmethod
    def _check(code, operation):
        if code:
            hints = {2: 'device unavailable', 4: 'microphone already in use',
                     6: 'no input driver', 7: 'not enough memory',
                     8: 'format unavailable', 32: 'mono 16 kHz PCM unsupported',
                     33: 'driver still owns the audio buffer'}
            raise VoiceNoteError('Microphone ' + operation + ' failed: '
                                 + hints.get(code, 'Windows input error') + ' (' + str(code) + ')')

    def devices(self):
        api = self._native()
        result = []
        for device_id in range(min(int(api.waveInGetNumDevs()), 256)):
            caps = _WaveCaps()
            code = api.waveInGetDevCapsW(device_id, ctypes.byref(caps), ctypes.sizeof(caps))
            if code:
                continue  # Disconnected devices are not selectable.
            name = caps.name.strip()
            allowed = caps.channels > 0 and _microphone_name(name)
            result.append({'id': device_id, 'name': name, 'selectable': allowed,
                           'reason': '' if allowed else 'Only recognized microphone inputs are supported'})
        return result

    def capture(self, path, device_id, stop_event, cancel_event, *, max_seconds=MAX_SECONDS,
                expected_name=None, on_started=None):
        if _UNRELEASED_NATIVE:
            raise VoiceNoteError('A microphone driver did not release an earlier buffer; restart the launcher')
        if type(device_id) is not int or device_id < 0:
            raise ValueError('Choose an explicit microphone device')
        if type(max_seconds) not in (int, float) or not 0 < max_seconds <= MAX_SECONDS:
            raise ValueError('Voice note duration must be between 0 and 120 seconds')
        selected = next((d for d in self.devices() if d['id'] == device_id), None)
        if not selected or not selected['selectable']:
            raise VoiceNoteError('Selected microphone is unavailable or is not a recognized microphone input')
        if expected_name is not None and selected['name'] != expected_name:
            raise VoiceNoteError('Microphone devices changed; select the microphone again')
        if stop_event.is_set() or cancel_event.is_set():
            return {'cancelled': cancel_event.is_set(), 'duration_ms': 0}

        api = self._native()
        samples = int(SAMPLE_RATE * max_seconds)
        buffer = ctypes.create_string_buffer(samples * SAMPLE_BYTES)
        header = _WaveHeader(data=ctypes.addressof(buffer), length=samples * SAMPLE_BYTES)
        fmt = _WaveFormat(1, 1, SAMPLE_RATE, SAMPLE_RATE * SAMPLE_BYTES, SAMPLE_BYTES, 16, 0)
        handle = ctypes.c_void_p()
        prepared = False
        cleanup_errors = []
        try:
            self._check(api.waveInOpen(ctypes.byref(handle), device_id, ctypes.byref(fmt), 0, 0, 0), 'open')
            self._check(api.waveInPrepareHeader(handle, ctypes.byref(header), ctypes.sizeof(header)), 'prepare')
            prepared = True
            self._check(api.waveInAddBuffer(handle, ctypes.byref(header), ctypes.sizeof(header)), 'queue')
            if not (stop_event.is_set() or cancel_event.is_set()):
                self._check(api.waveInStart(handle), 'start')
                start = self._clock()
                if on_started:
                    on_started()
                while not (stop_event.is_set() or cancel_event.is_set() or header.flags & 1):
                    remaining = max_seconds - (self._clock() - start)
                    if remaining <= 0:
                        break
                    stop_event.wait(min(.05, remaining))
        finally:
            if handle.value:
                # Reset returns even a partially filled buffer. Always attempt
                # every cleanup step, including failures before waveInStart.
                reset = api.waveInReset(handle)
                if reset:
                    api.waveInStop(handle)
                    reset = api.waveInReset(handle)
                if reset:
                    cleanup_errors.append('reset=' + str(reset))
                released = not prepared
                if prepared:
                    code = api.waveInUnprepareHeader(handle, ctypes.byref(header), ctypes.sizeof(header))
                    released = code == 0
                    if code:
                        cleanup_errors.append('unprepare=' + str(code))
                code = api.waveInClose(handle)
                if code:
                    cleanup_errors.append('close=' + str(code))
                if not released or code:
                    _UNRELEASED_NATIVE.append((api, handle, header, buffer))
                if cleanup_errors:
                    raise VoiceNoteError('Microphone cleanup failed; restart the launcher: ' + ', '.join(cleanup_errors))
        if cancel_event.is_set():
            return {'cancelled': True, 'duration_ms': 0}
        count = min(int(header.recorded), samples * SAMPLE_BYTES)
        count -= count % SAMPLE_BYTES
        if count == 0:
            raise VoiceNoteError('The microphone returned no audio; check device access and try again')
        # Only persist after WinMM releases the input. A crash while recording
        # cannot leave an apparently completed WAV or expose unwritten memory.
        temporary = path.with_suffix('.wav.part')
        try:
            with temporary.open('xb') as destination:
                with wave.open(destination, 'wb') as output:
                    output.setnchannels(1)
                    output.setsampwidth(SAMPLE_BYTES)
                    output.setframerate(SAMPLE_RATE)
                    output.writeframes(buffer.raw[:count])
                destination.flush()
                os.fsync(destination.fileno())
            temporary.replace(path)
        finally:
            temporary.unlink(missing_ok=True)
        return {'cancelled': False, 'duration_ms': round(count * 1000 / (SAMPLE_RATE * SAMPLE_BYTES))}


class LocalWhisper:
    """One existing local whisper-cli/model, fixed arguments, CPU and one thread."""

    def __init__(self, *, popen=None, monotonic=None, timeout_seconds=600):
        self._popen = popen or subprocess.Popen
        self._clock = monotonic or time.monotonic
        if type(timeout_seconds) not in (int, float) or not 0 < timeout_seconds <= 600:
            raise ValueError('Transcription timeout must be at most 600 seconds')
        self.timeout_seconds = timeout_seconds

    @staticmethod
    def _local_file(value, label):
        if not isinstance(value, (str, os.PathLike)) or not str(value):
            raise ValueError('Configure an existing local ' + label)
        path = Path(value)
        if not path.is_absolute() or str(path).startswith(('\\\\', '//')):
            raise ValueError(label + ' must be an absolute local path')
        path = path.resolve(strict=True)
        if not path.is_file() or path.stat().st_size == 0:
            raise ValueError(label + ' must be an existing nonempty local file')
        if os.name == 'nt':
            kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            kernel.GetDriveTypeW.argtypes = [ctypes.c_wchar_p]
            kernel.GetDriveTypeW.restype = ctypes.c_uint32
            if kernel.GetDriveTypeW(path.anchor) not in (2, 3, 6):
                raise ValueError(label + ' must be on a local disk')
        return path

    def validate(self, whisper_cli, model_path, language='auto'):
        executable = self._local_file(whisper_cli, 'whisper-cli executable')
        if executable.name.casefold() not in ('whisper-cli.exe', 'whisper-cli'):
            raise ValueError('Choose the existing whisper.cpp whisper-cli executable')
        if os.name == 'nt' and executable.suffix.casefold() != '.exe':
            raise ValueError('Windows transcription requires whisper-cli.exe')
        model = self._local_file(model_path, 'Whisper model')
        if model.suffix.casefold() != '.bin':
            raise ValueError('Choose an existing whisper.cpp .bin model')
        language = {'zh-Hans': 'zh', 'zh-Hant': 'zh'}.get(language, language)
        if not isinstance(language, str) or not re.fullmatch(r'auto|[a-z]{2,3}', language):
            raise ValueError('Invalid transcription language')
        return executable, model, language

    @staticmethod
    def _finish_process(process):
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=3)

    def transcribe(self, wav_path, output_prefix, whisper_cli, model_path, language,
                   cancel_event, idle_check):
        executable, model, language = self.validate(whisper_cli, model_path, language)
        _wave_duration(wav_path)
        if cancel_event.is_set():
            raise VoiceNoteError('Transcription cancelled; original WAV retained')
        idle_check()
        output = Path(str(output_prefix) + '.txt')
        if output.exists():
            raise VoiceNoteError('Transcription output already exists')
        command = [str(executable), '-m', str(model), '-f', str(wav_path),
                   '-of', str(output_prefix), '-otxt', '-l', language, '-ng', '-t', '1']
        flags = (getattr(subprocess, 'CREATE_NO_WINDOW', 0)
                 | getattr(subprocess, 'BELOW_NORMAL_PRIORITY_CLASS', 0)) if os.name == 'nt' else 0
        process = None
        start = self._clock()
        next_idle_check = start
        try:
            process = self._popen(command, shell=False, stdin=subprocess.DEVNULL,
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                  cwd=str(executable.parent), creationflags=flags)
            while True:
                now = self._clock()
                if now >= next_idle_check:
                    idle_check()
                    next_idle_check = now + 1
                if cancel_event.is_set():
                    raise VoiceNoteError('Transcription cancelled; original WAV retained')
                if now - start >= self.timeout_seconds:
                    raise VoiceNoteError('Local transcription timed out; original WAV retained')
                if output.exists() and output.stat().st_size > MAX_TRANSCRIPT_BYTES:
                    raise VoiceNoteError('Local transcript exceeds size limit')
                result = process.poll()
                if result is not None:
                    if result != 0:
                        raise VoiceNoteError('Local whisper-cli failed (exit ' + str(result) + '); original WAV retained')
                    break
                cancel_event.wait(.2)
            idle_check()
            if output.is_symlink() or not output.is_file():
                raise VoiceNoteError('Local whisper-cli returned no transcript; check model and CLI compatibility')
            with output.open('rb') as source:
                raw = source.read(MAX_TRANSCRIPT_BYTES + 1)
            if len(raw) > MAX_TRANSCRIPT_BYTES:
                raise VoiceNoteError('Local transcript exceeds size limit')
            return raw.decode('utf-8-sig', errors='strict').strip()
        finally:
            try:
                if process is not None:
                    self._finish_process(process)
            finally:
                output.unlink(missing_ok=True)


class VoiceNoteManager:
    """One opt-in capture or local transcription at a time; durable note manifests.

    activity_probe(session_id), when supplied, must return literal booleans for
    playtest_stopped and gameplay_idle. It is rechecked while Whisper runs.
    capture_factory returns an adapter with devices() and capture() matching
    WinMMCapture; transcriber matches LocalWhisper. These seams are for tests.
    """

    def __init__(self, root, *, activity_probe=None, capture_factory=None, transcriber=None):
        self.root = Path(root).resolve()
        self.activity_probe = activity_probe
        self.capture_factory = capture_factory or WinMMCapture
        self.transcriber = transcriber or LocalWhisper()
        self._lock = threading.RLock()
        self._active = None
        self._job = None
        self._last = None
        self._closed = False

    def _path(self, session_id, note_id=None, suffix='.json', *, create=False):
        _identity(session_id)
        if note_id is not None:
            _identity(note_id, note=True)
        directory = self.root / session_id
        candidate = directory / (note_id + suffix) if note_id else directory
        if not candidate.resolve().is_relative_to(self.root):
            raise ValueError('Voice note path escapes its storage directory')
        if directory.is_symlink() or candidate.is_symlink():
            raise ValueError('Linked voice note paths are not supported')
        if create:
            directory.mkdir(parents=True, exist_ok=True)
        return candidate

    def _save(self, note):
        path = self._path(note['session_id'], note['id'], create=True)
        _atomic_json(path, note)

    def devices(self):
        return self.capture_factory().devices()

    def note(self, session_id, note_id):
        with self._lock:
            _identity(note_id, note=True)
            value = _read_json(self._path(session_id, note_id))
            expected = session_id + '/' + note_id + '.wav'
            if (value.get('version') != 1 or value.get('id') != note_id
                    or value.get('session_id') != session_id
                    or value.get('wav_file') not in (None, expected)):
                raise VoiceNoteError('Voice note metadata does not match its identity')
            running = [entry['note']['id'] for entry in (self._active, self._job) if entry]
            if value.get('status') in _IN_PROGRESS and note_id not in running:
                value['status'] = 'error'
                value['error'] = 'Previous voice operation was interrupted; any original WAV is retained'
                value['ended_ms'] = value.get('ended_ms') or _milliseconds()
                wav = self._path(session_id, note_id, '.wav')
                if wav.is_file():
                    value['wav_file'] = expected
                    try:
                        value['duration_ms'] = _wave_duration(wav)
                    except (OSError, ValueError, VoiceNoteError):
                        pass
                self._save(value)
            return value

    def list_notes(self, session_id):
        with self._lock:
            directory = self._path(session_id)
            if not directory.is_dir():
                return []
            paths = sorted(directory.glob('voice-*.json'))
            if len(paths) > 1000:
                raise VoiceNoteError('This session has too many voice notes')
            notes = [self.note(session_id, path.stem) for path in paths]
            return sorted(notes, key=lambda item: (item.get('created_ms', 0), item['id']))

    def path_for_note(self, session_id, note_id):
        with self._lock:
            note = self.note(session_id, note_id)
            if not note.get('wav_file') or note.get('status') in ('recording', 'stopping', 'cancelled'):
                raise VoiceNoteError('The voice note has no completed WAV')
            path = self._path(session_id, note_id, '.wav')
            _wave_duration(path)
            return path

    def snapshot(self):
        with self._lock:
            return copy.deepcopy({'active': self._active['note'] if self._active else None,
                                  'transcribing': self._job['note'] if self._job else None,
                                  'last': self._last, 'max_seconds': MAX_SECONDS})

    def _available(self):
        if self._closed:
            raise VoiceNoteError('Voice note manager is closed')
        if self._active or self._job:
            raise VoiceNoteError('Finish the current voice capture or transcription first')

    def start(self, session_id, device_id, *, consent):
        with self._lock:
            self._available()
            if consent is not True:
                raise ValueError('Explicit microphone consent is required for every voice note')
            _identity(session_id)
            if type(device_id) is not int or device_id < 0:
                raise ValueError('Choose an explicit microphone device')
            adapter = self.capture_factory()
            selected = next((d for d in adapter.devices() if d['id'] == device_id), None)
            if not selected or selected.get('selectable') is not True:
                raise VoiceNoteError('Selected microphone is unavailable or unsupported')
            note = {'version': 1, 'id': 'voice-' + secrets.token_hex(16), 'session_id': session_id,
                    'device_id': device_id, 'device_name': selected['name'], 'status': 'recording',
                    'created_ms': _milliseconds(), 'started_ms': None, 'ended_ms': None,
                    'duration_ms': 0, 'wav_file': None, 'transcript': None, 'error': ''}
            entry = {'note': note, 'stop': threading.Event(), 'cancel': threading.Event()}
            self._save(note)
            self._active = entry
            thread = threading.Thread(target=self._capture, args=(entry, adapter),
                                      name='wuwa-voice-note', daemon=True)
            entry['thread'] = thread
            try:
                thread.start()
            except Exception:
                self._active = None
                note.update(status='error', ended_ms=_milliseconds(), error='Unable to start microphone worker')
                self._save(note)
                raise
            return copy.deepcopy(note)

    def _capture(self, entry, adapter):
        note = entry['note']
        path = None

        def started():
            with self._lock:
                note['started_ms'] = _milliseconds()
                self._save(note)

        try:
            path = self._path(note['session_id'], note['id'], '.wav')
            result = adapter.capture(path, note['device_id'], entry['stop'], entry['cancel'],
                                     max_seconds=MAX_SECONDS, expected_name=note['device_name'], on_started=started)
            with self._lock:
                if entry['cancel'].is_set() or result.get('cancelled'):
                    path.unlink(missing_ok=True)
                    note.update(status='cancelled', duration_ms=0, wav_file=None)
                else:
                    duration = _wave_duration(path)
                    note.update(status='recorded', duration_ms=duration,
                                wav_file=note['session_id'] + '/' + note['id'] + '.wav')
        except Exception as exc:
            with self._lock:
                note.update(status='error', error=str(exc)[:500])
                if path is not None and path.is_file():
                    note['wav_file'] = note['session_id'] + '/' + note['id'] + '.wav'
        finally:
            with self._lock:
                note['ended_ms'] = _milliseconds()
                try:
                    self._save(note)
                except (OSError, ValueError) as exc:
                    note.update(status='error', error='Unable to save voice note metadata: ' + str(exc)[:350])
                self._last = copy.deepcopy(note)
                self._active = None

    def stop(self, note_id):
        with self._lock:
            _identity(note_id, note=True)
            if not self._active or self._active['note']['id'] != note_id:
                if self._last and self._last['id'] == note_id:
                    return copy.deepcopy(self._last)
                raise VoiceNoteError('This microphone note is not active')
            self._active['stop'].set()
            self._active['note']['status'] = 'stopping'
            return copy.deepcopy(self._active['note'])

    def cancel(self, note_id):
        with self._lock:
            _identity(note_id, note=True)
            for entry in (self._active, self._job):
                if entry and entry['note']['id'] == note_id:
                    entry['cancel'].set()
                    if entry is self._active:
                        entry['stop'].set()
                        entry['note']['status'] = 'stopping'
                    return copy.deepcopy(entry['note'])
            raise VoiceNoteError('This voice operation is not active')

    def _idle(self, session_id, playtest_stopped, gameplay_idle):
        if playtest_stopped is not True or gameplay_idle is not True:
            raise VoiceNoteError('Stop the playtest and gameplay before local transcription')
        with self._lock:
            if self._active:
                raise VoiceNoteError('Stop microphone capture before local transcription')
        if self.activity_probe is not None:
            state = self.activity_probe(session_id)
            if (not isinstance(state, dict) or state.get('playtest_stopped') is not True
                    or state.get('gameplay_idle') is not True):
                raise VoiceNoteError('Playtest or gameplay is active; original WAV retained')

    def transcribe(self, session_id, note_id, *, whisper_cli, model_path,
                   playtest_stopped, gameplay_idle, language='auto'):
        with self._lock:
            self._available()
            self._idle(session_id, playtest_stopped, gameplay_idle)
            executable, model, language = self.transcriber.validate(whisper_cli, model_path, language)
            note = self.note(session_id, note_id)
            path = self.path_for_note(session_id, note_id)
            entry = {'note': note, 'cancel': threading.Event()}
            note.update(status='transcribing', error='')
            self._save(note)
            self._job = entry
            check = lambda: self._idle(session_id, playtest_stopped, gameplay_idle)
            thread = threading.Thread(target=self._transcribe,
                                      args=(entry, path, executable, model, language, check),
                                      name='wuwa-local-transcript', daemon=True)
            entry['thread'] = thread
            try:
                thread.start()
            except Exception:
                self._job = None
                note.update(status='error', error='Unable to start transcription worker')
                self._save(note)
                raise
            return copy.deepcopy(note)

    def _transcribe(self, entry, path, executable, model, language, idle_check):
        note = entry['note']
        try:
            prefix = self._path(note['session_id'], note['id'], '.transcript-' + secrets.token_hex(8))
            text = self.transcriber.transcribe(path, prefix, executable, model, language,
                                              entry['cancel'], idle_check)
            if not isinstance(text, str) or len(text.encode('utf-8')) > MAX_TRANSCRIPT_BYTES:
                raise VoiceNoteError('Invalid or oversized local transcript')
            idle_check()
            if entry['cancel'].is_set():
                raise VoiceNoteError('Transcription cancelled; original WAV retained')
            with self._lock:
                note.update(status='transcribed', transcript={'text': text, 'verified': False,
                            'label': 'Unverified local transcription', 'language': language,
                            'engine': 'whisper.cpp', 'created_ms': _milliseconds()})
        except Exception as exc:
            with self._lock:
                note.update(status='error', error=str(exc)[:500])
        finally:
            with self._lock:
                try:
                    self._save(note)
                except (OSError, ValueError) as exc:
                    note.update(status='error', error='Unable to save transcript: ' + str(exc)[:350])
                self._last = copy.deepcopy(note)
                self._job = None

    def close(self, timeout=8):
        """Stop capture and cancel transcription; never silently discard a WAV."""
        with self._lock:
            self._closed = True
            entries = [entry for entry in (self._active, self._job) if entry]
            if self._active:
                self._active['stop'].set()
            if self._job:
                self._job['cancel'].set()
        deadline = time.monotonic() + min(max(float(timeout), 0), 10)
        for entry in entries:
            entry['thread'].join(max(0, deadline - time.monotonic()))
        if any(entry['thread'].is_alive() for entry in entries):
            raise VoiceNoteError('Voice cleanup is still running; do not start another capture')
