"""Offline service/HTTP regression checks. No device, game or model is opened."""
from contextlib import contextmanager
import copy
from http.client import HTTPConnection
from http.server import ThreadingHTTPServer
import importlib.util
import json
from pathlib import Path
import socket
import sys
import tempfile
import threading
import unittest
import wave


HERE = Path(__file__).resolve().parent


def module(name):
    spec = importlib.util.spec_from_file_location(name, HERE / (name + '.py'))
    value = importlib.util.module_from_spec(spec)
    sys.modules[name] = value
    spec.loader.exec_module(value)
    return value


service_module = module('wuwa_playtest_service')
player = module('wuwa_player')


class FakeVoice:
    def __init__(self, root):
        self.root = Path(root)
        self.active = self.transcribing = self.last = None
        self.notes = {}
        self.starts = 0
        self.transcribe_args = None

    def snapshot(self):
        return copy.deepcopy(dict(active=self.active, transcribing=self.transcribing, last=self.last, max_seconds=120))

    def devices(self):
        return [{'id': 0, 'name': 'Synthetic test microphone', 'selectable': True}]

    def start(self, session_id, device_id, *, consent):
        if not consent or device_id != 0:
            raise ValueError('Explicit supported microphone required')
        self.starts += 1
        self.active = {'id': 'voice-' + str(self.starts), 'session_id': session_id,
                       'status': 'recording', 'wav_file': None, 'duration_ms': 0, 'error': '', 'transcript': None}
        self.notes[(session_id, self.active['id'])] = self.active
        return copy.deepcopy(self.active)

    def note(self, session_id, note_id):
        if (session_id, note_id) not in self.notes:
            raise ValueError('Unknown voice note')
        return copy.deepcopy(self.notes[(session_id, note_id)])

    def list_notes(self, session_id):
        return [copy.deepcopy(value) for (sid, _), value in self.notes.items() if sid == session_id]

    def stop(self, note_id):
        if not self.active or self.active['id'] != note_id:
            raise ValueError('Not active')
        self.active.update(status='recorded', wav_file=self.active['session_id'] + '/' + note_id + '.wav', duration_ms=100)
        with wave.open(str(self.root / (note_id + '.wav')), 'wb') as audio:
            audio.setparams((1, 2, 16000, 0, 'NONE', 'not compressed'))
            audio.writeframes(b'\0' * 3200)
        self.last, self.active = self.active, None
        return copy.deepcopy(self.last)

    def cancel(self, note_id):
        entry = self.active or self.transcribing
        if not entry or entry['id'] != note_id:
            raise ValueError('Not active')
        entry.update(status='cancelled')
        self.last, self.active, self.transcribing = entry, None, None

    def path_for_note(self, session_id, note_id):
        self.note(session_id, note_id)
        return self.root / (note_id + '.wav')

    def transcribe(self, session_id, note_id, **kwargs):
        self.transcribe_args = kwargs
        if not kwargs['playtest_stopped'] or not kwargs['gameplay_idle']:
            raise ValueError('Stop playtest and gameplay')
        self.transcribing = self.notes[(session_id, note_id)]
        self.transcribing['status'] = 'transcribing'

    def close(self):
        self.active = self.transcribing = None


class ServiceFixture:
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='wuwa-playtest-service-')
        self.root = Path(self.temp.name)
        self.status = {'package': {'id': 'private-candidate', 'defaultBuild': 'baseline'},
                       'builds': [{'id': 'baseline', 'name': 'Known build', 'sha256': 'a' * 64}],
                       'selected': 'baseline', 'selectionMatches': True, 'openxr': {'name': 'Fixture'},
                       'gameRunning': False, 'injectorRunning': False, 'job': {'running': False},
                       'recording': {'running': False}}
        self.recording = {'recording_id': 'recording-one', 'state': 'saved'}
        self.voice = FakeVoice(self.root)
        self.service = service_module.Service(self.root, lambda: copy.deepcopy(self.status),
            lambda: copy.deepcopy(self.recording), voice=self.voice)

    def tearDown(self):
        self.service.close()
        self.temp.cleanup()

    def start(self, event_id='start-one'):
        return self.service.action('start', {'event_id': event_id, 'language': 'en'})['session']

    def voice_note(self, session):
        body = {'session_id': session['session_id'], 'item_id': 'stereo', 'device_id': 0,
                'consent': True, 'event_id': 'voice-start-one'}
        self.service.action('voice-start', body)
        return body, self.voice.active['id']


