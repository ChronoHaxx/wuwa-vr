"""Headless incident UI with real HTTP/Collector and inert game/OS fixtures."""
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
import unittest
from urllib.parse import urlparse

from playwright.sync_api import sync_playwright, expect
import wuwa_incident

HERE = Path(__file__).resolve().parent
SITE = HERE.parent.parent / 'site'
ARTIFACTS = HERE.parent / 'native/work/incident-ui-20261004'
INJECTION = '<img src=x onerror="window.injectionRan=true"><script>window.injectionRan=true</script>'


class Fixture:
    def __init__(self, root):
        self.root = root
        self.profile = root / 'profile'
        self.profile.mkdir()
        self.data = root / 'data'
        self.data.mkdir()
        self.now = 1800000000000
        self.collector = wuwa_incident.Collector(self.profile, self.data / 'incidents',
            lambda: {'package_id': 'synthetic-ui-only', 'catalog_backend_sha256': 'a' * 64},
            clock_ms=lambda: self.now)
        spec = importlib.util.spec_from_file_location('incident_page_' + secrets.token_hex(4), HERE / 'wuwa_player.py')
        self.helper = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.helper)
        self.helper.CONFIG = self.helper.Config(app=HERE.parent, data=self.data, profile=self.profile)
        self.helper.INCIDENTS = self.collector
        self.status = {'selected': 'fixture', 'selectionMatches': True,
                       'builds': [{'id': 'fixture', 'name': 'Synthetic fixture only', 'role': 'candidate'}],
                       'package': {'id': 'ui-fixture', 'private': True, 'defaultBuild': 'fixture'},
                       'openxr': {'name': 'Not connected', 'available': False},
                       'gameRunning': False, 'injectorRunning': False,
                       'game': {'mode': 'manual', 'launcher': '', 'detected': [], 'problem': ''},
                       'riskAcknowledged': False, 'originalBackup': False, 'dataFolder': 'Synthetic fixture',
                       'job': {'running': False}, 'recording': {'running': False, 'available': False}}
        self.helper.status = lambda: copy.deepcopy(self.status)
        self.helper.static_file = lambda url: (SITE / url.removeprefix('/guide/'),
            self.helper.STATIC_TYPES.get(Path(url).suffix, 'application/octet-stream')) if url in (
            '/guide/style.css', '/guide/config.js', '/guide/app.js', '/guide/media/mark.svg') else None
        self.requests, self.opened = [], []
        self.fail_save = None
        self.delay = False
        self.waiting, self.release = threading.Event(), threading.Event()
        fixture = self

        class Handler(self.helper.Handler):
            def post_action(self, path, body):
                # Never delegate any hardware/game/OS action in this fixture.
                if path == '/api/open' and body == {'folder': 'incidents'}:
                    fixture.opened.append('incidents')
                    return {'ok': True}
                if path != '/api/incidents/save':
                    raise ValueError('Side effects disabled by inert UI fixture')
                if fixture.fail_save:
                    raise ValueError(fixture.fail_save)
                if fixture.delay:
                    fixture.waiting.set()
                    fixture.release.wait(5)
                return super().post_action(path, body)

            def reply(self, code, data, content_type='application/json; charset=utf-8'):
                fixture.requests.append({'method': self.command, 'path': self.path, 'status': code})
                return super().reply(code, data, content_type)

        self.server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.server.daemon_threads = True
        self.url = 'http://127.0.0.1:' + str(self.server.server_port)
        self.helper.BASE_URL = self.url
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def publish(self, age_ms=0):
        (self.profile / 'wuwa-test.status.json').write_text(
            json.dumps({'pid': 123, 'unix_ms': self.now - age_ms, 'live_options': {'VR_MonoTheatre': 'true'}}))
        (self.profile / 'log.txt').write_text('Synthetic backend log only\n')
        self.collector.poll()

    def close(self):
        self.release.set()
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(2)


