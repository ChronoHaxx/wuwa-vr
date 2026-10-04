"""Headless browser QA with real HTTP/service/store and inert hardware fixtures.

Run: python -m unittest discover -s launcher/dev -p test_playtest_page.py -v
Screenshots/results: launcher/native/work/playtest-ui-20261003
No game, microphone, system device enumeration or model process is used.
"""
import copy
from datetime import datetime, timezone
import hashlib
from http.server import ThreadingHTTPServer
import importlib.util
import json
from pathlib import Path
import secrets
import tempfile
import threading
import time
import unittest
from urllib.parse import urlparse
import wave

from playwright.sync_api import sync_playwright, expect


HERE = Path(__file__).resolve().parent
ARTIFACTS = HERE.parent / 'native/work/playtest-ui-20261003'
INJECTION = '<img src=x onerror="window.injectionRan=true"><script>window.injectionRan=true</script> Both eyes need human review.'


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, HERE / filename)
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


class FakeVoice:
    """Data-only stand-in. Never imports the production native voice module."""
    def __init__(self, root):
        self.root = root
        self.rows = {}
        self.active = self.job = self.last = None
        self.starts = self.enumerations = self.transcriptions = 0
        self.device_error = None
        self.hold_transcription = False

    def devices(self):
        self.enumerations += 1
        if self.device_error:
            raise ValueError(self.device_error)
        return [{'id': 7, 'name': 'Microphone (synthetic Quest fixture)', 'selectable': True, 'reason': ''},
                {'id': 8, 'name': 'Stereo Mix fixture', 'selectable': False, 'reason': 'Unsupported loopback'}]

    def snapshot(self):
        return copy.deepcopy({'active': self.active, 'transcribing': self.job, 'last': self.last, 'max_seconds': 120})

    def list_notes(self, session_id):
        return copy.deepcopy([row for row in self.rows.values() if row['session_id'] == session_id])

    def note(self, session_id, note_id):
        note = self.rows[note_id]
        if note['session_id'] != session_id:
            raise ValueError('Wrong synthetic session')
        return copy.deepcopy(note)

    def start(self, session_id, device_id, *, consent):
        if consent is not True or device_id != 7 or self.active or self.job:
            raise ValueError('Synthetic voice request rejected')
        self.starts += 1
        now = int(time.time() * 1000)
        row = {'version': 1, 'id': 'voice-' + secrets.token_hex(16), 'session_id': session_id,
               'device_id': device_id, 'device_name': 'Microphone (synthetic Quest fixture)',
               'status': 'recording', 'created_ms': now, 'started_ms': now,
               'ended_ms': None, 'duration_ms': 0, 'wav_file': None, 'transcript': None, 'error': ''}
        self.rows[row['id']] = self.active = row
        return copy.deepcopy(row)

    def stop(self, note_id):
        if not self.active or self.active['id'] != note_id:
            raise ValueError('No synthetic active note')
        row = self.active
        self.root.mkdir(parents=True, exist_ok=True)
        with wave.open(str(self.root / (note_id + '.wav')), 'wb') as output:
            output.setnchannels(1)
            output.setsampwidth(2)
            output.setframerate(16000)
            output.writeframes(b'\0\0' * 1600)
        row.update(status='recorded', duration_ms=100, ended_ms=int(time.time() * 1000),
                   wav_file=row['session_id'] + '/' + note_id + '.wav')
        self.last, self.active = row, None
        return copy.deepcopy(row)

    def cancel(self, note_id):
        row = self.active or self.job
        if not row or row['id'] != note_id:
            raise ValueError('No synthetic operation')
        row.update(status='error' if self.job else 'cancelled', ended_ms=int(time.time() * 1000),
                   error='Transcription cancelled; original WAV retained' if self.job else '')
        self.last, self.active, self.job = row, None, None
        return copy.deepcopy(row)

    def path_for_note(self, session_id, note_id):
        self.note(session_id, note_id)
        return self.root / (note_id + '.wav')

    def transcribe(self, session_id, note_id, **kwargs):
        if kwargs.get('playtest_stopped') is not True or kwargs.get('gameplay_idle') is not True:
            raise ValueError('Synthetic activity gate rejected transcription')
        self.transcriptions += 1
        self.note(session_id, note_id)
        row = self.rows[note_id]
        if self.hold_transcription:
            row['status'] = 'transcribing'
            self.job = row
        else:
            row.update(status='transcribed', transcript={'text': INJECTION, 'verified': False,
                       'label': 'Unverified local transcription', 'language': kwargs.get('language')})
            self.last = row
        return copy.deepcopy(row)

    def close(self):
        self.active = self.job = None


