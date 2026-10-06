"""Recent local diagnostics, not a recording. No work starts on import.

Only fixed, already-published files are read. The five-minute ring lives in RAM;
an explicit save writes a bounded incident report. No game requests, process
memory reads, device capture, inputs, downloads or uploads are performed.
"""
from collections import deque
import copy
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import re
import secrets
import stat
import threading
import time

WINDOW_MS = 300000
POLL_SECONDS = 2
STATUS_BYTES = 256 * 1024
SAMPLE_BYTES = 24 * 1024
LOG_READ_BYTES = 64 * 1024
LOG_RING_BYTES = 1024 * 1024
REPORT_BYTES = 8 * 1024 * 1024
MAX_INCIDENTS = 50
ID = re.compile(r'incident-[0-9]{8}-[0-9]{6}-[0-9a-f]{12}\Z')


def _plain(path):
    info = path.lstat()
    if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
        raise ValueError('Linked diagnostic paths are not read or written')
    if stat.S_ISREG(info.st_mode) and info.st_nlink > 1:
        raise ValueError('Hard-linked diagnostic files are not read or written')
    return info


def read_object(path, limit=STATUS_BYTES):
    _plain(path.parent)
    if not stat.S_ISREG(_plain(path).st_mode):
        raise ValueError('Diagnostic source is not a regular file')
    with path.open('rb') as source:
        raw = source.read(limit + 1)
    if len(raw) > limit:
        raise ValueError('Diagnostic file exceeds size limit')
    result = json.loads(raw.decode('utf-8-sig'))
    if not isinstance(result, dict):
        raise ValueError('Diagnostic file is not an object')
    return result


def _compact(value, redact, depth=0):
    if depth > 4:
        return '[nested data omitted]'
    if value is None or type(value) in (bool, int):
        return value
    if type(value) is float:
        return value if math.isfinite(value) else None
    if isinstance(value, str):
        return redact(value)[:512]
    if isinstance(value, dict):
        return {str(k)[:96]: _compact(v, redact, depth + 1) for k, v in list(value.items())[:100]}
    if isinstance(value, list):
        return [_compact(v, redact, depth + 1) for v in value[:16]]
    return None


def package_identity(app, data):
    """Small metadata reads, not a DLL hash or proof of the loaded backend."""
    result = {'verification': 'Package metadata only; loaded DLL identity is unverified.'}
    try:
        info = read_object(Path(app) / 'portable.json')
        state = read_object(Path(data) / 'state.json')
        catalog = read_object(Path(app) / 'dev/wuwa-builds.json')
        result.update(package_id=info.get('packageId'), selected_build=state.get('selected'))
        selected = next((b for b in catalog.get('builds', []) if isinstance(b, dict)
                         and b.get('id') == result['selected_build']), {})
        result['catalog_backend_sha256'] = selected.get('sha256')
        result['state'] = 'available' if selected else 'selected_build_missing'
    except (OSError, ValueError) as exc:
        result.update(state='unavailable', error=type(exc).__name__)
    return result


