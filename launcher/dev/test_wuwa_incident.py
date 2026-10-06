"""Offline incident retention/export checks. No game, device or live profile access."""
import importlib.util
from http.client import HTTPConnection
from http.server import ThreadingHTTPServer
import json
import os
from pathlib import Path
import stat
import tempfile
import threading
import unittest
from unittest.mock import patch
from types import SimpleNamespace

import wuwa_incident as m


class IncidentFixture:
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='wuwa-incident-test-')
        self.root = Path(self.temp.name)
        self.profile = self.root / 'profile'
        self.profile.mkdir()
        self.now = 1800000000000
        self.output = self.root / 'incidents'
        self.c = m.Collector(self.profile, self.output,
            lambda: {'package_id': 'fixture', 'catalog_backend_sha256': 'a' * 64,
                     'verification': 'Package metadata only'}, clock_ms=lambda: self.now,
            redact=lambda value: str(value).replace('private-user', '%USERNAME%'))

    def tearDown(self):
        self.temp.cleanup()

    def status(self, **kwargs):
        value = {'pid': 42, 'unix_ms': self.now, 'version': 1,
                 'live_options': {'VR_MonoTheatre': 'false'},
                 'camera': {'controls': {'script_status': 'Game camera.'}, 'head_m': [1, 2, 3]},
                 'input_sequence': {'synthetic_buttons': 'private-unwanted'}}
        value.update(kwargs)
        (self.profile / 'wuwa-test.status.json').write_text(json.dumps(value), encoding='utf-8')

    def log(self, text, append=False):
        with (self.profile / 'log.txt').open('a' if append else 'w', encoding='utf-8', newline='') as f:
            f.write(text)


