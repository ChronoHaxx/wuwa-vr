"""Fresh-user launch recovery with inert processes/registry and isolated data."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import time
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
                 'runDir': str(self.cancel.parent), 'pid': 1234, 'started': self.now.isoformat(),
                 'heartbeat': self.now.isoformat()}
        value.update(extra)
        (self.data / 'launch-state.json').write_text(json.dumps(value))
        return value

    def test_worker_remains_visible_and_cancellable_after_frontend_finishes(self):
        self.state()
        with patch.object(player, 'launch_owner_status', return_value={'verified': True, 'check': 'alive', 'legacy': True}):
            result = player.launch_state()
            self.assertTrue(result['current'] and result['running'] and result['cancellable'])
            self.assertEqual(result['runId'], 'attempt')
            player.cancel_launch()
        self.assertEqual(self.cancel.read_text(), 'cancel\n')

    def test_dead_or_recycled_worker_cannot_be_cancelled(self):
        self.state()
        with patch.object(player, 'launch_owner_status', return_value={'verified': False, 'check': 'exited', 'legacy': True}):
            self.assertFalse(player.launch_state()['current'])
            player.JOB.update(kind='launch', startedUtc=(self.now - timedelta(seconds=2)).isoformat())
            result = player.launch_state()
            self.assertTrue(result['current'])
            self.assertFalse(result['running'] or result['cancellable'])
            player.JOB['startedUtc'] = (self.now + timedelta(seconds=2)).isoformat()
            self.assertFalse(player.launch_state()['current'])
            with self.assertRaisesRegex(ValueError, 'No launch'):
                player.cancel_launch()
        self.assertFalse(self.cancel.exists())

    def test_legacy_live_worker_with_old_progress_is_recovery_not_new_launch(self):
        # The real report has the legacy pid/started fields, an Oct 4 preflight,
        # no current job, and a different currently selected build.
        import psutil
        created = datetime.fromtimestamp(psutil.Process(os.getpid()).create_time(), timezone.utc).isoformat()
        self.state(attemptId=None, requestedAt=None, pid=os.getpid(), started=created,
                   heartbeat='2026-10-04T15:54:55+01:00', buildId='lightfix2-20261001', phase='preflight',
                   message='Launching 3.7 + reflection + far lighting fix v2')
        result = player.launch_state()
        self.assertTrue(result['running'] and result['ownerVerified'] and result['ownerLegacy'])
        self.assertTrue(result['stalled'])
        self.assertEqual(result['ownerCheck'], 'alive')
        self.assertIn('Stuck launcher processes', result['message'])
        self.assertIn('far lighting fix', result['lastMessage'])
        with patch.object(player, 'processes', return_value=[]):
            with self.assertRaisesRegex(ValueError, 'Stuck launcher processes'):
                player.require_idle()

    def test_cancellation_that_is_not_acknowledged_offers_recovery(self):
        self.state()
        with patch.object(player, 'launch_owner_status', return_value={'verified': True, 'check': 'alive', 'legacy': True}):
            response = player.cancel_launch()
            self.assertIn('does not confirm', response)
            old = time.time() - 20
            os.utime(self.cancel, (old, old))
            result = player.launch_state()
            self.assertTrue(result['cancelRequested'] and result['stalled'])
            self.assertGreater(result['cancelRequestedAgeSeconds'], 10)

    def test_future_heartbeat_cannot_keep_old_worker_progress_fresh(self):
        self.state(heartbeat=(self.now + timedelta(days=1)).isoformat())
        with patch.object(player, 'launch_owner_status', return_value={'verified': True, 'check': 'alive', 'legacy': True}):
            result = player.launch_state()
            self.assertTrue(result['stalled'])
            self.assertLess(result['heartbeatAgeSeconds'], -5)

    def test_inaccessible_identity_is_not_reported_exited_or_safe_to_mutate(self):
        self.state()
        with patch.object(player, 'launch_owner_status', return_value={'verified': False, 'check': 'inaccessible', 'legacy': True, 'error': 5}):
            result = player.launch_state()
            self.assertFalse(result['running'] or result['ownerVerified'] or result['cancellable'])
            self.assertTrue(result['activityUnknown'])
            self.assertEqual(result['ownerWin32Error'], 5)
            with patch.object(player, 'processes', return_value=[]):
                with self.assertRaisesRegex(ValueError, 'cannot be checked'):
                    player.require_idle()
            handler = object.__new__(player.Handler)
            with self.assertRaisesRegex(ValueError, 'current operation'):
                handler.post_action('/api/stop', {})

    def test_real_owner_exit_clears_previous_busy_status_without_rewriting_receipt(self):
        import psutil
        child = subprocess.Popen([os.environ['SystemRoot'] + r'\System32\WindowsPowerShell\v1.0\powershell.exe',
                                  '-NoProfile', '-NonInteractive', '-Command', 'Start-Sleep -Milliseconds 600'],
                                 creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            created = datetime.fromtimestamp(psutil.Process(child.pid).create_time(), timezone.utc).isoformat()
            self.state(pid=child.pid, started=created, heartbeat='2026-10-04T15:54:55+01:00')
            original = (self.data / 'launch-state.json').read_bytes()
            first = player.launch_state()
            self.assertTrue(first['running'] and first['stalled'])
            child.wait(timeout=10)
            second = player.launch_state()
            self.assertFalse(second['current'] or second['running'] or second['cancellable'] or second['activityUnknown'])
            self.assertEqual(second['ownerCheck'], 'exited')
            self.assertEqual((self.data / 'launch-state.json').read_bytes(), original)
            with patch.object(player, 'processes', return_value=[]):
                player.require_idle()
        finally:
            child.wait(timeout=10)

    def test_real_helper_pid_visible_during_work_and_removed_after_exit(self):
        fixture = self.app / 'inert metadata.ps1'
        fixture.write_text("Start-Sleep -Milliseconds 800; Write-Output 'finished'", encoding='utf-8')
        result = []
        task = threading.Thread(target=lambda: result.append(player.powershell('-File', str(fixture), encoding='utf-8')))
        task.start()
        try:
            deadline = time.time() + 5
            workers = []
            while time.time() < deadline and not workers:
                workers = player.helper_worker_status()
                time.sleep(.02)
            self.assertEqual(len(workers), 1)
            self.assertGreater(workers[0]['pid'], 0)
            self.assertEqual(workers[0]['script'], fixture.name)
            self.assertFalse(workers[0]['cancellable'])
        finally:
            task.join(timeout=10)
        self.assertFalse(task.is_alive())
        self.assertEqual(player.helper_worker_status(), [])
        self.assertEqual(result[0].returncode, 0)
        trace = next(self.data.joinpath('logs').glob('helper-*.log')).read_text(encoding='utf-8')
        self.assertIn('Worker PID ', trace)

    def test_terminal_error_visible_only_for_current_launch(self):
        self.state(phase='failed', message='Injector rejected selected path')
        with patch.object(player, 'launch_owner_status', return_value={'verified': False, 'check': 'exited', 'legacy': True}):
            self.assertFalse(player.launch_state()['current'])

    def test_observed_startup_route_and_backend_problem_survive_status(self):
        self.state(gameStartRequested='steam', gameStartEffective='manual', gameStartLauncher=False,
                   gameTarget='fixture shipping executable', backendError='OpenXR fixture failure', backendLogCaptured=True,
                   steamTargetCount=0, steamTargetUnverifiedCount=1, steamTargetCandidateCount=1,
                   steamTargetProcesses=[{'pid':1234, 'pathReadable':False, 'matchesSelected':False}],
                   targetVerificationLost=True, backendEvidencePresent=True, backendRendererInitialized=True,
                   backendProjectionSeen=True)
        with patch.object(player, 'launch_owner_status', return_value={'verified': True, 'check': 'alive', 'legacy': True}):
            result = player.launch_state()
        self.assertEqual(result['gameStartRequested'], 'steam')
        self.assertEqual(result['gameStartEffective'], 'manual')
        self.assertFalse(result['gameStartLauncher'])
        self.assertEqual(result['backendError'], 'OpenXR fixture failure')
        self.assertTrue(result['backendLogCaptured'])
        self.assertEqual(result['steamTargetCount'], 0)
        self.assertEqual(result['steamTargetUnverifiedCount'], 1)
        self.assertEqual(result['steamTargetCandidateCount'], 1)
        self.assertEqual(result['steamTargetProcesses'][0]['pid'], 1234)
        self.assertFalse(result['steamTargetProcesses'][0]['pathReadable'])
        for key in ('targetVerificationLost', 'backendEvidencePresent', 'backendRendererInitialized', 'backendProjectionSeen'):
            self.assertTrue(result[key])

    def test_foreign_cancel_path_and_terminal_states_never_written(self):
        with patch.object(player, 'launch_owner_status', return_value={'verified': True, 'check': 'alive', 'legacy': True}):
            self.state(cancelPath=str(self.root / 'cancel.request'))
            self.assertFalse(player.launch_state()['cancellable'])
            for phase in ('failed', 'finished', 'cancelled'):
                self.state(phase=phase)
                self.assertFalse(player.launch_state()['cancellable'])
        self.assertFalse(self.cancel.exists())

    def test_active_worker_blocks_duplicate_launch_and_runtime_mutation(self):
        self.state()
        with patch.object(player, 'processes', return_value=[]), patch.object(player, 'launch_owner_status', return_value={'verified': True, 'check': 'alive', 'legacy': True}):
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
            with patch.object(player, 'launch_owner_status', return_value={'verified': True, 'check': 'alive', 'legacy': True}):
                with self.assertRaisesRegex(ValueError, 'current operation'):
                    handler.post_action('/api/stop', {})
            with patch.object(player, 'launch_owner_status', return_value={'verified': False, 'check': 'exited', 'legacy': True}), patch.dict(player.RECORDING, running=True):
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
        (self.cancel.parent / 'startup.log').write_text('reached preflight before becoming stuck')
        (self.cancel.parent / 'injector.stderr.log').write_text('x' * 32000 + '\nselected path was rejected')
        (self.cancel.parent / 'unrelated.txt').write_text('must not be copied')
        text = '\n'.join(player.startup_diagnostics({'runId': 'attempt'}))
        self.assertIn('early preflight failure', text)
        self.assertIn('reached preflight before becoming stuck', text)
        self.assertIn('selected path was rejected', text)
        self.assertNotIn('must not be copied', text)
        self.assertLess(len(text), 17000)
        self.assertEqual(player.startup_diagnostics({'runId': '../../'}), [])

    def test_backend_diagnostics_preserve_init_and_error_in_bounded_log(self):
        profile = player.config().profile
        profile.mkdir()
        text = '[UnrealVR] entry\n' + ('filler line\n' * 15000) + 'Could not create openxr session: XR_ERROR_RUNTIME_FAILURE\n'
        (profile / 'log.txt').write_text(text, encoding='utf-8')
        result = '\n'.join(player.backend_diagnostics({}))
        self.assertIn('[UnrealVR] entry', result)
        self.assertIn('XR_ERROR_RUNTIME_FAILURE', result)
        self.assertIn('compare timestamps', result)
        self.assertLess(len(result), 62000)
        self.assertEqual((profile / 'log.txt').read_text(encoding='utf-8'), text)

    def test_saved_backend_attempt_takes_precedence_over_new_game_run(self):
        player.config().profile.mkdir()
        (player.config().profile / 'log.txt').write_text('later unrelated game run')
        (self.cancel.parent / 'backend.log').write_text('recorded attempt initialization failure')
        result = '\n'.join(player.backend_diagnostics({'runId': 'attempt'}))
        self.assertIn('saved for attempt attempt', result)
        self.assertIn('recorded attempt initialization failure', result)
        self.assertNotIn('later unrelated', result)

    def test_backend_diagnostic_keeps_error_between_header_and_tail(self):
        player.config().profile.mkdir()
        content = 'line\n' * 55 + '[error] Could not create openxr session: fixture\n' + 'line\n' * 170
        (player.config().profile / 'log.txt').write_text(content)
        self.assertIn('Could not create openxr session: fixture', '\n'.join(player.backend_diagnostics({})))

    def test_backend_log_missing_traversal_and_personal_path_redaction(self):
        (self.root / 'backend.log').write_text('outside attempt secret')
        result = '\n'.join(player.backend_diagnostics({'runId': '../../'}))
        self.assertIn('No readable UEVR backend log', result)
        self.assertNotIn('outside attempt secret', result)
        player.config().profile.mkdir()
        (player.config().profile / 'log.txt').write_text(r'C:\Users\PrivateFixture\game\backend.dll')
        with patch.dict(os.environ, USERPROFILE=r'C:\Users\PrivateFixture', USERNAME='PrivateFixture'):
            result = '\n'.join(player.backend_diagnostics({}))
        self.assertNotIn('PrivateFixture', result)
        self.assertIn('%USERPROFILE%', result)

    def test_repeated_frame_failures_do_not_hide_window_probe_evidence(self):
        rows = [f'ordinary header {i}' for i in range(50)]
        rows += ['[info] [WuWaD3DWindow] GetHwnd failed; GetDesc selected valid window']
        rows += [f'[2026-10-06 16:31:11.{i:03d}] [UnrealVR] [error] Failed to initialize Framework on DirectX 11' for i in range(60)]
        rows += ['[info] Device or SwapChain null. DirectX 12 may be in use.']
        rows += [f'ordinary tail {i}' for i in range(190)]
        result = '\n'.join(player.compact_backend_rows(rows))
        self.assertIn('GetDesc selected valid window', result)
        self.assertIn('Device or SwapChain null', result)
        self.assertIn('repeated 59 more times', result)
        self.assertIn('16:31:11.059', result)
        self.assertLess(len(result.splitlines()), 181)

    def test_distinct_probe_and_error_survive_interleaved_error_flood(self):
        rows = [f'header {i}' for i in range(50)] + ['[error] Unique startup failure']
        rows += ['[info] [WuWaD3DProbe] switching to DirectX 11']
        for i in range(80):
            rows += [f'[2026-10-06 16:31:11.{i:03d}] [error] repetitive failure', f'frame {i}']
        rows += [f'tail {i}' for i in range(190)]
        result = '\n'.join(player.compact_backend_rows(rows))
        self.assertIn('Unique startup failure', result)
        self.assertIn('switching to DirectX 11', result)
        self.assertEqual(result.count('repetitive failure'), 1)


if __name__ == '__main__':
    unittest.main()