class Fixture:
    def __init__(self, directory):
        self.directory = directory
        self.helper = module('page_qa_helper_' + secrets.token_hex(4), 'wuwa_player.py')
        self.service_module = module('page_qa_service_' + secrets.token_hex(4), 'wuwa_playtest_service.py')
        self.store_module = module('page_qa_store_' + secrets.token_hex(4), 'wuwa_playtest.py')
        self.voice = FakeVoice(directory / 'synthetic-audio')
        self.status = {'selected': 'fixture-build', 'selectionMatches': True,
                       'builds': [{'id': 'fixture-build', 'name': 'Synthetic QA build', 'sha256': 'a' * 64}],
                       'package': {'id': 'browser-fixture-only', 'defaultBuild': 'fixture-build'},
                       'openxr': {'name': 'Synthetic runtime'}, 'gameRunning': False,
                       'injectorRunning': False, 'job': {'running': False}, 'recording': {'running': False}}
        self.recording = {'recording_id': 'recording-fixture-01', 'running': False}
        self.requests = []
        self.response_waiting = threading.Event()
        self.response_gate = threading.Event()
        self.delay_path = None
        self.helper.CONFIG = self.helper.Config(app=HERE.parent, data=directory / 'data', profile=directory / 'profile')
        self.helper.status = lambda: copy.deepcopy(self.status)
        self.helper.recording_snapshot = lambda: copy.deepcopy(self.recording)
        self.replace_service()
        fixture = self

        class Handler(self.helper.Handler):
            def reply(self, code, data, content_type='application/json; charset=utf-8'):
                path = urlparse(self.path).path
                fixture.requests.append({'method': self.command, 'path': path, 'status': code})
                if self.command == 'POST' and path == fixture.delay_path and code == 200:
                    fixture.response_waiting.set()
                    fixture.response_gate.wait(8)
                    fixture.delay_path = None
                return super().reply(code, data, content_type)

        self.server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.url = 'http://127.0.0.1:' + str(self.server.server_port)
        self.helper.BASE_URL = self.url
        self.server_thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.server_thread.start()

    def replace_service(self):
        # Reconstruct actual persistence/service against the same isolated disk.
        store = self.store_module.SessionStore(self.directory / 'data/playtests/sessions')
        self.service = self.service_module.Service(self.directory / 'data', self.helper.status,
                                                   self.helper.recording_snapshot, store=store, voice=self.voice)
        self.helper.PLAYTEST = self.service

    def close(self):
        self.response_gate.set()
        self.server.shutdown()
        self.server.server_close()
        self.server_thread.join(2)
        self.service.close()


class PlaytestPageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        ARTIFACTS.mkdir(parents=True, exist_ok=True)
        cls.results = []
        cls.playwright = sync_playwright().start()
        cls.browser = cls.playwright.chromium.launch(headless=True)

    @classmethod
    def tearDownClass(cls):
        cls.browser.close()
        cls.playwright.stop()
        sources = {name: hashlib.sha256((HERE / name).read_bytes()).hexdigest()
                   for name in ('wuwa-playtest.html', 'wuwa_playtest_service.py', 'wuwa_playtest.py', 'wuwa_player.py')}
        report = {'created_utc': datetime.now(timezone.utc).isoformat(), 'headless': True,
                  'browser': 'Chromium', 'real_components': ['Handler', 'Service', 'SessionStore'],
                  'fake_components': ['microphone manager', 'game/runtime status', 'recording status', 'Whisper'],
                  'physical_acceptance': 'Not performed', 'source_sha256': sources, 'tests': cls.results}
        (ARTIFACTS / 'results.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')

    def setUp(self):
        self.started = time.monotonic()
        self.source_sha256 = {name: hashlib.sha256((HERE / name).read_bytes()).hexdigest()
                              for name in ('wuwa-playtest.html', 'wuwa_playtest_service.py', 'wuwa_playtest.py', 'wuwa_player.py')}
        self.temp = tempfile.TemporaryDirectory(prefix='fixture-', dir=ARTIFACTS)
        self.fixture = Fixture(Path(self.temp.name))
        self.context = self.browser.new_context(viewport={'width': 1440, 'height': 1080}, accept_downloads=True)
        self.external_requests = []

        def local_only(route):
            if route.request.url.startswith(self.fixture.url + '/'):
                route.continue_()
            else:
                self.external_requests.append(route.request.url)
                route.abort()

        self.context.route('**/*', local_only)
        self.page = self.context.new_page()
        self.page.set_default_timeout(5000)
        self.errors = []
        self.page.on('pageerror', lambda error: self.errors.append(str(error)))
        self.page.goto(self.fixture.url + '/playtest')
        expect(self.page.locator('#connection')).to_contain_text('Connected locally')

    def tearDown(self):
        failed = any(test is self and traceback for test, traceback in
                     self._outcome.result.errors + self._outcome.result.failures)
        screenshots = []
        if failed:
            screenshot = self._testMethodName + '-failure.png'
            self.page.screenshot(path=str(ARTIFACTS / screenshot), full_page=True)
            screenshots.append(screenshot)
        self.results.append({'test': self._testMethodName, 'passed': not failed,
                             'duration_seconds': round(time.monotonic() - self.started, 3),
                             'source_sha256_at_start': self.source_sha256,
                             'page_errors': self.errors, 'external_requests_blocked': self.external_requests,
                             'requests': self.fixture.requests, 'failure_screenshots': screenshots})
        self.context.close()
        self.fixture.close()
        self.temp.cleanup()

    def start(self):
        self.page.locator('#start').click()
        expect(self.page.locator('#finish')).to_be_enabled()
        expect(self.page.locator('#checks article')).to_have_count(len(self.fixture.service_module.CHECKS))
        return self.fixture.service.store.current()['session_id']

    def refresh(self):
        with self.page.expect_response(lambda response: response.request.method == 'GET' and '/api/playtest' in response.url):
            self.page.locator('#refresh').click()
        expect(self.page.locator('#refresh')).to_be_enabled()

    def finish(self):
        self.page.once('dialog', lambda dialog: dialog.accept())
        self.page.locator('#finish').click()
        expect(self.page.locator('#finish')).to_be_disabled()
        expect(self.page.locator('#sessionSummary')).to_contain_text('Finished')

    def screenshot(self, name):
        self.page.screenshot(path=str(ARTIFACTS / (name + '.png')), full_page=True)

    def no_horizontal_overflow(self):
        geometry = self.page.evaluate('''() => ({width:innerWidth,document:document.documentElement.scrollWidth,
          bad:[...document.querySelectorAll('button,input,select,textarea,.panel')]
            .filter(e=>e.getBoundingClientRect().width && (e.getBoundingClientRect().left < -1 || e.getBoundingClientRect().right > innerWidth+1))
            .map(e=>({tag:e.tagName,id:e.id,left:e.getBoundingClientRect().left,right:e.getBoundingClientRect().right}))})''')
        self.assertLessEqual(geometry['document'], geometry['width'] + 1, geometry)
        self.assertEqual(geometry['bad'], [], geometry)

    def test_complete_observation_recording_report_and_history_flow(self):
        first = self.start()
        expect(self.page.locator('#buildIdentity')).to_contain_text('Synthetic QA build')
        expect(self.page.locator('#checks .badge.not_tested')).to_have_count(len(self.fixture.service_module.CHECKS))
        self.page.locator('#checks article').nth(0).get_by_role('button', name='Pass', exact=True).click()
        expect(self.page.locator('#checks .badge.pass')).to_have_count(1)
        self.page.locator('#checks article').nth(1).get_by_role('button', name='Fail', exact=True).click()
        expect(self.page.locator('#checks .badge.fail')).to_have_count(1)
        self.page.locator('#linkRecording').click()
        expect(self.page.locator('#recordings')).to_contain_text('recording-fixture-01')
        self.page.locator('#item').select_option('stereo')
        self.page.locator('#note').fill(INJECTION)
        self.page.get_by_text('Attach a video timestamp (optional)', exact=True).click()
        self.page.locator('#noteRecording').select_option('recording-fixture-01')
        self.page.locator('#videoSeconds').fill('6.59')
        self.page.locator('#saveNote').click()
        expect(self.page.locator('#note')).to_have_value('')
        expect(self.page.locator('#notes')).to_contain_text(INJECTION)
        expect(self.page.locator('#notes')).to_contain_text('recording-fixture-01 · 0:06')
        self.assertIsNone(self.page.evaluate('window.injectionRan'))
        self.assertEqual(self.page.locator('#notes script, #notes img').count(), 0)
        state = self.fixture.service.store.snapshot(first)
        event = next(event for event in state['events'] if event.get('note') == INJECTION)
        self.assertEqual(event['video_ms'], 6590)
        self.assertEqual(event['recording_id'], 'recording-fixture-01')
        self.no_horizontal_overflow()
        self.screenshot('desktop-en-observation')
        self.finish()
        expect(self.page.locator('#saveNote')).to_be_disabled()
        expect(self.page.locator('#checks button')).to_have_count(4 * len(self.fixture.service_module.CHECKS))
        self.assertTrue(all(self.page.locator('#checks button').nth(i).is_disabled() for i in range(20)))
        with self.page.expect_download() as downloaded:
            self.page.locator('#report').click()
        report_path = ARTIFACTS / 'synthetic-playtest-report.html'
        downloaded.value.save_as(str(report_path))
        report = report_path.read_text(encoding='utf-8')
        self.assertIn('Synthetic QA build', report)
        self.assertIn('&lt;img', report)
        self.assertNotIn('<script>window.injectionRan', report)
        self.assertIn('recording-fixture-01', report)
        expect(self.page.locator('#message')).to_contain_text('Report downloaded')
        second = self.start()
        self.assertNotEqual(first, second)
        expect(self.page.locator('#checks .badge.not_tested')).to_have_count(len(self.fixture.service_module.CHECKS))
        self.page.locator('#history').select_option(first)
        expect(self.page.locator('#sessionSummary')).to_contain_text('Finished')
        expect(self.page.locator('#checks .badge.pass')).to_have_count(1)
        self.page.locator('#history').select_option(second)
        expect(self.page.locator('#sessionSummary')).to_contain_text('In progress')
        expect(self.page.locator('#checks .badge.not_tested')).to_have_count(len(self.fixture.service_module.CHECKS))
        self.assertEqual(self.errors, [])
        self.assertEqual(self.external_requests, [])

    def test_english_chinese_desktop_and_mobile_layout_with_persisted_language(self):
        self.start()
        for language, title in [('en', 'Test once. Keep the evidence.'), ('zh-Hans', '认真测试，保留证据。')]:
            self.page.locator('#language').select_option(language)
            expect(self.page.locator('h1')).to_have_text(title)
            self.page.locator('#voiceTools summary').click()
            self.page.locator('#devices').click()
            expect(self.page.locator('#microphone option')).to_have_count(3)
            self.page.locator('#voiceTools summary').click()
            for width, label in [(1440, 'desktop'), (390, 'mobile'), (320, 'narrow')]:
                self.page.set_viewport_size({'width': width, 'height': 1000})
                self.no_horizontal_overflow()
                self.screenshot(label + '-' + language)
        self.page.reload()
        expect(self.page.locator('html')).to_have_attribute('lang', 'zh-Hans')
        expect(self.page.locator('#sessionSummary')).to_contain_text('进行中')
        expect(self.page.locator('#checks h3').first).to_have_text('启动与退出')
        self.assertEqual(self.errors, [])
        self.assertEqual(self.fixture.voice.starts, 0)

    def test_voice_explicit_consent_busy_controls_cancel_audio_and_transcript(self):
        self.start()
        self.assertEqual(self.fixture.voice.enumerations, 0)
        self.assertEqual(self.fixture.voice.starts, 0)
        self.page.locator('#voiceTools summary').click()
        self.page.locator('#devices').click()
        expect(self.page.locator('#microphone option')).to_have_count(3)
        self.assertTrue(self.page.locator('#microphone option[value="8"]').is_disabled())
        self.page.locator('#microphone').select_option('7')
        expect(self.page.locator('#voiceStart')).to_be_disabled()
        self.page.locator('#consent').check()
        expect(self.page.locator('#voiceStart')).to_be_enabled()
        self.page.locator('#voiceStart').click()
        expect(self.page.locator('#consent')).not_to_be_checked()
        expect(self.page.locator('#voiceStatus')).to_contain_text('Recording microphone')
        expect(self.page.locator('#finish')).to_be_disabled()
        expect(self.page.locator('#devices')).to_be_disabled()
        expect(self.page.locator('#voiceStart')).to_be_disabled()
        expect(self.page.locator('#voiceStop')).to_be_enabled()
        self.screenshot('desktop-voice-recording-fixture')
        self.page.locator('#voiceCancel').click()
        expect(self.page.locator('#voiceStatus')).to_contain_text('Capture discarded')
        expect(self.page.locator('#voiceStart')).to_be_disabled()
        self.page.locator('#consent').check()
        self.page.locator('#voiceStart').click()
        expect(self.page.locator('#voiceStop')).to_be_enabled()
        self.page.locator('#voiceStop').click()
        expect(self.page.locator('#voiceStatus')).to_contain_text('Voice note saved')
        expect(self.page.locator('#voiceStart')).to_be_disabled()
        with self.page.expect_download() as audio:
            self.page.get_by_role('button', name='Download WAV', exact=True).click()
        downloaded = Path(audio.value.path())
        with wave.open(str(downloaded), 'rb') as source:
            self.assertEqual((source.getnchannels(), source.getframerate(), source.getnframes()), (1, 16000, 1600))
        expect(self.page.get_by_role('button', name='Transcribe', exact=True)).to_be_disabled()
        self.finish()
        self.page.get_by_text('Local transcription after the session', exact=True).click()
        cli = self.fixture.directory / 'whisper-cli.exe'
        model = self.fixture.directory / 'ggml-fixture.bin'
        cli.write_bytes(b'Never executed; browser fixture')
        model.write_bytes(b'Never loaded; browser fixture')
        self.page.locator('#whisperCli').fill(str(cli))
        self.page.locator('#modelPath').fill(str(model))
        self.page.locator('#saveTranscription').click()
        expect(self.page.locator('#message')).to_contain_text('Local transcription paths saved')
        self.fixture.voice.hold_transcription = True
        self.page.get_by_role('button', name='Transcribe', exact=True).click()
        expect(self.page.locator('#voiceStatus')).to_contain_text('Transcribing locally')
        expect(self.page.locator('#start')).to_be_disabled()
        expect(self.page.locator('#saveTranscription')).to_be_disabled()
        self.page.locator('#cancelTranscription').click()
        expect(self.page.locator('#voiceStatus')).to_contain_text('Transcription cancelled')
        expect(self.page.locator('#start')).to_be_enabled()
        self.fixture.voice.hold_transcription = False
        self.page.get_by_role('button', name='Transcribe', exact=True).click()
        expect(self.page.locator('#notes')).to_contain_text('Unverified local transcription')
        expect(self.page.locator('#notes blockquote')).to_have_text(INJECTION)
        expect(self.page.locator('#checks .badge.not_tested')).to_have_count(len(self.fixture.service_module.CHECKS))
        self.assertIsNone(self.page.evaluate('window.injectionRan'))
        self.assertEqual(self.fixture.voice.transcriptions, 2)
        self.assertEqual(self.errors, [])
        self.screenshot('desktop-unverified-transcript-fixture')

    def test_errors_disconnect_and_history_warning_preserve_unsaved_text(self):
        first = self.start()
        draft = 'Unsaved observation: left-eye tree, exact location still needed.'
        self.page.locator('#note').fill(draft)
        self.page.locator('#finish').click()
        expect(self.page.locator('#message')).to_have_text('You have unsaved note text.')
        expect(self.page.locator('#note')).to_have_value(draft)
        self.fixture.recording = {}
        self.page.locator('#linkRecording').click()
        expect(self.page.locator('#message')).to_contain_text('Start or finish a recording')
        expect(self.page.locator('#note')).to_have_value(draft)
        self.page.locator('#voiceTools summary').click()
        self.fixture.voice.device_error = 'Synthetic microphone enumeration unavailable'
        self.page.locator('#devices').click()
        expect(self.page.locator('#message')).to_contain_text('Synthetic microphone enumeration unavailable')
        expect(self.page.locator('#note')).to_have_value(draft)
        self.context.set_offline(True)
        self.page.locator('#refresh').click()
        expect(self.page.locator('#connection')).to_contain_text('Launcher unavailable')
        expect(self.page.locator('#saveNote')).to_be_disabled()
        expect(self.page.locator('#note')).to_have_value(draft)
        self.screenshot('desktop-offline-draft')
        self.context.set_offline(False)
        self.refresh()
        expect(self.page.locator('#connection')).to_contain_text('Connected locally')
        expect(self.page.locator('#note')).to_have_value(draft)
        self.page.once('dialog', lambda dialog: dialog.dismiss())
        self.page.locator('#history').select_option('')
        expect(self.page.locator('#history')).to_have_value(first)
        expect(self.page.locator('#note')).to_have_value(draft)
        self.page.locator('#note').fill('')
        self.assertEqual(self.errors, [])

    def test_service_restart_resumes_committed_session_and_results(self):
        session_id = self.start()
        self.page.locator('#checks article').nth(3).get_by_role('button', name='Blocked', exact=True).click()
        expect(self.page.locator('#checks .badge.blocked')).to_have_count(1)
        self.page.locator('#note').fill('Saved before isolated service restart.')
        self.page.locator('#saveNote').click()
        expect(self.page.locator('#note')).to_have_value('')
        self.fixture.replace_service()
        self.page.reload()
        expect(self.page.locator('#checks .badge.blocked')).to_have_count(1)
        expect(self.page.locator('#notes')).to_contain_text('Saved before isolated service restart.')
        expect(self.page.locator('#buildIdentity')).to_contain_text(session_id)
        expect(self.page.locator('#start')).to_be_disabled()
        self.assertEqual(self.fixture.voice.starts, 0)
        self.assertEqual(self.errors, [])

    def test_pending_note_save_does_not_erase_a_new_draft(self):
        self.start()
        submitted = 'First observation being saved.'
        newer = 'Next observation typed while the previous save completes.'
        self.page.locator('#note').fill(submitted)
        self.fixture.delay_path = '/api/playtest/note'
        self.page.locator('#saveNote').click()
        self.assertTrue(self.fixture.response_waiting.wait(2))
        expect(self.page.locator('#saveNote')).to_be_disabled()
        expect(self.page.locator('#finish')).to_be_disabled()
        # A text box may either lock during save or retain edits made in flight.
        if self.page.locator('#note').is_enabled():
            self.page.locator('#note').fill(newer)
            self.fixture.response_gate.set()
            expect(self.page.locator('#saveNote')).to_be_enabled()
            expect(self.page.locator('#note')).to_have_value(newer)
        else:
            self.fixture.response_gate.set()
            expect(self.page.locator('#saveNote')).to_be_enabled()
            expect(self.page.locator('#note')).to_have_value('')
        expect(self.page.locator('#notes')).to_contain_text(submitted)
        self.page.locator('#note').fill('')


if __name__ == '__main__':
    unittest.main()