class ServiceTests(ServiceFixture, unittest.TestCase):
    def test_start_selection_is_immutable_across_retries(self):
        session = self.start()
        self.status['openxr']['name'] = 'Another runtime'
        self.status['selected'] = ''
        replay = self.start()
        self.assertEqual(session, replay)
        self.assertEqual(replay['build']['runtime'], 'Fixture')
        with self.assertRaisesRegex(ValueError, 'different start'):
            self.service.action('start', {'event_id': 'start-one', 'language': 'zh-Hans'})
        self.assertTrue(all(check['status'] == 'not_tested' for check in session['checks']))

    def test_unknown_build_cannot_create_evidence(self):
        with self.assertRaises(ValueError):
            self.service.action('start', {'event_id': 'unknown', 'build_id': 'invented'})
        self.assertEqual(self.service.state()['history'], [])

    def test_notes_results_and_video_time_survive_retry_and_finish(self):
        session = self.start()
        sid = session['session_id']
        link = {'session_id': sid, 'event_id': 'link-one'}
        self.service.action('link-recording', link)
        self.recording['recording_id'] = 'recording-two'
        self.service.action('link-recording', link)
        result = {'session_id': sid, 'item_id': 'stereo', 'status': 'fail', 'note': '<script>not code</script>',
                  'event_id': 'result-one', 'recording_id': 'recording-one', 'video_ms': 6590}
        self.service.action('result', result)
        self.service.action('result', result)
        state = self.service.action('finish', {'session_id': sid, 'event_id': 'finish-one'})
        self.assertEqual(len(state['session']['events']), 3)
        self.assertEqual(state['session']['counts']['not_tested'], len(service_module.CHECKS) - 1)
        self.service.action('result', result)
        self.service.action('link-recording', link)
        report = self.service.report(sid)
        self.assertIn('&lt;script&gt;not code&lt;/script&gt;', report)
        self.assertNotIn('<script>not code', report)
        self.assertIn('6590', report)
        with self.assertRaises(ValueError):
            self.service.action('note', {'session_id': sid, 'item_id': 'stereo', 'note': 'too late'})

    def test_invalid_result_or_unlinked_timestamp_is_not_saved(self):
        sid = self.start()['session_id']
        for extra in ({'status': 'auto_pass'}, {'status': 'pass', 'video_ms': 3},
                      {'status': 'pass', 'recording_id': '../file', 'video_ms': 3}):
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                self.service.action('result', {'session_id': sid, 'item_id': 'stereo', **extra})
        self.assertEqual(self.service.state(sid)['session']['events'], [])

    def test_voice_requires_consent_item_and_stable_request(self):
        session = self.start()
        for extra in ({'consent': False}, {'item_id': 'missing'}, {'event_id': '../bad'}):
            body = {'session_id': session['session_id'], 'item_id': 'stereo', 'device_id': 0, 'consent': True, 'event_id': 'voice-first', **extra}
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                self.service.action('voice-start', body)
        self.assertEqual(self.voice.starts, 0)
        body, note_id = self.voice_note(session)
        self.service.action('voice-start', body)
        self.assertEqual(self.voice.starts, 1)
        with self.assertRaises(ValueError):
            self.service.action('voice-start', {**body, 'device_id': 1})
        with self.assertRaisesRegex(ValueError, 'Finish the voice'):
            self.service.action('finish', {'session_id': session['session_id']})
        self.service.action('voice-stop', {'session_id': session['session_id'], 'note_id': note_id})
        self.service.action('voice-start', body)
        self.assertEqual(self.voice.starts, 1)

    def test_voice_report_contains_escaped_unverified_transcript(self):
        session = self.start()
        _, note_id = self.voice_note(session)
        self.voice.stop(note_id)
        self.voice.last['transcript'] = {'text': '<b>left eye frozen</b>', 'verified': False}
        report = self.service.report(session['session_id'])
        self.assertIn('Voice notes', report)
        self.assertIn('&lt;b&gt;left eye frozen&lt;/b&gt;', report)
        self.assertIn('unverified', report)
        self.assertEqual(self.service.state()['session']['counts']['pass'], 0)

    def test_transcription_needs_local_files_finished_session_and_idle_game(self):
        session = self.start()
        sid = session['session_id']
        _, note_id = self.voice_note(session)
        self.voice.stop(note_id)
        request = {'session_id': sid, 'note_id': note_id}
        with self.assertRaisesRegex(ValueError, 'Configure'):
            self.service.action('transcribe', request)
        with self.assertRaisesRegex(ValueError, 'existing absolute'):
            self.service.action('transcription-settings', {'whisper_cli': 'relative.exe', 'model_path': 'none'})
        for name in ('whisper-cli.exe', 'ggml-base.bin'):
            (self.root / name).write_bytes(b'fixture only; never executed')
        self.service.action('transcription-settings', {'whisper_cli': str(self.root / 'whisper-cli.exe'), 'model_path': str(self.root / 'ggml-base.bin')})
        with self.assertRaises(ValueError):
            self.service.action('transcribe', request)
        self.service.action('finish', {'session_id': sid})
        for key in ('gameRunning', 'injectorRunning'):
            self.status[key] = True
            with self.assertRaises(ValueError):
                self.service.action('transcribe', request)
            self.status[key] = False
        self.status['recording']['running'] = True
        with self.assertRaises(ValueError):
            self.service.action('transcribe', request)
        self.status['recording']['running'] = False
        self.service.action('transcribe', request)
        self.assertEqual(self.voice.transcribe_args['language'], 'en')
        self.assertIsNotNone(self.voice.transcribing)