class IncidentPageTests(unittest.TestCase):
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
        report = {'created_utc': datetime.now(timezone.utc).isoformat(), 'headless': True,
                  'real_components': ['launcher page', 'HTTP handler', 'incident Collector'],
                  'fake_components': ['game/profile files', 'launcher status', 'open folder'],
                  'physical_acceptance': 'Not performed', 'tests': cls.results,
                  'source_sha256': {name: hashlib.sha256((HERE / name).read_bytes()).hexdigest()
                                    for name in ('wuwa-player.html', 'wuwa_player.py', 'wuwa_incident.py')}}
        (ARTIFACTS / 'results.json').write_text(json.dumps(report, indent=2), encoding='utf-8')

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='fixture-', dir=ARTIFACTS)
        self.fixture = Fixture(Path(self.temp.name))
        self.context = self.browser.new_context(viewport={'width': 1100, 'height': 900})
        self.external = []
        def route(request):
            if request.request.url.startswith(self.fixture.url + '/'):
                request.continue_()
            else:
                self.external.append(request.request.url)
                request.abort()
        self.context.route('**/*', route)
        self.page = self.context.new_page()
        self.page.set_default_timeout(5000)
        self.errors = []
        self.page.on('pageerror', lambda error: self.errors.append(str(error)))
        self.page.goto(self.fixture.url + '/')
        self.panel = self.page.locator('section[aria-labelledby="incident-heading"]')
        self.panel.scroll_into_view_if_needed()

    def tearDown(self):
        failed = any(test is self and error for test, error in
                     self._outcome.result.errors + self._outcome.result.failures)
        screenshot = self._testMethodName + '.png'
        self.panel.screenshot(path=str(ARTIFACTS / screenshot))
        self.results.append({'test': self._testMethodName, 'result': 'FAIL' if failed else 'PASS',
                             'screenshot': screenshot, 'page_errors': self.errors,
                             'external_requests_blocked': len(self.external)})
        self.context.close()
        self.fixture.close()
        self.temp.cleanup()

    def refresh(self):
        self.page.evaluate('refreshIncidents()')

    def assert_safe(self):
        self.assertFalse(self.errors)
        self.assertFalse(self.external)
        self.assertIsNone(self.page.evaluate('window.injectionRan'))
        token = self.fixture.helper.TOKEN
        self.assertNotIn(token, self.page.locator('body').inner_text())
        self.assertTrue(all(token not in item['path'] for item in self.fixture.requests))
        post_paths = {item['path'] for item in self.fixture.requests if item['method'] == 'POST'}
        self.assertTrue(post_paths <= {'/api/incidents/save', '/api/open'})

    def test_save_note_time_and_open_folder_with_stale_status(self):
        self.fixture.publish(age_ms=40000)
        self.refresh()
        expect(self.page.locator('#incidentState')).to_contain_text('stale')
        note = 'Black screen after dialogue; resumed after 30 seconds. ' + INJECTION
        self.page.locator('#incidentNote').fill(note)
        self.page.locator('#incidentAgo').select_option('30')
        self.fixture.delay = True
        self.page.locator('#saveIncident').click()
        expect(self.page.locator('#saveIncident')).to_be_disabled()
        self.assertTrue(self.fixture.waiting.wait(1))
        self.fixture.release.set()
        expect(self.page.locator('#incidentResult')).to_contain_text('Saved locally:')
        expect(self.page.locator('#incidentResult')).to_contain_text('No retrospective video')
        expect(self.page.locator('#saveIncident')).to_be_enabled()
        report = self.fixture.collector.report(self.fixture.collector.last_saved['id'])
        self.assertEqual(report['note'], note)
        self.assertEqual(report['user_event_time_estimate_ms'], self.fixture.now - 30000)
        self.assertIn('stale', json.dumps(report['warnings']))
        self.assertNotIn(self.fixture.helper.TOKEN, json.dumps(report))
        with self.page.expect_response(lambda response: response.url.endswith('/api/open')):
            self.page.locator('#openIncidents').click()
        self.assertEqual(self.fixture.opened, ['incidents'])
        self.assert_safe()

    def test_empty_then_missing_status_does_not_claim_live_evidence(self):
        expect(self.page.locator('#incidentState')).to_contain_text('Waiting for the first sample')
        expect(self.page.locator('#incidentState')).not_to_contain_text('undefined')
        self.fixture.collector.poll()
        self.refresh()
        expect(self.page.locator('#incidentState')).to_contain_text('missing')
        self.page.locator('#saveIncident').click()
        expect(self.page.locator('#incidentResult')).to_contain_text('Saved locally:')
        expect(self.page.locator('#incidentResult')).to_contain_text('missing, stale or unreadable')
        expect(self.page.locator('#incidentResult')).to_contain_text('Less than five minutes')
        report = self.fixture.collector.report(self.fixture.collector.last_saved['id'])
        self.assertEqual(report['status_samples'][-1]['status_state'], 'missing')
        self.assert_safe()

    def test_invalid_and_failed_save_keep_note_and_allow_retry(self):
        note = 'Keep this note through failed saves'
        self.page.locator('#incidentNote').fill(note)
        self.page.locator('#incidentAgo').evaluate('(element) => element.options[0].value = "301"')
        self.page.locator('#saveIncident').click()
        expect(self.page.locator('#incidentResult')).to_contain_text('Could not save: Event age')
        expect(self.page.locator('#saveIncident')).to_be_enabled()
        expect(self.page.locator('#incidentNote')).to_have_value(note)
        self.page.locator('#incidentAgo').select_option('60')
        self.fixture.fail_save = 'Synthetic write failure ' + INJECTION
        self.page.locator('#saveIncident').click()
        expect(self.page.locator('#incidentResult')).to_contain_text('Synthetic write failure')
        self.assertEqual(self.page.locator('#incidentResult img, #incidentResult script').count(), 0)
        expect(self.page.locator('#saveIncident')).to_be_enabled()
        self.fixture.fail_save = None
        self.page.locator('#saveIncident').click()
        expect(self.page.locator('#incidentResult')).to_contain_text('Saved locally:')
        report = self.fixture.collector.report(self.fixture.collector.last_saved['id'])
        self.assertEqual(report['note'], note)
        self.assertEqual(len(list(self.fixture.collector.output.iterdir())), 1)
        self.assert_safe()


if __name__ == '__main__':
    unittest.main()
