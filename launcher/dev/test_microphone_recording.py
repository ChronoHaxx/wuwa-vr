"""Microphone in recordings (1.2.6): the in-game request, the launcher's list and check, and the recorder command."""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import time
import types
import unittest
from unittest.mock import patch

HERE = Path(__file__).parent
BEYOND = '{0.0.1.00000000}.{75b22807-fd1e-4d5f-b0de-9166b18ff01d}'
BROADCAST = '{0.0.1.00000000}.{c9b90578-0d83-4e12-872c-a9b025bf91dc}'


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, HERE / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


bridge_module = load('video_bridge_microphone', 'wuwa_video_bridge.py')
player = load('player_microphone', 'wuwa_player.py')
recorder = load('record_microphone', 'record-wuwa.py')


class BridgeMicrophone(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='WuWa microphone ')
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
        request = dict(version=1, session=self.bridge.session, id=f'm{self.serial}', pid=42, created_ms=1000,
                       expires_ms=12_000, action='start', fps=30, eye_width=1024, telemetry=True, sound=True)
        request.update(extra)
        (self.profile / 'video-control').mkdir(exist_ok=True)
        (self.profile / 'video-control/request-42.json').write_text(json.dumps(request), encoding='utf-8')
        self.bridge.step()
        return self.bridge.last_request

    def test_microphone_and_volume_reach_the_launcher(self):
        self.assertTrue(self.send(microphone=BEYOND, mic_volume=150)['ok'])
        self.assertTrue(self.send(microphone='default')['ok'])
        self.assertTrue(self.send(style='showcase', microphone='default', mic_volume=0)['ok'])
        self.assertEqual(self.started, [
            ((False, 30, 1024, 42), {'sound': True, 'microphone': BEYOND, 'mic_volume': 150}),
            ((False, 30, 1024, 42), {'sound': True, 'microphone': 'default', 'mic_volume': 100}),
            ((False, 30, 1024, 42), {'style': 'showcase', 'sound': True, 'microphone': 'default', 'mic_volume': 0})])

    def test_no_microphone_keeps_the_old_call(self):
        self.assertTrue(self.send()['ok'])
        self.assertTrue(self.send(microphone='')['ok'])
        self.assertEqual(self.started, [((False, 30, 1024, 42), {'sound': True})] * 2)

    def test_bad_microphone_or_volume_is_refused(self):
        for bad in ('--sound', 'a b', 7, 'x' * 201):
            reply = self.send(microphone=bad)
            self.assertFalse(reply['ok'])
            self.assertIn('microphone', reply['message'])
        for bad in (-1, 301, '100', 1.5, True):
            reply = self.send(microphone='default', mic_volume=bad)
            self.assertFalse(reply['ok'])
            self.assertIn('volume', reply['message'])
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

    def test_microphone_flags_follow_the_sound_flag(self):
        command = self.command(sound=True, microphone=BEYOND, mic_volume=150)
        self.assertEqual(command[1:4], ['--sound', '--microphone=' + BEYOND, '--mic-volume=150'])
        self.assertTrue(command[4].endswith('openvr_api.dll'))
        # The voice alone (game sound off) and the showcase view.
        command = self.command(style='showcase', microphone='default')
        self.assertEqual(command[1:4], ['--showcase', '--microphone=default', '--mic-volume=100'])

    def test_no_microphone_no_flags(self):
        for extra in ({}, {'microphone': ''}, {'microphone': None}):
            command = self.command(sound=True, **extra)
            self.assertFalse(any(c.startswith(('--microphone', '--mic-volume')) for c in command))

    def test_bad_microphone_or_volume_is_refused(self):
        for bad in ('--x', 'Default', '{0.0.1}.{nope}', BEYOND + ' x'):
            with self.assertRaisesRegex(ValueError, 'microphone'):
                self.command(microphone=bad)
        with self.assertRaisesRegex(ValueError, 'volume'):
            self.command(microphone='default', mic_volume=301)


class LauncherMicrophones(unittest.TestCase):
    def setUp(self):
        self.saved = dict(player.MICROPHONES)
        self.addCleanup(player.MICROPHONES.update, self.saved)

    def known(self, items):
        player.MICROPHONES.update(list=items, at=time.monotonic(), busy=False)

    def test_check_refuses_an_unplugged_microphone(self):
        self.known([{'id': BEYOND, 'name': 'Microphone (Beyond)', 'default': True}])
        player.check_microphone('', 100)
        player.check_microphone('default', 100)
        player.check_microphone(BEYOND, 300)
        with self.assertRaisesRegex(ValueError, 'not connected'):
            player.check_microphone(BROADCAST, 100)
        for bad in ('--sound', 7, None):
            with self.assertRaisesRegex(ValueError, 'microphone'):
                player.check_microphone(bad, 100)
        for bad in (-1, 301, '100', 1.5):
            with self.assertRaisesRegex(ValueError, 'volume'):
                player.check_microphone('default', bad)

    def test_unknown_list_leaves_the_choice_to_the_recorder(self):
        self.known([])
        player.check_microphone(BROADCAST, 100)

    def test_list_from_the_recorder(self):
        with tempfile.TemporaryDirectory() as d:
            app = Path(d)
            (app / 'dev-tools').mkdir()
            (app / 'dev-tools/wuwa-recorder.exe').write_bytes(b'')
            listed = {'microphones': [
                {'id': BEYOND, 'name': 'Microphone (Beyond)', 'default': True},
                {'id': BROADCAST, 'name': 'Microphone (NVIDIA Broadcast)', 'default': False},
                {'id': '--sound', 'name': 'injected', 'default': False},
                {'id': BROADCAST.replace('c9', 'd9'), 'name': 7},
                'junk']}
            done = subprocess.CompletedProcess([], 0, stdout=(json.dumps(listed) + '\r\n').encode('ascii'), stderr=b'')
            with patch.object(player, 'config', return_value=types.SimpleNamespace(app=app)), \
                    patch.object(player.subprocess, 'run', return_value=done) as run:
                player.refresh_microphones()
            self.assertEqual(run.call_args[0][0][1], '--list-microphones')
            self.assertEqual(player.MICROPHONES['list'], [
                {'id': BEYOND, 'name': 'Microphone (Beyond)', 'default': True},
                {'id': BROADCAST, 'name': 'Microphone (NVIDIA Broadcast)', 'default': False}])
            # An older recorder (no such option) or a crash: an empty list, then the last good list is kept.
            with patch.object(player, 'config', return_value=types.SimpleNamespace(app=app)), \
                    patch.object(player.subprocess, 'run', return_value=subprocess.CompletedProcess([], 1, b'', b'Unknown option')):
                player.refresh_microphones()
            self.assertEqual(player.MICROPHONES['list'], [])
            self.known([{'id': BEYOND, 'name': 'Microphone (Beyond)', 'default': True}])
            with patch.object(player, 'config', return_value=types.SimpleNamespace(app=app)), \
                    patch.object(player.subprocess, 'run', side_effect=subprocess.TimeoutExpired('x', 5)):
                player.refresh_microphones()
            self.assertEqual(len(player.MICROPHONES['list']), 1)
            self.assertFalse(player.MICROPHONES['busy'])


if __name__ == '__main__':
    unittest.main()