class HttpTests(ServiceFixture, unittest.TestCase):
    @contextmanager
    def server(self):
        original = {key: getattr(player, key) for key in ('CONFIG', 'BASE_URL', 'PLAYTEST', 'JOB')}
        player.CONFIG = player.Config(app=HERE.parent, data=self.root, profile=self.root / 'profile')
        player.PLAYTEST = self.service
        player.JOB = {'running': False}
        server = ThreadingHTTPServer(('127.0.0.1', 0), player.Handler)
        player.BASE_URL = 'http://127.0.0.1:' + str(server.server_port)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            yield server
        finally:
            server.shutdown()
            server.server_close()
            thread.join(2)
            for key, value in original.items():
                setattr(player, key, value)

    def request(self, server, method, path, body=None, *, token=True, origin=True):
        headers = {'Content-Type': 'application/json'}
        if token:
            headers['X-WuWa-Token'] = player.TOKEN
        if origin:
            headers['Origin'] = player.BASE_URL
        connection = HTTPConnection('127.0.0.1', server.server_port, timeout=4)
        connection.request(method, path, None if body is None else json.dumps(body), headers)
        response = connection.getresponse()
        data = response.read()
        connection.close()
        return response.status, data, dict(response.getheaders())

    def test_authenticated_flow_and_original_audio_without_device(self):
        with self.server() as server:
            code, page, headers = self.request(server, 'GET', '/playtest', token=False)
            self.assertEqual(code, 200)
            self.assertIn(player.TOKEN.encode(), page)
            self.assertIn("frame-ancestors 'none'", headers['Content-Security-Policy'])
            for method, path, body in [('GET', '/api/playtest', None), ('POST', '/api/playtest/start', {'event_id': 'http-one'})]:
                self.assertEqual(self.request(server, method, path, body, token=False)[0], 403)
            self.assertEqual(self.request(server, 'POST', '/api/playtest/start', {}, origin=False)[0], 403)
            code, data, _ = self.request(server, 'POST', '/api/playtest/start', {'event_id': 'http-one'})
            self.assertEqual(code, 200)
            session = json.loads(data)['session']
            code, _, _ = self.request(server, 'POST', '/api/playtest/note', {'session_id': session['session_id'],
                'item_id': 'stereo', 'note': '双眼' * 2000, 'event_id': 'chinese-long'})
            self.assertEqual(code, 200)
            _, note_id = self.voice_note(session)
            self.assertEqual(self.request(server, 'POST', '/api/stop', {})[0], 400)
            self.voice.stop(note_id)
            self.voice.last.update(status='error', error='Synthetic transcription failure; WAV retained')
            url = '/api/playtest/audio?session_id=' + session['session_id'] + '&note_id=' + note_id
            self.assertEqual(self.request(server, 'GET', url, token=False)[0], 403)
            code, wav, headers = self.request(server, 'GET', url)
            self.assertEqual(code, 200)
            self.assertTrue(wav.startswith(b'RIFF'))
            self.assertEqual(headers['Content-Type'], 'audio/wav')
            self.assertEqual(self.request(server, 'GET', url.replace(note_id, '../secret'))[0], 400)

    def test_active_transcription_blocks_helper_jobs_and_stop(self):
        with self.server() as server:
            self.voice.transcribing = {'id': 'synthetic'}
            operation_calls = []
            with self.assertRaisesRegex(ValueError, 'transcription'):
                player.begin_job('launch', 'Never run', lambda: operation_calls.append(True))
            self.assertFalse(operation_calls)
            self.assertEqual(self.request(server, 'POST', '/api/stop', {})[0], 400)

    def test_language_save_emits_exactly_one_http_response(self):
        # A reply inside post_action formerly returned None, causing do_POST to
        # append a second 404 response after the successful language JSON.
        with self.server() as server:
            payload = json.dumps({'language': 'zh-Hans'}).encode()
            headers = (f'POST /api/language HTTP/1.1\r\nHost: 127.0.0.1:{server.server_port}\r\n'
                       f'Origin: {player.BASE_URL}\r\nX-WuWa-Token: {player.TOKEN}\r\n'
                       f'Content-Type: application/json\r\nContent-Length: {len(payload)}\r\nConnection: close\r\n\r\n').encode()
            with socket.create_connection(('127.0.0.1', server.server_port), timeout=4) as client:
                client.sendall(headers + payload)
                chunks = []
                while True:
                    chunk = client.recv(65536)
                    if not chunk:
                        break
                    chunks.append(chunk)
            wire = b''.join(chunks)
            self.assertEqual(wire.count(b'HTTP/1.0 '), 1)
            self.assertTrue(wire.startswith(b'HTTP/1.0 200 '))
            self.assertEqual(json.loads(wire.split(b'\r\n\r\n', 1)[1])['language'], 'zh-Hans')
            self.assertEqual((self.root / 'language.txt').read_text().strip(), 'zh-Hans')


if __name__ == '__main__':
    unittest.main()
