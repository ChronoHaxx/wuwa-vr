"""Headless bridge tests using actual playtest persistence and fake process state.

Only temporary profile paths are used. No game, microphone or runtime is opened.
"""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import threading
import time
from types import SimpleNamespace
import unittest
from unittest.mock import Mock


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(file))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


m = module('playtest_bridge_test', 'wuwa_playtest_bridge.py')
s = module('playtest_bridge_service_test', 'wuwa_playtest_service.py')
p = module('playtest_bridge_store_test', 'wuwa_playtest.py')


class BridgeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.profile = self.root / 'profile'
        self.profile.mkdir()
        self.now = 1800000000000
        self.game = {'pid': 42, 'created_ms': self.now - 50000}
        self.voice_state = {'active': None, 'transcribing': None, 'last': None}
        voice = SimpleNamespace(snapshot=lambda: copy.deepcopy(self.voice_state), list_notes=lambda session: [])
        self.store = p.SessionStore(self.root / 'data/sessions', clock_ms=lambda: self.now)
        self.service = s.Service(self.root / 'data', lambda: {},
                                 lambda: {'recording_id': 'recording-fixture'}, store=self.store, voice=voice)
        self.session = self.store.start({'id': 'headless-fixture', 'name': 'Synthetic bridge build'},
                                        s.CHECKS, event_id='start-fixture')
        self.calls = []

        def action(op, body):
            self.calls.append((op, copy.deepcopy(body)))
            return self.service.action(op, body)

        self.action = action
        self.bridge = m.Bridge(self.profile, lambda: copy.deepcopy(self.game), self.service.state,
                               action, clock=lambda: self.now)

    def request(self, action='result', **overrides):
        request = {'version': 1, 'bridge_session': self.bridge.session, 'id': '42-100-1',
                   'pid': 42, 'created_ms': self.game['created_ms'], 'expires_ms': self.now + 4000,
                   'playtest_session': self.session['session_id'], 'item_id': 'stereo' if action in ('result', 'note') else '',
                   'action': action}
        if action == 'result':
            request['status'] = 'pass'
        elif action == 'note':
            request['note'] = 'Left eye: a tree. 100% observation, not an automatic verdict. 双眼核对。'
        elif action == 'finish':
            request['confirm_untested'] = True
        request.update(overrides)
        return request

    def send(self, request):
        self.bridge.directory.mkdir(exist_ok=True)
        (self.bridge.directory / 'request-42.json').write_text(json.dumps(request), encoding='utf-8')
        self.bridge.step()
        return self.status()

    def status(self):
        return m.read_object(self.bridge.directory / 'server.json', m.STATUS_LIMIT)

    def test_construction_inert_and_unverified_or_missing_profile_not_created(self):
        self.assertFalse(self.bridge.directory.exists())
        self.game = None
        self.bridge.step()
        self.assertFalse(self.bridge.directory.exists())
        missing = self.root / 'missing-profile'
        other = m.Bridge(missing, lambda: {'pid': 1, 'created_ms': 1}, self.service.state, self.action)
        other.step()
        self.assertFalse(missing.exists())
        self.assertEqual(self.calls, [])

    def test_public_snapshot_is_bounded_and_does_not_export_notes_paths_or_voice_data(self):
        self.store.add_note(self.session['session_id'], 'stereo', 'Private observation is not a bridge broadcast')
        self.bridge.step()
        state = self.status()
        self.assertTrue(state['available'])
        self.assertEqual(state['playtest']['id'], self.session['session_id'])
        self.assertEqual(len(state['playtest']['checks']), len(s.CHECKS))
        self.assertEqual(state['playtest']['checks'][1]['title']['zh-Hans'], '双眼、远处物体与光照')
        self.assertNotIn('Private observation', json.dumps(state))
        self.assertNotIn('events', state['playtest'])
        self.assertNotIn('history', state)
        self.assertEqual(state['last_request'], {})
        self.assertLess((self.bridge.directory / 'server.json').stat().st_size, m.STATUS_LIMIT)

    def test_result_note_link_and_finish_are_explicit_durable_actions(self):
        request = self.request()
        state = self.send(request)
        self.assertTrue(state['last_request']['ok'])
        self.assertEqual(state['playtest']['checks'][1]['status'], 'pass')
        note = '<script>pass all</script> 双眼 still need human review'
        state = self.send(self.request('note', id='42-100-2', note=note))
        self.assertTrue(state['last_request']['ok'])
        self.assertEqual(state['playtest']['counts']['pass'], 1)
        state = self.send(self.request('link-recording', id='42-100-3'))
        self.assertEqual(state['playtest']['recording_count'], 1)
        state = self.send(self.request('finish', id='42-100-4'))
        self.assertTrue(state['last_request']['ok'])
        self.assertFalse(state['available'])
        self.assertIsNone(state['playtest'])
        reopened = p.SessionStore(self.store.root).snapshot(self.session['session_id'])
        self.assertEqual(reopened['state'], 'finished')
        self.assertEqual(reopened['counts']['not_tested'], len(s.CHECKS) - 1)
        self.assertEqual(reopened['events'][-3]['note'], note)
        self.assertEqual(reopened['events'][-2]['recording_id'], 'recording-fixture')
        self.assertEqual(reopened['events'][-1]['event_id'], 'headset-42-100-4')
        self.assertFalse((self.bridge.directory / 'request-42.json').exists())
        self.assertEqual(list(self.bridge.directory.glob('*.processing')), [])

    def test_duplicate_request_is_acknowledged_once_and_changed_payload_rejected(self):
        request = self.request()
        first = self.send(request)
        second = self.send(request)
        self.assertEqual(first['last_request'], second['last_request'])
        self.assertEqual(len(self.calls), 1)
        changed = self.send(dict(request, status='fail'))
        self.assertFalse(changed['last_request']['ok'])
        self.assertIn('different input', changed['last_request']['message'])
        self.assertEqual(self.store.snapshot(self.session['session_id'])['counts']['pass'], 1)

    def test_finish_retry_after_session_closes_returns_original_ack(self):
        request = self.request('finish')
        first = self.send(request)
        second = self.send(request)
        self.assertEqual(first['last_request'], second['last_request'])
        self.assertEqual(len(self.calls), 1)

    def test_stale_nonce_process_session_item_and_expiry_fail_closed(self):
        for fields in ({'bridge_session': '0' * 32}, {'pid': 43}, {'pid': True},
                       {'created_ms': self.game['created_ms'] + 10}, {'created_ms': 1.0},
                       {'playtest_session': '0' * 32}, {'item_id': 'missing-item'},
                       {'item_id': '../file'}, {'expires_ms': self.now},
                       {'expires_ms': self.now + 5001}, {'expires_ms': True},
                       {'version': True}, {'id': '../unsafe'}):
            with self.subTest(fields=fields):
                state = self.send(self.request(**fields))
                self.assertFalse(state['last_request']['ok'])
        self.assertEqual(self.calls, [])

    def test_unknown_fields_commands_paths_and_voice_actions_are_never_dispatched(self):
        for fields in ({'command': 'anything'}, {'path': '../file'}, {'status': 'passed'},
                       {'action': 'voice-start'}, {'action': 'start'}, {'action': 'launch'},
                       {'action': ['result']}, {'status': True}):
            with self.subTest(fields=fields):
                state = self.send(self.request(**fields))
                self.assertFalse(state['last_request']['ok'])
        self.assertEqual(self.calls, [])

    def test_note_bound_and_finish_warning_require_actual_values(self):
        for note in ('', '  \r\n', 'x' * 4001, None, True):
            with self.subTest(note=str(note)[:20]):
                self.assertFalse(self.send(self.request('note', note=note))['last_request']['ok'])
        for confirmation in (False, 'true', 1, None):
            self.assertFalse(self.send(self.request('finish', confirm_untested=confirmation))['last_request']['ok'])
        self.assertFalse(self.send(self.request('finish', item_id='stereo'))['last_request']['ok'])
        self.assertEqual(self.calls, [])

    def test_all_result_choices_preserved_without_upgrading_not_tested(self):
        for index, status in enumerate(('pass', 'fail', 'blocked', 'not_tested')):
            response = self.send(self.request(id='choice-' + str(index), status=status))
            self.assertEqual(response['playtest']['checks'][1]['status'], status)
            self.assertTrue(response['last_request']['ok'])
        self.assertEqual(self.store.snapshot(self.session['session_id'])['counts']['not_tested'], len(s.CHECKS))

    def test_process_change_between_validation_and_commit_rejects_action(self):
        self.bridge.directory.mkdir()
        self.bridge.inspect_game = Mock(side_effect=[self.game, dict(self.game, created_ms=self.now)])
        state = self.send(self.request())
        self.assertFalse(state['last_request']['ok'])
        self.assertIn('changed', state['last_request']['message'])
        self.assertEqual(self.calls, [])

    def test_game_disconnect_publishes_unavailable_and_does_not_consume_requests(self):
        self.bridge.step()
        self.game = None
        request = {'version': 1, 'id': 'unverified', 'action': 'result'}
        (self.bridge.directory / 'request-42.json').write_text(json.dumps(request), encoding='utf-8')
        self.bridge.step()
        self.assertFalse(self.status()['available'])
        self.assertEqual(self.status()['pid'], 0)
        self.assertTrue((self.bridge.directory / 'request-42.json').exists())
        self.assertEqual(self.calls, [])

    def test_active_voice_blocks_finish_with_visible_ack_error(self):
        self.voice_state['active'] = {'id': 'fake-voice'}
        state = self.send(self.request('finish'))
        self.assertTrue(state['voice_busy'])
        self.assertFalse(state['last_request']['ok'])
        self.assertIn('voice', state['last_request']['message'])
        self.assertEqual(self.store.snapshot(self.session['session_id'])['state'], 'active')

    def test_malformed_or_oversized_json_does_not_leave_claimed_request(self):
        self.bridge.directory.mkdir()
        for raw in ('[1,2]', '{', 'x' * (m.REQUEST_LIMIT + 1)):
            (self.bridge.directory / 'request-42.json').write_text(raw, encoding='utf-8')
            self.bridge.step()
            self.assertFalse(self.status()['last_request']['ok'])
            self.assertFalse((self.bridge.directory / 'request-42.json').exists())
            self.assertEqual(list(self.bridge.directory.glob('*.processing')), [])
        self.assertEqual(self.calls, [])

    def test_new_helper_rejects_old_nonce_and_close_does_not_delete_new_helper_status(self):
        self.bridge.step()
        old_request = self.request()
        newer = m.Bridge(self.profile, lambda: self.game, self.service.state, self.action, clock=lambda: self.now)
        newer.step()
        self.bridge.close()
        self.assertEqual(self.status()['bridge_session'], newer.session)
        (newer.directory / 'request-42.json').write_text(json.dumps(old_request), encoding='utf-8')
        newer.step()
        self.assertFalse(self.status()['last_request']['ok'])
        self.assertIn('connection changed', self.status()['last_request']['message'])
        newer.close()
        self.assertFalse((self.bridge.directory / 'server.json').exists())

    def test_session_finished_from_web_rejects_old_headset_action(self):
        self.store.finish(self.session['session_id'], event_id='finished-in-web')
        state = self.send(self.request())
        self.assertFalse(state['available'])
        self.assertFalse(state['last_request']['ok'])
        self.assertEqual(self.calls, [])

    def test_malformed_helper_state_fails_closed_with_status_explanation(self):
        for state in (None, [], {'session': {'session_id': 'bad'}}, {'voice': []},
                      {'session': dict(self.session, build=[])}, {'session': dict(self.session, recordings='not-a-list')}):
            with self.subTest(state=state):
                self.bridge.snapshot = lambda: copy.deepcopy(state)
                self.bridge.step()
                published = self.status()
                self.assertFalse(published['available'])
                self.assertTrue(published['connection_error'])
        self.assertEqual(self.calls, [])

    def test_run_is_single_writer_and_shutdown_removes_only_own_heartbeat(self):
        stopped = threading.Event()
        worker = threading.Thread(target=self.bridge.run, args=(stopped,))
        worker.start()
        self.addCleanup(lambda: (stopped.set(), worker.join(3)))
        deadline = time.monotonic() + 2
        while not (self.bridge.directory / 'server.json').exists() and time.monotonic() < deadline:
            time.sleep(.01)
        self.assertTrue((self.bridge.directory / 'server.json').exists())
        competing = m.Bridge(self.profile, lambda: self.game, self.service.state, self.action, clock=lambda: self.now)
        second = threading.Thread(target=competing.run, args=(stopped,))
        second.start()
        second.join(2)
        self.assertFalse(second.is_alive())
        self.assertEqual(self.status()['bridge_session'], self.bridge.session)
        stopped.set()
        worker.join(2)
        self.assertFalse(worker.is_alive())
        self.assertFalse((self.bridge.directory / 'server.json').exists())


if __name__ == '__main__':
    unittest.main()