class IncidentTests(IncidentFixture, unittest.TestCase):
    def test_construction_and_poll_do_not_write_or_control_anything(self):
        self.status()
        self.log('ordinary existing backend output\n')
        before = {p.name: (p.read_bytes(), p.stat().st_mtime_ns) for p in self.profile.iterdir()}
        self.c.poll()
        after = {p.name: (p.read_bytes(), p.stat().st_mtime_ns) for p in self.profile.iterdir()}
        self.assertEqual(before, after)
        self.assertFalse(self.output.exists())
        self.assertEqual(self.c.snapshot()['sample_count'], 1)
        sample = self.c.samples[-1]
        self.assertEqual(sample['status_state'], 'fresh')
        self.assertNotIn('head_m', sample['diagnostics']['camera'])
        self.assertNotIn('input_sequence', sample['diagnostics'])

    def test_after_event_save_keeps_before_and_stale_after_without_video_claim(self):
        self.status()
        self.log('before\n')
        self.c.poll()
        self.now += 40000
        self.log('after private-user\n', append=True)
        receipt = self.c.save('long black screen for private-user', 35)
        report = self.c.report(receipt['id'])
        self.assertEqual(report['status_samples'][0]['status_state'], 'fresh')
        self.assertEqual(report['status_samples'][-1]['status_state'], 'stale')
        self.assertEqual(report['user_event_time_estimate_ms'], self.now - 35000)
        self.assertEqual(report['status_samples'][0]['identity']['catalog_backend_sha256'], 'a' * 64)
        text = json.dumps(report)
        self.assertNotIn('private-user', text)
        self.assertIn('No retrospective video', text)
        self.assertIn('not treat it as a live', text)
        self.assertTrue((self.output / receipt['id'] / 'README.txt').is_file())

    def test_missing_and_malformed_status_still_save_an_honest_report(self):
        first = self.c.save()
        self.assertEqual(self.c.report(first['id'])['status_samples'][-1]['status_state'], 'missing')
        self.assertIn('missing, stale or unreadable', first['warning'])
        self.assertIn('Less than five minutes', first['warning'])
        self.assertIn('No retained backend log', first['warning'])
        (self.profile / 'wuwa-test.status.json').write_text('{partial')
        second = self.c.save()
        self.assertEqual(self.c.report(second['id'])['status_samples'][-1]['status_state'], 'unreadable')
        self.assertFalse((self.profile / 'wuwa-test.request.json').exists())

    def test_bad_identity_and_future_clock_are_not_fresh(self):
        for override, expected in [({'pid': True}, 'invalid_identity'),
                                   ({'unix_ms': self.now + 60000}, 'clock_mismatch')]:
            self.status(**override)
            self.c.poll()
            self.assertEqual(self.c.samples[-1]['status_state'], expected)

    def test_ring_expires_and_log_memory_is_bounded(self):
        self.status()
        for n in range(180):
            self.now += 2000
            self.log(str(n) + '-' + 'x' * 16000 + '\n', append=True)
            self.c.poll()
        state = self.c.snapshot()
        self.assertLessEqual(state['sample_count'], 151)
        self.assertLessEqual(state['log_bytes'], m.LOG_RING_BYTES)
        self.assertGreater(state['log_evicted_bytes'], 0)
        self.assertGreaterEqual(self.c.samples[0]['observed_at_ms'], self.now - m.WINDOW_MS)
        self.now += m.WINDOW_MS + 1
        self.assertEqual(self.c.snapshot()['sample_count'], 0)
        self.assertEqual(self.c.snapshot()['log_bytes'], 0)

    def test_log_incremental_read_rotation_truncation_and_byte_cap(self):
        self.log('old\n')
        self.c.poll()
        self.log('new\n', append=True)
        self.c.poll()
        self.assertEqual(self.c.logs[-1]['text'], 'new\n')
        self.assertFalse(self.c.logs[-1]['initial_or_rotated_tail'])
        self.c.poll()
        self.assertEqual(len(self.c.logs), 2)
        self.log('r\n')
        self.c.poll()
        self.assertTrue(self.c.logs[-1]['initial_or_rotated_tail'])
        self.log('z' * (m.LOG_READ_BYTES + 1234), append=True)
        self.c.poll()
        self.assertEqual(len(self.c.logs[-1]['text']), m.LOG_READ_BYTES)
        self.assertEqual(self.c.logs[-1]['skipped_bytes'], 1234)

    def test_oversize_status_refused_and_sample_cap_reported(self):
        self.status(padding='x' * m.STATUS_BYTES)
        self.c.poll()
        self.assertEqual(self.c.samples[-1]['status_state'], 'unreadable')
        self.status(shadow={str(i): 'x' * 500 for i in range(100)})
        self.c.poll()
        self.assertTrue(self.c.samples[-1]['truncated'])
        self.assertLess(len(json.dumps(self.c.samples[-1])), m.SAMPLE_BYTES)

    def test_request_and_report_path_validation(self):
        for value in (-1, 301, True, '4'):
            with self.assertRaises(ValueError):
                self.c.save(event_ago_seconds=value)
        for value in (None, 'x' * 4001):
            with self.assertRaises(ValueError):
                self.c.save(note=value)
        for value in ('../profile', '/log.txt', None, 'incident-20260101-000000-000000000000/../x'):
            with self.assertRaises(ValueError):
                self.c.report(value)
        self.assertFalse(self.output.exists())

    def test_no_silent_deletion_when_export_limit_reached(self):
        with patch.object(m, 'MAX_INCIDENTS', 1):
            first = self.c.save()
            with self.assertRaisesRegex(ValueError, 'already saved'):
                self.c.save()
            self.assertEqual(self.c.report(first['id'])['id'], first['id'])

    def test_worker_stops_without_poll_when_stop_already_set(self):
        stopped = threading.Event()
        stopped.set()
        self.c.run(stopped)
        self.assertEqual(self.c.snapshot()['sample_count'], 0)

    def test_package_identity_is_metadata_not_loaded_dll_verification(self):
        app, data = self.root / 'app', self.root / 'data'
        (app / 'dev').mkdir(parents=True)
        data.mkdir()
        (app / 'portable.json').write_text('{"packageId":"test-package"}')
        (app / 'dev/wuwa-builds.json').write_text('{"builds":[{"id":"r4","sha256":"metadata-hash"}]}')
        (data / 'state.json').write_text('{"selected":"r4"}')
        identity = m.package_identity(app, data)
        self.assertEqual(identity['catalog_backend_sha256'], 'metadata-hash')
        self.assertIn('unverified', identity['verification'])

    def test_non_file_source_rejected_and_export_errors_do_not_claim_success(self):
        (self.profile / 'wuwa-test.status.json').mkdir()
        self.c.poll()
        self.assertEqual(self.c.samples[-1]['status_state'], 'unreadable')
        self.output.write_text('blocked path')
        with self.assertRaises(OSError):
            self.c.save()
        self.assertIsNone(self.c.snapshot()['last_saved'])

    def test_worker_polls_once_then_honors_stop(self):
        class StopAfterWait:
            stopped = False
            def is_set(self):
                return self.stopped
            def wait(self, seconds):
                self.stopped = True
        self.c.run(StopAfterWait())
        self.assertEqual(self.c.snapshot()['sample_count'], 1)

    def test_reparse_flags_rejected_without_following_target(self):
        info = SimpleNamespace(st_mode=stat.S_IFDIR, st_file_attributes=0x400)
        with patch.object(Path, 'lstat', return_value=info):
            with self.assertRaisesRegex(ValueError, 'Linked'):
                m._plain(self.profile)

    def test_hardlinked_status_is_not_read(self):
        source = self.root / 'other.json'
        source.write_text('{"pid":42,"unix_ms":1800000000000}')
        os.link(source, self.profile / 'wuwa-test.status.json')
        self.c.poll()
        self.assertEqual(self.c.samples[-1]['status_state'], 'unreadable')
        self.assertNotIn('diagnostics', self.c.samples[-1])

    def test_linked_export_directory_does_not_receive_writes(self):
        other = self.root / 'other-output'
        other.mkdir()
        try:
            self.output.symlink_to(other, target_is_directory=True)
        except OSError as exc:
            self.skipTest('This Windows account cannot create symlinks: ' + str(exc.winerror))
        with self.assertRaisesRegex(ValueError, 'Linked'):
            self.c.save()
        self.assertEqual(list(other.iterdir()), [])
        self.assertIsNone(self.c.last_saved)


