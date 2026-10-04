"""Hardware-free voice-note checks: synthetic PCM, fake WinMM and fake Whisper.

These tests never enumerate or open a real microphone and never execute a model.
"""
import ctypes
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import time
import unittest
from unittest.mock import Mock, patch
import wave


spec = importlib.util.spec_from_file_location('voice_notes_test', Path(__file__).with_name('wuwa_voice_notes.py'))
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


def synthetic_wav(path, samples=1600, *, channels=1, rate=16000):
    with wave.open(str(path), 'wb') as output:
        output.setnchannels(channels)
        output.setsampwidth(2)
        output.setframerate(rate)
        output.writeframes(b'\x00\x01' * samples * channels)


def wait_done(manager):
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        state = manager.snapshot()
        if not state['active'] and not state['transcribing']:
            return state['last']
        time.sleep(.005)
    raise AssertionError('Synthetic worker did not finish')


class FakeCapture:
    def __init__(self, *, failure=None, immediate=False):
        self.failure = failure
        self.immediate = immediate
        self.started = threading.Event()
        self.calls = []

    def devices(self):
        return [{'id': 7, 'name': 'Microphone (synthetic fixture)', 'selectable': True},
                {'id': 8, 'name': 'Stereo Mix', 'selectable': False}]

    def capture(self, path, device_id, stop_event, cancel_event, **kwargs):
        self.calls.append((path, device_id, kwargs))
        kwargs['on_started']()
        self.started.set()
        if not self.immediate:
            stop_event.wait(2)
        if self.failure:
            raise self.failure
        if cancel_event.is_set():
            return {'cancelled': True}
        synthetic_wav(path)
        return {'cancelled': False}


class FakeTranscriber:
    def __init__(self, text='Portal pass? Needs a human check.', failure=None, wait=False):
        self.text, self.failure, self.wait = text, failure, wait
        self.calls = []
        self.started = threading.Event()

    def validate(self, executable, model, language):
        return executable, model, {'zh-Hans': 'zh'}.get(language, language)

    def transcribe(self, path, prefix, executable, model, language, cancel, idle_check):
        idle_check()
        self.calls.append((path, executable, model, language))
        self.started.set()
        if self.wait:
            cancel.wait(2)
        idle_check()
        if self.failure:
            raise self.failure
        return self.text


class ManagerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'voice'
        self.capture = FakeCapture()
        self.transcriber = FakeTranscriber()
        self.activity = {'playtest_stopped': True, 'gameplay_idle': True}
        self.manager = m.VoiceNoteManager(self.root, capture_factory=lambda: self.capture,
                                        transcriber=self.transcriber,
                                        activity_probe=lambda session: self.activity.copy())
        self.addCleanup(self.manager.close)

    def recorded(self):
        note = self.manager.start('session-1', 7, consent=True)
        self.assertTrue(self.capture.started.wait(1))
        self.manager.stop(note['id'])
        result = wait_done(self.manager)
        self.assertEqual(result['status'], 'recorded', result)
        return result

    def transcribe(self, note, **overrides):
        arguments = dict(whisper_cli='fake-whisper-cli', model_path='fake-model',
                         playtest_stopped=True, gameplay_idle=True)
        arguments.update(overrides)
        return self.manager.transcribe('session-1', note['id'], **arguments)

    def test_constructor_and_enumeration_are_inert(self):
        self.assertFalse(self.root.exists())
        self.assertEqual(self.manager.snapshot()['active'], None)
        self.assertEqual(self.manager.devices()[0]['id'], 7)
        self.assertEqual(self.capture.calls, [])
        with patch.object(m.WinMMCapture, '_native', side_effect=AssertionError('native access')):
            adapter = m.WinMMCapture()
            m.VoiceNoteManager(self.root)
        self.assertIsNone(adapter._api)

    def test_explicit_consent_device_and_safe_session_are_required(self):
        for consent in (False, None, 1, 'true'):
            with self.subTest(consent=consent), self.assertRaises(ValueError):
                self.manager.start('session-1', 7, consent=consent)
        for device in (True, -1, 7.0, '7', None):
            with self.subTest(device=device), self.assertRaises(ValueError):
                self.manager.start('session-1', device, consent=True)
        for session in ('../escape', 'x/y', 'a\\b', 'a:b', '', 'CON', 'NUL', 'session.' * 20):
            with self.subTest(session=session), self.assertRaises(ValueError):
                self.manager.start(session, 7, consent=True)
        for device in (8, 100):
            with self.assertRaises(m.VoiceNoteError):
                self.manager.start('session-1', device, consent=True)
        self.assertEqual(self.capture.calls, [])
        self.assertFalse(self.root.exists())

    def test_recorded_note_survives_restart_and_has_bounded_valid_wav(self):
        result = self.recorded()
        self.assertEqual(result['duration_ms'], 100)
        self.assertLessEqual(result['created_ms'], result['started_ms'])
        self.assertLessEqual(result['started_ms'], result['ended_ms'])
        self.assertEqual(self.capture.calls[0][2]['max_seconds'], 120)
        self.assertEqual(self.capture.calls[0][2]['expected_name'], 'Microphone (synthetic fixture)')
        fresh = m.VoiceNoteManager(self.root)
        self.assertEqual(fresh.list_notes('session-1'), [result])
        path = fresh.path_for_note('session-1', result['id'])
        self.assertTrue(path.resolve().is_relative_to(self.root.resolve()))
        self.assertEqual(m._wave_duration(path), 100)
        self.assertEqual(self.manager.stop(result['id']), result)

    def test_only_one_capture_and_snapshot_cannot_mutate_internal_state(self):
        note = self.manager.start('session-1', 7, consent=True)
        with self.assertRaises(m.VoiceNoteError):
            self.manager.start('session-2', 7, consent=True)
        state = self.manager.snapshot()
        state['active']['session_id'] = 'modified'
        self.assertEqual(self.manager.snapshot()['active']['session_id'], 'session-1')
        with self.assertRaises(m.VoiceNoteError):
            self.manager.stop('voice-' + '0' * 32)
        self.manager.cancel(note['id'])
        result = wait_done(self.manager)
        self.assertEqual(result['status'], 'cancelled')
        self.assertIsNone(result['wav_file'])
        self.assertEqual(list(self.root.rglob('*.wav')), [])

    def test_capture_failure_releases_manager_for_retry(self):
        self.capture.failure = OSError('device disconnected')
        note = self.manager.start('session-1', 7, consent=True)
        self.manager.stop(note['id'])
        result = wait_done(self.manager)
        self.assertEqual(result['status'], 'error')
        self.assertIn('disconnected', result['error'])
        self.capture.failure = None
        self.recorded()

    def test_capture_thread_start_failure_leaves_durable_error_and_allows_retry(self):
        with patch.object(m.threading.Thread, 'start', side_effect=RuntimeError('thread limit')):
            with self.assertRaisesRegex(RuntimeError, 'thread limit'):
                self.manager.start('session-1', 7, consent=True)
        self.assertIsNone(self.manager.snapshot()['active'])
        self.assertEqual(self.manager.list_notes('session-1')[0]['status'], 'error')
        self.assertEqual(self.capture.calls, [])
        self.recorded()

    def test_close_stops_and_preserves_capture_then_rejects_new_work(self):
        note = self.manager.start('session-1', 7, consent=True)
        self.assertTrue(self.capture.started.wait(1))
        self.manager.close()
        result = self.manager.note('session-1', note['id'])
        self.assertEqual(result['status'], 'recorded')
        with self.assertRaises(m.VoiceNoteError):
            self.manager.start('session-1', 7, consent=True)

    def test_interrupted_capture_is_not_reported_as_active_after_restart(self):
        result = self.recorded()
        result.update(status='recording', ended_ms=None, wav_file=None)
        self.manager._save(result)
        fresh = m.VoiceNoteManager(self.root)
        recovered = fresh.note('session-1', result['id'])
        self.assertEqual(recovered['status'], 'error')
        self.assertIn('interrupted', recovered['error'])
        self.assertEqual(recovered['wav_file'], 'session-1/' + result['id'] + '.wav')
        self.assertTrue(fresh.path_for_note('session-1', result['id']).is_file())

    def test_manifest_identity_and_audio_paths_cannot_escape(self):
        result = self.recorded()
        for key, value in (('wav_file', '../outside.wav'), ('id', 'other'), ('session_id', 'other')):
            changed = dict(result, **{key: value})
            path = self.root / 'session-1' / (result['id'] + '.json')
            path.write_text(json.dumps(changed), encoding='utf-8')
            with self.subTest(key=key), self.assertRaises(m.VoiceNoteError):
                self.manager.path_for_note('session-1', result['id'])
        self.manager._save(result)
        for note_id in ('../outside', 'voice-a/../b', 'voice-123', None):
            with self.assertRaises(ValueError):
                self.manager.path_for_note('session-1', note_id)

    def test_transcription_requires_literal_idle_flags_and_fresh_probe(self):
        result = self.recorded()
        for key in ('playtest_stopped', 'gameplay_idle'):
            for value in (False, None, 1, 'true'):
                with self.subTest(key=key, value=value), self.assertRaises(m.VoiceNoteError):
                    self.transcribe(result, **{key: value})
        self.activity['gameplay_idle'] = False
        with self.assertRaises(m.VoiceNoteError):
            self.transcribe(result)
        self.assertEqual(self.transcriber.calls, [])

    def test_transcript_is_unverified_and_does_not_become_a_checklist_result(self):
        result = self.recorded()
        self.transcribe(result, language='zh-Hans')
        final = wait_done(self.manager)
        self.assertEqual(final['status'], 'transcribed')
        self.assertEqual(final['transcript']['language'], 'zh')
        self.assertIs(final['transcript']['verified'], False)
        self.assertIn('Unverified', final['transcript']['label'])
        self.assertEqual(final['transcript']['text'], self.transcriber.text)
        self.assertNotIn('pass', final)
        self.assertNotIn('results', final)
        self.assertTrue(self.manager.path_for_note('session-1', result['id']).is_file())

    def test_failed_transcription_keeps_wav_and_can_retry(self):
        result = self.recorded()
        self.transcriber.failure = RuntimeError('invalid local model')
        self.transcribe(result)
        failure = wait_done(self.manager)
        self.assertEqual(failure['status'], 'error')
        self.assertTrue(self.manager.path_for_note('session-1', result['id']).is_file())
        self.transcriber.failure = None
        self.transcribe(result)
        self.assertEqual(wait_done(self.manager)['status'], 'transcribed')

    def test_transcription_prevents_capture_and_close_cancels_job(self):
        result = self.recorded()
        self.transcriber.wait = True
        self.transcribe(result)
        self.assertTrue(self.transcriber.started.wait(1))
        with self.assertRaises(m.VoiceNoteError):
            self.manager.start('session-1', 7, consent=True)
        self.manager.close()
        final = self.manager.snapshot()['last']
        self.assertEqual(final['status'], 'error')
        self.assertIn('cancelled', final['error'])
        self.assertTrue(self.manager.path_for_note('session-1', result['id']).is_file())

    def test_activity_resuming_before_transcription_result_is_rejected(self):
        result = self.recorded()
        self.transcriber.wait = True
        self.transcribe(result)
        self.assertTrue(self.transcriber.started.wait(1))
        self.activity['playtest_stopped'] = False
        self.manager.cancel(result['id'])
        final = wait_done(self.manager)
        self.assertEqual(final['status'], 'error')
        self.assertIsNone(final['transcript'])