class Collector:
    def __init__(self, profile, output, identity, *, redact=str, clock_ms=None):
        self.profile, self.output = Path(profile), Path(output)
        self.identity, self.redact = identity, redact
        self.clock = clock_ms or (lambda: int(time.time() * 1000))
        self.started_ms = self.clock()
        self.samples, self.logs = deque(), deque()
        self.log_bytes = 0
        self.log_evicted_bytes = 0
        self.log_cursor = None
        self.last_saved = None
        self.lock = threading.RLock()
        self.poll_lock = threading.Lock()
        self.save_lock = threading.Lock()

    def _status(self, now):
        result = {'status_state': 'missing', 'status_age_ms': None}
        try:
            value = read_object(self.profile / 'wuwa-test.status.json')
            stamp, pid = value.get('unix_ms'), value.get('pid')
            valid = type(stamp) is int and type(pid) is int and stamp > 0 and pid > 0
            age = now - stamp if valid else None
            result.update(status_state='invalid_identity' if not valid else
                          'clock_mismatch' if age < -2000 else 'fresh' if age <= 10000 else 'stale',
                          status_age_ms=age, backend_pid=pid if valid else None, backend_unix_ms=stamp if valid else None)
            keys = ('version', 'scene_frame_pair', 'shadow', 'live_options', 'projection_test',
                    'reflection_capture_projection', 'rim_suppression', 'mono_native', 'cinematic_framing',
                    'focus_losses', 'trace_dropped', 'trace_reader_overflow')
            selected = {k: value[k] for k in keys if k in value}
            camera = value.get('camera')
            if isinstance(camera, dict):
                selected['camera'] = {k: camera[k] for k in ('controls', 'sample_valid', 'tracking_ready') if k in camera}
            result['diagnostics'] = _compact(selected, self.redact)
        except FileNotFoundError:
            pass
        except (OSError, ValueError, RecursionError) as exc:
            result.update(status_state='unreadable', status_error=type(exc).__name__)
        return result

    def _log(self, now):
        path = self.profile / 'log.txt'
        try:
            _plain(path.parent)
            info = _plain(path)
            if not stat.S_ISREG(info.st_mode):
                raise ValueError('Backend log is not a regular file')
            file_id = (info.st_dev, info.st_ino)
            with path.open('rb') as source:
                prefix = source.read(128)
                size = source.seek(0, 2)
                previous = self.log_cursor
                reset = previous is None or previous[0] != file_id or size < previous[1] or not prefix.startswith(previous[2])
                offset = 0 if reset else previous[1]
                start = max(offset, size - LOG_READ_BYTES)
                source.seek(start)
                raw = source.read(LOG_READ_BYTES)
                self.log_cursor = (file_id, start + len(raw), prefix)
            chunk = None
            if raw:
                # A byte-limited tail may start within a UTF-8 character/line.
                # Keep that uncertainty explicit rather than fabricate a line.
                text = self.redact(raw.decode('utf-8', errors='replace'))
                chunk = {'observed_at_ms': now, 'file_mtime_ms': round(info.st_mtime * 1000),
                         'read_start_byte': start, 'read_end_byte': start + len(raw),
                         'skipped_bytes': start - offset, 'initial_or_rotated_tail': reset,
                         'starts_mid_line_possible': start > 0, 'text': text}
            return {'log_state': 'available', 'log_age_ms': now - round(info.st_mtime * 1000)}, chunk
        except FileNotFoundError:
            self.log_cursor = None
            return {'log_state': 'missing'}, None
        except (OSError, ValueError) as exc:
            return {'log_state': 'unreadable', 'log_error': type(exc).__name__}, None

    def _prune(self, now):
        while self.samples and (self.samples[0]['observed_at_ms'] < now - WINDOW_MS or len(self.samples) > 151):
            self.samples.popleft()
        while self.logs and (self.logs[0]['observed_at_ms'] < now - WINDOW_MS or self.log_bytes > LOG_RING_BYTES):
            amount = len(self.logs.popleft()['text'].encode('utf-8'))
            self.log_bytes -= amount
            self.log_evicted_bytes += amount

    def poll(self):
        # Save and the two-second worker cannot advance a shared log cursor twice.
        with self.poll_lock:
            now = self.clock()
            sample = {'observed_at_ms': now, **self._status(now)}
            try:
                sample['identity'] = _compact(self.identity(), self.redact)
            except Exception as exc:
                sample['identity'] = {'state': 'unavailable', 'error': type(exc).__name__}
            log_state, chunk = self._log(now)
            sample.update(log_state)
            if len(json.dumps(sample).encode('utf-8')) > SAMPLE_BYTES:
                sample['diagnostics'] = {'omitted': 'Selected diagnostics exceed per-sample byte limit'}
                sample['truncated'] = True
            if len(json.dumps(sample).encode('utf-8')) > SAMPLE_BYTES:
                sample['identity'] = {'state': 'omitted', 'error': 'Metadata exceeds per-sample byte limit'}
            with self.lock:
                self.samples.append(sample)
                if chunk:
                    self.logs.append(chunk)
                    self.log_bytes += len(chunk['text'].encode('utf-8'))
                self._prune(now)

    def run(self, stopped):
        while not stopped.is_set():
            try:
                self.poll()
            except Exception as exc:
                # Diagnostics must never stop the launcher or spin on an error.
                with self.lock:
                    self.samples.append({'observed_at_ms': self.clock(), 'status_state': 'collector_error',
                                         'error': type(exc).__name__})
                    self._prune(self.clock())
            stopped.wait(POLL_SECONDS)

    def snapshot(self):
        with self.lock:
            self._prune(self.clock())
            latest = self.samples[-1] if self.samples else {}
            return {'monitoring_started_ms': self.started_ms, 'retained_seconds': WINDOW_MS // 1000,
                    'sample_count': len(self.samples), 'log_bytes': self.log_bytes,
                    'log_evicted_bytes': self.log_evicted_bytes,
                    'latest': {k: v for k, v in latest.items() if k not in ('diagnostics', 'identity')},
                    'last_saved': copy.deepcopy(self.last_saved)}

    def save(self, note='', event_ago_seconds=0):
        if not isinstance(note, str) or len(note) > 4000:
            raise ValueError('Incident note must be text, at most 4000 characters')
        if type(event_ago_seconds) is not int or not 0 <= event_ago_seconds <= 300:
            raise ValueError('Event age must be an integer from 0 to 300 seconds')
        with self.save_lock:
            self.poll()
            now = self.clock()
            with self.lock:
                self._prune(now)
                samples, logs = copy.deepcopy(list(self.samples)), copy.deepcopy(list(self.logs))
                evicted_bytes = self.log_evicted_bytes
            warnings = ['No retrospective video, screenshots, audio or input history is captured.',
                        'File freshness does not verify a running game, a loaded DLL, or the cause of a stall.',
                        'Log timestamps are retained verbatim; chunk observation times are not event times.',
                        'Log tails may contain older events and private game diagnostics. Review before sharing.']
            if not samples or now - samples[0]['observed_at_ms'] < WINDOW_MS - 2 * POLL_SECONDS * 1000:
                warnings.append('Less than five minutes of status history is available; helper may have started late.')
            if not samples or samples[-1].get('status_state') != 'fresh':
                warnings.append('Latest backend status is missing, stale or unreadable; do not treat it as a live observation.')
            if not logs:
                warnings.append('No retained backend log text is available.')
            if any(item.get('skipped_bytes') for item in logs):
                warnings.append('Backend log output exceeded the per-poll byte limit; some lines were skipped.')
            if evicted_bytes:
                warnings.append('Older log chunks were evicted by the five-minute or one-MiB memory limit; this is not a complete backend log.')
            incident_id = 'incident-' + datetime.fromtimestamp(now / 1000, timezone.utc).strftime('%Y%m%d-%H%M%S-') + secrets.token_hex(6)
            document = {'schema': 1, 'id': incident_id, 'saved_at_ms': now,
                        'user_event_time_estimate_ms': now - event_ago_seconds * 1000,
                        'event_time_note': 'User estimate; default is the save time, not detected stall onset.',
                        'note': self.redact(note), 'retention_ms': WINDOW_MS,
                        'monitoring_started_ms': self.started_ms, 'warnings': warnings,
                        'log_evicted_bytes_since_monitor_start': evicted_bytes,
                        'status_samples': samples, 'backend_log_chunks': logs}
            raw = json.dumps(document, ensure_ascii=False, allow_nan=False, indent=2).encode('utf-8')
            if len(raw) > REPORT_BYTES:
                raise ValueError('Incident report exceeds its bounded size; nothing was saved')
            _plain(self.output.parent)
            self.output.mkdir(exist_ok=True)
            _plain(self.output)
            if sum(1 for p in self.output.iterdir() if ID.fullmatch(p.name)) >= MAX_INCIDENTS:
                raise ValueError('50 incident reports are already saved. Move older reports out of the incidents folder first.')
            folder = self.output / incident_id
            folder.mkdir()
            # Interrupted saves retain partial evidence; the receipt is only
            # published after both complete files have been closed.
            with (folder / 'incident.json').open('xb') as target:
                target.write(raw)
            (folder / 'README.txt').write_text(
                'WuWa VR recent diagnostics\n\n' + '\n'.join(warnings) +
                '\n\nincident.json contains status samples, selected package identity and existing backend log excerpts.\n'
                'This report is saved only on this PC. Nothing was uploaded.\n', encoding='utf-8')
            receipt = {'id': incident_id, 'folder': self.redact(str(folder)),
                       'report_url': '/api/incidents/report?id=' + incident_id,
                       'warning': '\n'.join([warnings[0], *warnings[4:]])}
            with self.lock:
                self.last_saved = receipt
            return copy.deepcopy(receipt)

    def report(self, incident_id):
        if not isinstance(incident_id, str) or not ID.fullmatch(incident_id):
            raise ValueError('Choose a saved incident')
        folder = self.output / incident_id
        _plain(self.output)
        return read_object(folder / 'incident.json', REPORT_BYTES)
