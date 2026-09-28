"""Explicit, bounded controller input for developer recordings; off by default.

The backend owns cancellation and all input delivery. This client only submits a
short plan after video starts, renews its lease, and stops that same lease. It
does not install a driver, synthesize a device or move the mouse. Button plans
use the same selected physical device and native cancellation rules as axes.
"""
from __future__ import annotations

import json
import math
from pathlib import Path
import subprocess
import tempfile
import time
import uuid

AXES = ('lx', 'ly', 'rx', 'ry')
TRIGGERS = ('lt', 'rt')
BUTTONS = ('a', 'b', 'x', 'y', 'dpad_up', 'dpad_down', 'dpad_left', 'dpad_right',
           'start', 'back', 'left_shoulder', 'right_shoulder')
CONTEXTS = ('gameplay', 'game-menu')
MAX_MOVEMENT_MS = 10000  # Leaves 750 ms neutral lead/tail + 1250 ms polling margin within 12 s.


def load_plan(path: Path, user_index: int, seconds: int, video_only: bool = False):
    if type(user_index) is not int or not 0 <= user_index <= 3:
        raise ValueError('An input plan requires an explicit --user-index from 0 to 3')
    if video_only:
        raise ValueError('Input plans require camera/controller telemetry; omit --video-only')
    with path.open('rb') as source:
        raw = source.read(4097)
    if len(raw) > 4096:
        raise ValueError('Input plan exceeds 4096 bytes')
    def unique_fields(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError('Input plan contains a duplicate JSON field: ' + key)
            result[key] = value
        return result
    plan = json.loads(raw, object_pairs_hook=unique_fields)
    if (not isinstance(plan, dict) or not {'version', 'segments'} <= set(plan)
            or set(plan) - {'version', 'segments', 'context'}
            or type(plan['version']) is not int or plan['version'] != 1):
        raise ValueError('Input plan requires version 1, segments and optional context only')
    context = plan.get('context', 'gameplay')
    if context not in CONTEXTS or not isinstance(context, str):
        raise ValueError('Input context must be gameplay or game-menu')
    segments = plan['segments']
    if not isinstance(segments, list) or not 1 <= len(segments) <= 8:
        raise ValueError('Choose 1..8 input segments')
    normalized = []
    for segment in segments:
        if (not isinstance(segment, dict) or 'duration_ms' not in segment
                or set(segment) - {'duration_ms', *AXES, *TRIGGERS, 'buttons'}
                or type(segment['duration_ms']) is not int or segment['duration_ms'] <= 0
                or any(type(segment.get(axis, 0)) is not int or abs(segment.get(axis, 0)) > 16384 for axis in AXES)):
            raise ValueError('Each segment needs positive duration_ms and optional integer lx/ly/rx/ry in -16384..16384')
        if any(type(segment[axis]) is not int or not 0 <= segment[axis] <= 255
               for axis in TRIGGERS if axis in segment):
            raise ValueError('Optional lt/rt trigger axes must be integer bytes in 0..255')
        buttons = segment.get('buttons', [])
        if (not isinstance(buttons, list) or len(buttons) > len(BUTTONS)
                or any(not isinstance(button, str) or button not in BUTTONS for button in buttons)
                or len(set(buttons)) != len(buttons)):
            raise ValueError('Segment buttons must be a list of distinct supported lowercase controller button names')
        item = dict(duration_ms=segment['duration_ms'], **{axis: segment.get(axis, 0) for axis in AXES})
        # Presence, including an explicit zero, opts the whole native lease into
        # owning both triggers. Preserve it so zero means release, not legacy
        # passthrough of subthreshold physical trigger pressure.
        item.update({axis: segment[axis] for axis in TRIGGERS if axis in segment})
        if context == 'game-menu' and any(item.get(axis, 0) for axis in (*AXES, *TRIGGERS)):
            raise ValueError('game-menu input segments require all axes and triggers to be zero')
        # An empty list means release. Omit its wire field so legacy neutral /
        # axes-only plans still have the original five-field segment shape.
        if buttons:
            item['buttons'] = [button for button in BUTTONS if button in buttons]
        normalized.append(item)
    segments = normalized
    duration = sum(segment['duration_ms'] for segment in segments)
    if duration > MAX_MOVEMENT_MS:
        raise ValueError('Input segments exceed 10000 ms; neutral lead/tail and polling margin also count toward the 12 s limit')
    if seconds < math.ceil(duration / 1000) + 12:
        raise ValueError('Recording needs the input duration plus 12 seconds for video startup and neutral settling')
    return dict(version=1, context=context, user_index=user_index, segments=segments, duration_ms=duration)


def needs_button_context(plan):
    return bool(plan and (plan.get('context', 'gameplay') != 'gameplay'
                         or any(segment.get('buttons') for segment in plan['segments'])))


def needs_trigger_axes(plan):
    return bool(plan and any(axis in segment for segment in plan['segments'] for axis in TRIGGERS))


def check_backend(before, plan=None):
    state = before.get('input_sequence', {})
    if type(state.get('protocol_version')) is not int or state['protocol_version'] != 1:
        raise RuntimeError('Running backend does not support bounded input sequences; no input was requested')
    if state.get('active'):
        raise RuntimeError('Another input sequence is active; no input was requested')
    if (needs_button_context(plan)
            and (type(state.get('button_context_version')) is not int or state['button_context_version'] != 1)):
        raise RuntimeError('Running backend does not support controller buttons and menu contexts; no input was requested')
    if (needs_trigger_axes(plan)
            and (type(state.get('trigger_axis_version')) is not int or state['trigger_axis_version'] != 1)):
        raise RuntimeError('Running backend does not support controller trigger axes; no input was requested')


def latest_frame(folder):
    """Read only the bounded tail; a partially flushed JSON line is not a frame."""
    path = folder / 'frames.jsonl'
    try:
        with path.open('rb') as source:
            source.seek(0, 2)
            source.seek(max(0, source.tell() - 8192))
            data = source.read(8192)
    except FileNotFoundError:
        return None
    for line in reversed(data.split(b'\n')[:-1]):
        try:
            row = json.loads(line)
        except (ValueError, UnicodeError):
            continue
        if (isinstance(row, dict) and type(row.get('capture_end_ms')) is int
                and type(row.get('capture_begin_ms')) is int
                and row['capture_begin_ms'] <= row['capture_end_ms']):
            return row
    return None


class Lease:
    def __init__(self, client, plan, purpose='playtest'):
        self.client, self.plan = client, plan
        self.purpose = purpose
        self.id = uuid.uuid4().hex
        self.attempted = False
        self.done = False
        self.observed_sent = False
        self.events = []
        self.last = None

    def observe(self, action, reply):
        state = reply.get('input_sequence', {})
        if state.get('id') != self.id:
            raise RuntimeError('Input sequence response belongs to another lease')
        self.last = state
        self.observed_sent |= type(state.get('generated_polls')) is int and state['generated_polls'] > 0
        self.events.append(dict(unix_ms=int(time.time() * 1000), action=action, state=state))
        if ((needs_button_context(self.plan) or needs_trigger_axes(self.plan) or 'context' in state)
                and state.get('context') != self.plan.get('context', 'gameplay')):
            raise RuntimeError('Input sequence response has a different or missing input context')
        if not state.get('active'):
            self.done = (state.get('phase') == 'completed' and state.get('tail_neutral_observed') is True
                         and state.get('segment_mask') == (1 << len(self.plan['segments'])) - 1)
            if not self.done:
                raise RuntimeError('Input sequence stopped: ' + str(state.get('reason', state.get('phase', 'unknown'))))

    def begin(self):
        self.attempted = True  # Even a lost response may have started our bounded lease.
        reply = self.client.request('input_sequence', action='begin', timeout=2,
                                    request_id=self.id, user_index=self.plan['user_index'],
                                    context=self.plan.get('context', 'gameplay'), segments=self.plan['segments'])
        self.observe('begin', reply)

    def heartbeat(self):
        self.observe('heartbeat', self.client.request('input_sequence', action='heartbeat',
                     lease_id=self.id, timeout=2))

    def stop(self):
        if not self.attempted:
            return
        try:
            reply = self.client.request('input_sequence', action='stop', lease_id=self.id, timeout=2)
            state = reply.get('input_sequence', {})
            if state.get('id') == self.id:
                self.observed_sent |= type(state.get('generated_polls')) is int and state['generated_polls'] > 0
            self.events.append(dict(unix_ms=int(time.time() * 1000), action='stop', response=reply))
        except (OSError, RuntimeError, TimeoutError) as exc:
            self.events.append(dict(unix_ms=int(time.time() * 1000), action='stop', error=str(exc),
                note='Native heartbeat expiry still bounds the lease; release was not independently confirmed.'))

    def report(self, error):
        return dict(version=1, id=self.id, plan=self.plan, input_requested=self.attempted,
            purpose=self.purpose, input_context=self.plan.get('context', 'gameplay'),
            input_sent=True if self.observed_sent else None if self.attempted else False,
            completed=self.done, last_observation=self.last, events=self.events, error=error,
            note='Generated polls prove synthetic input reached the hook before the normal mod chain, not final game consumption. '
                 'Compare separate raw, synthetic and delivered motion observations; movement, menu changes and abilities require visual confirmation.')


def save_report(folder, receipt):
    if not folder.is_dir():
        return
    (folder / 'input-sequence.json').write_text(json.dumps(receipt, indent=2), encoding='utf-8')
    path = folder / 'recording.json'
    if path.is_file():
        original = json.loads(path.read_text(encoding='utf-8'))
        # Native recorder's claim covers only itself. Preserve that fact, then
        # report the full session accurately instead of calling it passive.
        original['capture_helper_input_sent'] = original.get('input_sent')
        original['input_sent'] = receipt['input_sent']
        original['input_sequence'] = 'input-sequence.json'
        original['purpose'] = receipt.get('purpose', 'playtest')
        original['input_context'] = receipt.get('input_context', 'gameplay')
        path.write_text(json.dumps(original, indent=2), encoding='utf-8')


def capture(client, args, command, plan):
    """Own one encoder and one input lease under the caller's existing IPC lock."""
    lease = Lease(client, plan, purpose='playtest')
    error = None
    process = None
    started = time.monotonic()
    requested_at = int(time.time() * 1000)
    next_heartbeat = 0.0
    sequence_deadline = None
    try:
        with tempfile.TemporaryFile() as log:
            process = subprocess.Popen(command, stdout=log, stderr=log,
                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            try:
                while process.poll() is None:
                    now = time.monotonic()
                    if args.stop_file.exists():
                        raise RuntimeError('Recording/input sequence cancelled by stop file')
                    if now - started > args.seconds + 45:
                        raise RuntimeError('Video recorder timed out')
                    if not lease.done:
                        client.assert_live()
                        client.assert_focus()
                        frame = latest_frame(args.output)
                        age = time.time() * 1000 - frame['capture_end_ms'] if frame else None
                        fresh = frame and frame['capture_begin_ms'] >= requested_at and 0 <= age <= 2000
                        if not lease.attempted:
                            if fresh:
                                lease.begin()
                                sequence_deadline = time.monotonic() + 15
                                next_heartbeat = time.monotonic() + .5
                            elif now - started > 8:
                                raise RuntimeError('Video did not produce a fresh frame; no input was requested')
                        else:
                            if not fresh:
                                raise RuntimeError('Video capture stopped progressing; input sequence stopped')
                            if now > sequence_deadline:
                                raise RuntimeError('Input sequence did not finish within its time bound')
                            if now >= next_heartbeat:
                                lease.heartbeat()
                                next_heartbeat = time.monotonic() + .5
                    time.sleep(.05)
                log.seek(max(0, log.tell() - 32768))
                output = log.read().decode('utf-8', errors='replace').strip()
                if process.returncode:
                    raise RuntimeError(output or 'Video recording failed')
                if not lease.done:
                    raise RuntimeError('Video ended before input sequence completion was confirmed')
            finally:
                lease.stop()
                if process.poll() is None:
                    args.stop_file.touch(exist_ok=True)
                    try:
                        process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        process.terminate()
                        try:
                            process.wait(timeout=3)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait(timeout=3)
    except BaseException as exc:
        error = str(exc) or type(exc).__name__
        raise
    finally:
        save_report(args.output, lease.report(error))