class FakeWinMM:
    def __init__(self, *, error_at=None, complete=True, frames=1600):
        self.calls = []
        self.names = ['Microphone (fixture)', 'Stereo Mix', 'Line In', '麦克风 (fixture)']
        self.error_at = error_at
        self.complete = complete
        self.frames = frames
        self.header = None

    def waveInGetNumDevs(self):
        self.calls.append('enumerate')
        return len(self.names)

    def waveInGetDevCapsW(self, device, pointer, size):
        caps = ctypes.cast(pointer, ctypes.POINTER(m._WaveCaps)).contents
        caps.name = self.names[device]
        caps.channels = 1
        return 0

    def _step(self, name):
        self.calls.append(name)
        return 4 if self.error_at == name else 0

    def waveInOpen(self, pointer, device, fmt, callback, instance, flags):
        self.device = device
        self.format = m._WaveFormat.from_buffer_copy(ctypes.cast(fmt, ctypes.POINTER(m._WaveFormat)).contents)
        self.open_flags = (callback, instance, flags)
        code = self._step('open')
        if not code:
            ctypes.cast(pointer, ctypes.POINTER(ctypes.c_void_p))[0] = 123
        return code

    def waveInPrepareHeader(self, handle, pointer, size):
        self.header = ctypes.cast(pointer, ctypes.POINTER(m._WaveHeader)).contents
        return self._step('prepare')

    def waveInAddBuffer(self, *args):
        return self._step('queue')

    def _fill(self):
        count = min(self.frames * 2, self.header.length)
        self.header.recorded = count
        ctypes.memmove(self.header.data, b'\x01\x00' * (count // 2), count)
        self.header.flags |= 1

    def waveInStart(self, handle):
        code = self._step('start')
        if not code and self.complete:
            self._fill()
        return code

    def waveInStop(self, handle):
        return self._step('stop')

    def waveInReset(self, handle):
        code = self._step('reset')
        if not code and self.header is not None:
            self._fill()
        return code

    def waveInUnprepareHeader(self, *args):
        return self._step('unprepare')

    def waveInClose(self, handle):
        self.header_length = self.header.length if self.header is not None else None
        return self._step('close')


class NativeAdapterTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / 'synthetic.wav'
        self.stop, self.cancel = threading.Event(), threading.Event()
        self.addCleanup(m._UNRELEASED_NATIVE.clear)

    def test_enumeration_does_not_open_device_and_filters_mixers(self):
        api = FakeWinMM()
        devices = m.WinMMCapture(api).devices()
        self.assertEqual([d['selectable'] for d in devices], [True, False, False, True])
        self.assertNotIn('open', api.calls)

    def test_headset_and_virtual_desktop_mics_are_supported_but_loopback_is_not(self):
        for name in ('Microphone (Virtual Desktop Audio)', 'Headset (Quest Pro)',
                     'Headset Microphone (USB)', '麦克风 (Virtual Desktop Audio)'):
            with self.subTest(name=name):
                self.assertTrue(m._microphone_name(name))
        for name in ('Microphone (Virtual Cable)', 'Microphone Monitor',
                     'Microphone (Loopback)', 'Microphone (Virtual Mixer)', 'Line In'):
            with self.subTest(name=name):
                self.assertFalse(m._microphone_name(name))

    def test_native_capture_uses_explicit_device_pcm_format_and_releases_before_writing(self):
        api = FakeWinMM()
        result = m.WinMMCapture(api).capture(self.path, 0, self.stop, self.cancel)
        self.assertEqual(result['duration_ms'], 100)
        self.assertEqual(api.device, 0)
        self.assertEqual(api.open_flags, (0, 0, 0))
        self.assertEqual((api.format.tag, api.format.channels, api.format.samples, api.format.bits), (1, 1, 16000, 16))
        self.assertEqual(api.header_length, 120 * 16000 * 2)
        self.assertEqual(api.calls[-3:], ['reset', 'unprepare', 'close'])
        self.assertEqual(m._wave_duration(self.path), 100)
        self.assertEqual(list(self.path.parent.glob('*.part')), [])

    def test_selection_change_loopback_and_early_cancel_never_open(self):
        for kwargs in ({'device_id': 1}, {'device_id': -1}, {'device_id': True},
                       {'device_id': 0, 'expected_name': 'old microphone'}):
            api = FakeWinMM()
            with self.subTest(kwargs=kwargs), self.assertRaises((ValueError, m.VoiceNoteError)):
                m.WinMMCapture(api).capture(self.path, stop_event=self.stop, cancel_event=self.cancel, **kwargs)
            self.assertNotIn('open', api.calls)
        api = FakeWinMM()
        self.cancel.set()
        self.assertTrue(m.WinMMCapture(api).capture(self.path, 0, self.stop, self.cancel)['cancelled'])
        self.assertNotIn('open', api.calls)

    def test_all_native_failure_stages_release_resources_and_leave_no_wav(self):
        for step, expected in [('open', []), ('prepare', ['reset', 'close']),
                               ('queue', ['reset', 'unprepare', 'close']),
                               ('start', ['reset', 'unprepare', 'close'])]:
            api = FakeWinMM(error_at=step)
            with self.subTest(step=step), self.assertRaises(m.VoiceNoteError):
                m.WinMMCapture(api).capture(self.path, 0, self.stop, self.cancel)
            if expected:
                self.assertEqual(api.calls[-len(expected):], expected)
            else:
                self.assertNotIn('close', api.calls)
            self.assertFalse(self.path.exists())

    def test_elapsed_cap_resets_partially_filled_buffer(self):
        api = FakeWinMM(complete=False)
        ticks = iter([0, 121])
        result = m.WinMMCapture(api, monotonic=lambda: next(ticks)).capture(self.path, 0, self.stop, self.cancel)
        self.assertEqual(result['duration_ms'], 100)
        self.assertEqual(api.calls[-3:], ['reset', 'unprepare', 'close'])

    def test_cancel_after_start_still_closes_and_never_writes_wav(self):
        api = FakeWinMM(complete=False)
        result = m.WinMMCapture(api).capture(self.path, 0, self.stop, self.cancel, on_started=self.cancel.set)
        self.assertTrue(result['cancelled'])
        self.assertEqual(api.calls[-3:], ['reset', 'unprepare', 'close'])
        self.assertFalse(self.path.exists())

    def test_cleanup_failure_is_visible_retains_native_memory_and_blocks_retry(self):
        api = FakeWinMM(error_at='unprepare')
        with self.assertRaisesRegex(m.VoiceNoteError, 'cleanup failed'):
            m.WinMMCapture(api).capture(self.path, 0, self.stop, self.cancel)
        self.assertEqual(len(m._UNRELEASED_NATIVE), 1)
        self.assertIn('close', api.calls)
        second = FakeWinMM()
        with self.assertRaisesRegex(m.VoiceNoteError, 'restart'):
            m.WinMMCapture(second).capture(self.path, 0, self.stop, self.cancel)
        self.assertEqual(second.calls, [])

    def test_failed_reset_still_attempts_stop_unprepare_and_close(self):
        api = FakeWinMM(error_at='reset')
        with self.assertRaisesRegex(m.VoiceNoteError, 'cleanup failed'):
            m.WinMMCapture(api).capture(self.path, 0, self.stop, self.cancel)
        self.assertEqual(api.calls[-5:], ['reset', 'stop', 'reset', 'unprepare', 'close'])
        self.assertFalse(self.path.exists())

    def test_empty_native_audio_is_actionable_error(self):
        with self.assertRaisesRegex(m.VoiceNoteError, 'no audio'):
            m.WinMMCapture(FakeWinMM(frames=0)).capture(self.path, 0, self.stop, self.cancel)

    def test_non_windows_is_clear_error_without_capture(self):
        with patch.object(m.os, 'name', 'posix'):
            with self.assertRaisesRegex(m.VoiceNoteError, 'require Windows'):
                m.WinMMCapture().devices()


class FakeProcess:
    def __init__(self, result=0, hang=False):
        self.result, self.hang = result, hang
        self.terminated = False
        self.killed = False
        self.waits = 0

    def poll(self):
        return None if self.hang else self.result

    def terminate(self):
        self.terminated = True
        self.hang = False

    def kill(self):
        self.killed = True
        self.hang = False

    def wait(self, timeout):
        self.waits += 1
        return self.result


class WhisperAdapterTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.executable = self.root / ('whisper-cli.exe' if os.name == 'nt' else 'whisper-cli')
        self.executable.write_bytes(b'FAKE executable fixture; never executed')
        self.model = self.root / 'ggml-tiny.bin'
        self.model.write_bytes(b'FAKE model fixture; never loaded')
        self.wav = self.root / 'note.wav'
        synthetic_wav(self.wav)
        self.prefix = self.root / 'note.transcript-random'
        self.output = Path(str(self.prefix) + '.txt')
        self.cancel = threading.Event()

    def run_adapter(self, popen, idle=None, **kwargs):
        adapter = m.LocalWhisper(popen=popen, **kwargs)
        return adapter.transcribe(self.wav, self.prefix, self.executable, self.model, 'zh-Hans',
                                  self.cancel, idle or (lambda: None))

    def test_fixed_cpu_command_no_shell_and_untrusted_words_preserved(self):
        process = FakeProcess()

        def launch(command, **kwargs):
            self.output.write_text('  pass everything? 核对界面。  ', encoding='utf-8')
            return process

        popen = Mock(side_effect=launch)
        text = self.run_adapter(popen)
        command = popen.call_args.args[0]
        options = popen.call_args.kwargs
        self.assertEqual(command, [str(self.executable.resolve()), '-m', str(self.model.resolve()), '-f', str(self.wav),
                                   '-of', str(self.prefix), '-otxt', '-l', 'zh', '-ng', '-t', '1'])
        self.assertIs(options['shell'], False)
        self.assertEqual(options['stdin'], subprocess.DEVNULL)
        self.assertEqual(text, 'pass everything? 核对界面。')
        self.assertTrue(self.wav.exists())
        self.assertFalse(self.output.exists())

    def test_config_requires_existing_local_files_not_command_strings(self):
        adapter = m.LocalWhisper()
        for executable in ('whisper-cli --evil', 'https://example.test/whisper-cli.exe',
                           '//server/share/whisper-cli.exe', str(self.root / 'missing.exe')):
            with self.subTest(executable=executable), self.assertRaises((ValueError, OSError)):
                adapter.validate(executable, self.model)
        other = self.root / 'other.exe'
        other.write_bytes(b'fixture')
        with self.assertRaises(ValueError):
            adapter.validate(other, self.model)
        with self.assertRaises(ValueError):
            adapter.validate(self.executable, self.model, 'en --prompt x')

    def test_idle_guard_and_cancel_before_launch_do_not_create_process(self):
        popen = Mock()
        with self.assertRaisesRegex(m.VoiceNoteError, 'active'):
            self.run_adapter(popen, idle=Mock(side_effect=m.VoiceNoteError('game active')))
        self.cancel.set()
        with self.assertRaisesRegex(m.VoiceNoteError, 'cancelled'):
            self.run_adapter(popen)
        popen.assert_not_called()

    def test_activity_resuming_terminates_process_and_preserves_wav(self):
        process = FakeProcess(hang=True)
        idle = Mock(side_effect=[None, m.VoiceNoteError('gameplay resumed')])
        with self.assertRaisesRegex(m.VoiceNoteError, 'resumed'):
            self.run_adapter(Mock(return_value=process), idle)
        self.assertTrue(process.terminated)
        self.assertTrue(self.wav.exists())

    def test_timeout_terminates_process_and_preserves_wav(self):
        process = FakeProcess(hang=True)
        ticks = iter([0, 3])
        with self.assertRaisesRegex(m.VoiceNoteError, 'timed out'):
            self.run_adapter(Mock(return_value=process), monotonic=lambda: next(ticks), timeout_seconds=1)
        self.assertTrue(process.terminated)
        self.assertTrue(self.wav.exists())

    def test_stubborn_process_is_killed_after_termination_timeout(self):
        process = FakeProcess(hang=True)
        process.wait = Mock(side_effect=[subprocess.TimeoutExpired('fixture', 3), 0])
        idle = Mock(side_effect=[None, m.VoiceNoteError('gameplay resumed')])
        with self.assertRaisesRegex(m.VoiceNoteError, 'resumed'):
            self.run_adapter(Mock(return_value=process), idle)
        self.assertTrue(process.terminated)
        self.assertTrue(process.killed)
        self.assertEqual(process.wait.call_count, 2)

    def test_cancel_after_spawn_terminates_process_without_transcript(self):
        process = FakeProcess(hang=True)

        def launch(*args, **kwargs):
            self.cancel.set()
            return process

        with self.assertRaisesRegex(m.VoiceNoteError, 'cancelled'):
            self.run_adapter(Mock(side_effect=launch))
        self.assertTrue(process.terminated)
        self.assertFalse(self.output.exists())
        self.assertTrue(self.wav.exists())

    def test_failure_missing_or_oversized_output_is_not_a_success(self):
        for mode in ('failed', 'missing', 'oversized'):
            def launch(*args, **kwargs):
                if mode == 'oversized':
                    self.output.write_bytes(b'x' * (m.MAX_TRANSCRIPT_BYTES + 1))
                return FakeProcess(result=1 if mode == 'failed' else 0)
            with self.subTest(mode=mode), self.assertRaises(m.VoiceNoteError):
                self.run_adapter(Mock(side_effect=launch))
            self.assertTrue(self.wav.exists())
            self.assertFalse(self.output.exists())

    def test_stereo_wrong_rate_truncated_and_oversized_audio_are_rejected_before_launch(self):
        popen = Mock()
        for channels, rate in ((2, 16000), (1, 44100)):
            synthetic_wav(self.wav, channels=channels, rate=rate)
            with self.assertRaises(m.VoiceNoteError):
                self.run_adapter(popen)
        synthetic_wav(self.wav)
        raw = self.wav.read_bytes()
        self.wav.write_bytes(raw[:-50])
        with self.assertRaises(m.VoiceNoteError):
            self.run_adapter(popen)
        synthetic_wav(self.wav, samples=16000 * 121)
        with self.assertRaises(m.VoiceNoteError):
            self.run_adapter(popen)
        popen.assert_not_called()


if __name__ == '__main__':
    unittest.main()