class IncidentHttpTests(IncidentFixture, unittest.TestCase):
    # Keep these end-to-end checks offline with a fake profile and the real
    # handler, authentication and save logic. No serve() worker is started.
    def test_authenticated_save_and_download_missing_game(self):
        here = Path(__file__).resolve().parent
        spec = importlib.util.spec_from_file_location('incident_test_player', here / 'wuwa_player.py')
        player = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(player)
        with ThreadingHTTPServer(('127.0.0.1', 0), player.Handler) as server:
            server.daemon_threads = True
            origin = 'http://127.0.0.1:' + str(server.server_port)
            with patch.object(player, 'BASE_URL', origin), patch.object(player, 'INCIDENTS', self.c):
                thread = threading.Thread(target=server.serve_forever, daemon=True)
                thread.start()
                def request(method, path, body=None, token=True, valid_origin=True):
                    connection = HTTPConnection('127.0.0.1', server.server_port, timeout=3)
                    headers = {'Origin': origin if valid_origin else 'https://unrelated.example'}
                    if token:
                        headers['X-WuWa-Token'] = player.TOKEN
                    try:
                        connection.request(method, path, json.dumps(body) if body is not None else None, headers)
                        response = connection.getresponse()
                        return response.status, json.loads(response.read())
                    finally:
                        connection.close()
                try:
                    self.assertEqual(request('GET', '/api/incidents', token=False)[0], 403)
                    self.assertEqual(request('POST', '/api/incidents/save', {}, token=False)[0], 403)
                    self.assertEqual(request('POST', '/api/incidents/save', {}, valid_origin=False)[0], 403)
                    self.assertEqual(request('POST', '/api/incidents/save', {'path': '../x'})[0], 400)
                    self.assertFalse(self.output.exists())
                    code, saved = request('POST', '/api/incidents/save', {'note': 'Stall ended'})
                    self.assertEqual(code, 200)
                    self.assertEqual(request('GET', saved['report_url'], token=False)[0], 403)
                    code, report = request('GET', saved['report_url'])
                    self.assertEqual(code, 200)
                    self.assertEqual(report['status_samples'][-1]['status_state'], 'missing')
                    self.assertEqual(report['note'], 'Stall ended')
                    self.assertEqual(request('GET', '/api/incidents/report?id=../../x')[0], 400)
                    code, state = request('GET', '/api/incidents')
                    self.assertEqual(state['last_saved']['id'], saved['id'])
                finally:
                    server.shutdown()
                    thread.join(timeout=2)


if __name__ == '__main__':
    unittest.main()
