"""Fresh-user launch recovery with inert processes/registry and isolated data."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from datetime import datetime, timezone, timedelta
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('player_recovery', Path(__file__).with_name('wuwa_player.py'))
player = importlib.util.module_from_spec(spec)
spec.loader.exec_module(player)


class LaunchRecovery(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='WuWa recovery 中文 ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.app = self.root / 'app'
        self.app.mkdir()
        self.data = self.root / 'data'
        self.data.mkdir()
        self.addCleanup(patch.stopall)
        patch.object(player, 'CONFIG', player.Config(self.app, self.data, self.root / 'profile')).start()
        patch.dict(player.JOB, {'kind': '', 'running': False, 'startedUtc': ''}, clear=True).start()
        self.now = datetime.now(timezone.utc)
        self.cancel = self.data / 'runs/attempt/cancel.request'
        self.cancel.parent.mkdir(parents=True)

    def state(self, **extra):
        value = {'phase': 'waiting-for-play', 'requestedAt': self.now.isoformat(),
                 'attemptId': 'test', 'buildId': 'steam', 'cancelPath': str(self.cancel),
                 'runDir': str(self.cancel.parent), 'pid': 1234, 'started': self.now.isoformat()}
        value.update(extra)
        (self.data / 'launch-state.json').write_text(json.dumps(value))
        return value

    def test_worker_remains_visible_and_cancellable_after_frontend_finishes(self):
        self.state()
        with patch.object(player, 'launch_owner_alive', return_value=True):
            result = player.launch_state()
            self.assertTrue(result['current'] and result['running'] and result['cancellable'])
            self.assertEqual(result['runId'], 'attempt')
            player.cancel_launch()
        self.assertEqual(self.cancel.read_text(), 'cancel\n')

    def test_dead_or_recycled_worker_cannot_be_cancelled(self):
        self.state()
        with patch.object(player, 'launch_owner_alive', return_value=False):
            self.assertFalse(player.launch_state()['current'])
            with self.assertRaisesRegex(ValueError, 'No launch'):
                player.cancel_launch()
        self.assertFalse(self.cancel.exists())

    def test_terminal_error_visible_only_for_current_launch(self):
        self.state(phase='failed', message='Injector rejected selected path')
        with patch.object(player, 'launch_owner_alive', return_value=False):
            self.assertFalse(player.launch_state()['current'])
            player.JOB.update(kind='launch', startedUtc=(self.now - timedelta(seconds=2)).isoformat())
            result = player.launch_state()
            self.assertTrue(result['current'])
            self.assertFalse(result['running'] or result['cancellable'])
            player.JOB['startedUtc'] = (self.now + timedelta(seconds=2)).isoformat()
            self.assertFalse(player.launch_state()['current'])

    def test_foreign_cancel_path_and_terminal_states_never_written(self):
        with patch.object(player, 'launch_owner_alive', return_value=True):
            self.state(cancelPath=str(self.root / 'cancel.request'))
            self.assertFalse(player.launch_state()['cancellable'])
            for phase in ('failed', 'finished', 'cancelled'):
                self.state(phase=phase)
                self.assertFalse(player.launch_state()['cancellable'])
        self.assertFalse(self.cancel.exists())

    def test_active_worker_blocks_duplicate_launch_and_runtime_mutation(self):
        self.state()
        with patch.object(player, 'processes', return_value=[]), patch.object(player, 'launch_owner_alive', return_value=True):
            with self.assertRaisesRegex(ValueError, 'Stop waiting'):
                player.require_idle()

    def test_child_rejection_keeps_precise_reason(self):
        with patch.object(player, 'apply_build'), patch.object(player, 'powershell', return_value=subprocess.CompletedProcess([], 4, 'Wrong Windows account for this launch')):
            with self.assertRaisesRegex(player.OperationError, 'Wrong Windows account'):
                player.launch_build('steam')

    def test_stop_endpoint_cannot_abandon_worker_or_recording(self):
        handler = object.__new__(player.Handler)
        with patch.object(player.threading, 'Thread') as thread:
            self.state()
            with patch.object(player, 'launch_owner_alive', return_value=True):
                with self.assertRaisesRegex(ValueError, 'current operation'):
                    handler.post_action('/api/stop', {})
            with patch.object(player, 'launch_owner_alive', return_value=False), patch.dict(player.RECORDING, running=True):
                with self.assertRaisesRegex(ValueError, 'Stop recording'):
                    handler.post_action('/api/stop', {})
            thread.assert_not_called()

    def simulator(self):
        folder = self.app / 'dev-tools/OpenXR-Simulator'
        folder.mkdir(parents=True)
        (folder / 'openxr_simulator.dll').write_bytes(b'inert fixture')
        path = folder / 'openxr_simulator.json'
        path.write_text(json.dumps({'runtime': {'name': 'OpenXR Simulator', 'library_path': 'openxr_simulator.dll'}}))
        return path

    def test_fresh_pc_with_no_openxr_can_select_bundled_simulator(self):
        self.simulator()
        with patch.object(player.winreg, 'OpenKey', side_effect=FileNotFoundError), patch.object(player, 'require_idle'):
            result = player.openxr_status()
            self.assertTrue(result['canSimulator'])
            self.assertFalse(result['canHeadset'] or result['available'])
            self.assertEqual(result['manifest'], '')
            player.require_runtime('simulator', '')
            with self.assertRaisesRegex(ValueError, 'changed'):
                player.require_runtime('simulator', 'old manifest')

    def test_missing_old_simulator_is_repairable_without_replacing_headset(self):
        self.simulator()
        values = {'ActiveRuntime': str(self.root / 'deleted package/openxr_simulator.json'), 'PreviousActiveRuntime': ''}
        with patch.object(player.winreg, 'OpenKey'), patch.object(player.winreg, 'QueryValueEx', side_effect=lambda k, name: (values[name], 1)):
            result = player.openxr_status()
            self.assertTrue(result['isSimulator'] and result['canSimulator'])
            self.assertFalse(result['available'] or result['isBundledSimulator'])

    def test_existing_but_empty_registration_does_not_mean_absent(self):
        self.simulator()
        with patch.object(player.winreg, 'OpenKey'), patch.object(player.winreg, 'QueryValueEx', return_value=('', 1)):
            self.assertFalse(player.openxr_status()['canSimulator'])

    def test_read_denied_does_not_mean_no_runtime(self):
        self.simulator()
        with patch.object(player.winreg, 'OpenKey', side_effect=PermissionError):
            result = player.openxr_status()
            self.assertFalse(result.get('canSimulator'))
            self.assertNotIn('manifest', result)

    def test_real_inert_powershell_failure_writes_trace_without_arguments(self):
        fixture = self.app / 'inert test.ps1'
        fixture.write_text("Write-Output 'a deliberate test failure'; exit 7", encoding='utf-8')
        result = player.powershell('-File', str(fixture), encoding='utf-8')
        self.assertEqual(result.returncode, 7)
        trace = next((self.data / 'logs').glob('helper-*.log')).read_text(encoding='utf-8')
        self.assertIn('inert test.ps1', trace)
        self.assertIn('exit 7', trace)
        self.assertIn('a deliberate test failure', trace)
        self.assertNotIn(str(self.app), trace)

    def test_real_process_identity_requires_creation_time(self):
        # Query this test process only; no game or helper process is touched.
        import psutil
        created = datetime.fromtimestamp(psutil.Process(os.getpid()).create_time(), timezone.utc).isoformat()
        state = {'ownerPid': os.getpid(), 'ownerStartedUtc': created}
        self.assertTrue(player.launch_owner_alive(state))
        state['ownerStartedUtc'] = (self.now + timedelta(days=1)).isoformat()
        self.assertFalse(player.launch_owner_alive(state))

    def test_diagnostics_include_bounded_owned_startup_error_only(self):
        (self.cancel.parent / 'startup-error.txt').write_text('early preflight failure')
        (self.cancel.parent / 'injector.stderr.log').write_text('x' * 32000 + '\nselected path was rejected')
        (self.cancel.parent / 'unrelated.txt').write_text('must not be copied')
        text = '\n'.join(player.startup_diagnostics({'runId': 'attempt'}))
        self.assertIn('early preflight failure', text)
        self.assertIn('selected path was rejected', text)
        self.assertNotIn('must not be copied', text)
        self.assertLess(len(text), 17000)
        self.assertEqual(player.startup_diagnostics({'runId': '../../'}), [])


if __name__ == '__main__':
    unittest.main()
