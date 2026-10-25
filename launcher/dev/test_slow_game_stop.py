"""Slow-game stop (1.2.7): the in-game rule reaches the launcher, the recorder command carries it, and a part the
recorder ended for it ends the recording with a warning and a status the game shows."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import types
import unittest
from unittest.mock import patch

HERE = Path(__file__).parent


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, HERE / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


bridge_module = load('video_bridge_slow', 'wuwa_video_bridge.py')
player = load('player_slow', 'wuwa_player.py')
recorder = load('record_slow', 'record-wuwa.py')


class BridgeRule(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='WuWa slow ')
        self.addCleanup(self.temp.cleanup)
        self.profile = Path(self.temp.name) / 'profile'
        self.profile.mkdir()
        self.started = []
        self.bridge = bridge_module.Bridge(
            self.profile, lambda: {'pid': 42, 'created_ms': 1000},
            lambda *a, **k: self.started.append((a, k)), lambda *a: None,
            lambda: {'available': True, 'state': 'idle'}, clock=lambda: 10_000)
        self.serial = 0

    def send(self, **extra):
        self.serial += 1
        request = dict(version=1, session=self.bridge.session, id=f's{self.serial}', pid=42, created_ms=1000,
                       expires_ms=12_000, action='start', fps=30, eye_width=1024, telemetry=True, sound=True)
        request.update(extra)
        (self.profile / 'video-control').mkdir(exist_ok=True)
        (self.profile / 'video-control/request-42.json').write_text(json.dumps(request), encoding='utf-8')
        self.bridge.step()
        return self.bridge.last_request

    def test_rule_reaches_the_launcher(self):
        self.assertTrue(self.send(slow_fps=8, slow_seconds=30)['ok'])
        self.assertTrue(self.send(slow_fps=0)['ok'])  # off
        self.assertTrue(self.send()['ok'])  # absent: the launcher's default
        self.assertEqual(self.started, [
            ((False, 30, 1024, 42), {'sound': True, 'slow_fps': 8, 'slow_seconds': 30}),
            ((False, 30, 1024, 42), {'sound': True, 'slow_fps': 0}),
            ((False, 30, 1024, 42), {'sound': True})])

    def test_bad_rule_is_refused(self):
        for bad in (-1, 31, '5', 2.5, True):
            reply = self.send(slow_fps=bad)
            self.assertFalse(reply['ok'])
            self.assertIn('slow-game', reply['message'])
        for bad in (4, 121, '20', None):
            reply = self.send(slow_seconds=bad)
            self.assertFalse(reply['ok'])
        self.assertEqual(self.started, [])


class RecorderCommand(unittest.TestCase):
    def command(self, **extra):
        with tempfile.TemporaryDirectory() as d:
            folder = Path(d)
            exe, openvr = folder / 'wuwa-recorder.exe', folder / 'openvr_api.dll'
            exe.write_bytes(b''), openvr.write_bytes(b'')
            values = dict(output=folder / 'out', seconds=30, fps=30, eye_width=1024, stop_file=folder / 'stop',
                          recorder=exe, openvr=openvr, source='steamvr')
            values.update(extra)
            client = types.SimpleNamespace(pid=42, simulator=folder / 'sim', created=0)
            return recorder.recorder_command(argparse.Namespace(**values), client)[1]

    def test_flags_only_when_on(self):
        command = self.command(min_game_fps=5, slow_seconds=20)
        self.assertIn('--min-game-fps=5', command)
        self.assertIn('--slow-seconds=20', command)
        # Options come before openvr_api.dll.
        self.assertLess(command.index('--slow-seconds=20'), next(i for i, c in enumerate(command) if c.endswith('openvr_api.dll')))
        for off in (dict(), dict(min_game_fps=0), dict(min_game_fps=0, slow_seconds=40)):
            self.assertFalse(any(c.startswith(('--min-game-fps', '--slow-seconds')) for c in self.command(**off)))

    def test_bad_rule_is_refused(self):
        for bad in (dict(min_game_fps=61), dict(min_game_fps='5'), dict(min_game_fps=5, slow_seconds=4),
                    dict(min_game_fps=5, slow_seconds=301)):
            with self.assertRaises(ValueError):
                self.command(**bad)


class SlowStopEndsTheRecording(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='WuWa slow parts ')
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        (root / 'app/dev').mkdir(parents=True)
        (root / 'app/dev/record-wuwa.py').write_text('', encoding='utf-8')
        (root / 'data').mkdir()
        self.addCleanup(patch.stopall)
        patch.object(player, 'CONFIG', player.Config(root / 'app', root / 'data', root / 'profile')).start()
        patch.dict(player.RECORDING, {'running': False, 'stopFile': '', 'folder': '', 'id': '', 'pid': None,
                                      'stopping': False}, clear=True).start()
        patch.dict(os.environ, {'WUWA_VR_NO_DIALOG': '1'}).start()
        player.RECORDING_STOP.clear()
        self.args = []

    def run_with(self, reports, **rule):
        def record(args):
            report = reports[len(self.args)]
            self.args.append(args)
            args.output.mkdir(parents=True)
            (args.output / 'recording.json').write_text(json.dumps(report), encoding='utf-8')
        loader = types.SimpleNamespace(exec_module=lambda module: module.__dict__.update(record=record))
        with patch.object(player.importlib.util, 'spec_from_file_location', return_value=types.SimpleNamespace(loader=loader)), \
             patch.object(player.importlib.util, 'module_from_spec', return_value=types.SimpleNamespace()):
            return player.record_playtest(**rule)

    def test_slow_part_ends_the_recording_and_is_reported(self):
        message = self.run_with([{'end_reason': 'time_limit'},
                                 {'end_reason': 'slow_game', 'slow_game': {'fps': 3.1, 'at_s': 140.2}},
                                 {'end_reason': 'time_limit'}], slow_fps=6, slow_seconds=25)
        self.assertEqual(len(self.args), 2, 'no part after the slow one')
        self.assertEqual([(a.min_game_fps, a.slow_seconds) for a in self.args], [(6, 25)] * 2)
        self.assertIn('below 6 fps for 25 seconds', message)
        self.assertIn('3.1 fps', message)
        self.assertEqual(player.RECORDING_STOP['reason'], 'slow_game')
        self.assertEqual((player.RECORDING_STOP['min_fps'], player.RECORDING_STOP['seconds'], player.RECORDING_STOP['fps']), (6, 25, 3.1))

    def test_default_rule_and_a_new_recording_clears_the_last_stop(self):
        self.run_with([{'end_reason': 'slow_game', 'slow_game': {'fps': 2}}])
        self.assertEqual((self.args[0].min_game_fps, self.args[0].slow_seconds), (5, 20))
        self.assertEqual(player.RECORDING_STOP['reason'], 'slow_game')
        self.args.clear()
        player.RECORDING.update(running=False)
        message = self.run_with([{'end_reason': 'stopped'}])
        self.assertEqual(player.RECORDING_STOP, {})
        self.assertIn('Recording saved', message)

    def test_rule_off_and_bad_values(self):
        self.run_with([{'end_reason': 'stopped'}], slow_fps=0)
        self.assertEqual(self.args[0].min_game_fps, 0)
        for bad in (dict(slow_fps=31), dict(slow_fps='5'), dict(slow_seconds=4), dict(slow_seconds=121)):
            with self.assertRaises(ValueError):
                player.check_slow_game(bad.get('slow_fps', 5), bad.get('slow_seconds', 20))

    def test_snapshot_carries_the_stop(self):
        player.RECORDING_STOP.update(reason='slow_game', ms=5, min_fps=5, seconds=20, fps=2.5)
        with patch.object(player, 'microphones', return_value=[]):
            snap = player.recording_snapshot()
        self.assertEqual(snap['stop'], {'reason': 'slow_game', 'ms': 5, 'min_fps': 5, 'seconds': 20, 'fps': 2.5})


if __name__ == '__main__':
    unittest.main()
