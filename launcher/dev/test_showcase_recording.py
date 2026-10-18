"""Showcase recording style: the in-game request, the launcher job and the recorder command."""
import argparse
import importlib.util
import json
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


bridge_module = load('video_bridge_showcase', 'wuwa_video_bridge.py')
player = load('player_showcase', 'wuwa_player.py')
recorder = load('record_showcase', 'record-wuwa.py')


class BridgeStyle(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='WuWa showcase ')
        self.addCleanup(self.temp.cleanup)
        self.profile = Path(self.temp.name) / 'profile'
        self.profile.mkdir()
        self.started = []
        self.bridge = bridge_module.Bridge(
            self.profile, lambda: {'pid': 42, 'created_ms': 1000},
            lambda *a, **k: self.started.append((a, k)), lambda *a: None,
            lambda: {'available': True, 'state': 'idle'}, clock=lambda: 10_000)

    def send(self, **extra):
        request = dict(version=1, session=self.bridge.session, id='r' + str(len(self.started)) + str(len(extra)),
                       pid=42, created_ms=1000, expires_ms=12_000, action='start', fps=60, eye_width=1280,
                       telemetry=False)
        request.update(extra)
        (self.profile / 'video-control').mkdir(exist_ok=True)
        (self.profile / 'video-control/request-42.json').write_text(json.dumps(request), encoding='utf-8')
        self.bridge.step()
        return self.bridge.last_request

    def test_showcase_reaches_the_launcher(self):
        self.assertTrue(self.send(style='showcase')['ok'])
        self.assertEqual(self.started, [((True, 60, 1280, 42), {'style': 'showcase'})])

    def test_showcase_height_reaches_the_launcher(self):
        self.assertTrue(self.send(style='showcase', showcase_pitch=-15)['ok'])
        self.assertEqual(self.started, [((True, 60, 1280, 42), {'style': 'showcase', 'pitch': -15})])

    def test_showcase_height_out_of_range_is_refused(self):
        for n, bad in enumerate((-41, 21, 1.5, '5')):
            reply = self.send(style='showcase', showcase_pitch=bad, **{'id': f'bad{n}'})
            self.assertFalse(reply['ok'])
            self.assertIn('height', reply['message'])
        self.assertEqual(self.started, [])

    def test_older_plugins_keep_the_side_by_side_call(self):
        self.assertTrue(self.send()['ok'])
        self.assertTrue(self.send(style='sbs')['ok'])
        self.assertEqual(self.started, [((True, 60, 1280, 42), {})] * 2)

    def test_sound_and_large_sizes_reach_the_launcher(self):
        self.assertTrue(self.send(sound=False, eye_width=2048)['ok'])
        self.assertTrue(self.send(style='showcase', sound=True, eye_width=1536, id='s2')['ok'])
        self.assertEqual(self.started, [((True, 60, 2048, 42), {'sound': False}),
                                        ((True, 60, 1536, 42), {'style': 'showcase', 'sound': True})])
        reply = self.send(sound='yes', id='s3')
        self.assertFalse(reply['ok'])
        self.assertIn('sound', reply['message'])
        reply = self.send(eye_width=1920, id='s4')
        self.assertFalse(reply['ok'])

    def test_unknown_style_is_refused(self):
        reply = self.send(style='desktop')
        self.assertFalse(reply['ok'])
        self.assertIn('style', reply['message'])
        self.assertEqual(self.started, [])


class RecorderCommand(unittest.TestCase):
    def args(self, folder, **extra):
        exe, openvr = folder / 'wuwa-recorder.exe', folder / 'openvr_api.dll'
        exe.write_bytes(b''), openvr.write_bytes(b'')
        values = dict(output=folder / 'out', seconds=30, fps=60, eye_width=1280, stop_file=folder / 'stop',
                      recorder=exe, openvr=openvr, source='steamvr')
        values.update(extra)
        return argparse.Namespace(**values)

    def test_showcase_flag_goes_first(self):
        with tempfile.TemporaryDirectory() as d:
            client = types.SimpleNamespace(pid=42, simulator=Path(d) / 'sim', created=0)
            source, command = recorder.recorder_command(self.args(Path(d), style='showcase'), client)
            self.assertEqual(source, 'steamvr')
            self.assertEqual(command[1], '--showcase')
            self.assertTrue(command[2].endswith('openvr_api.dll'))
            self.assertEqual(command[3:], ['42', str((Path(d) / 'out').resolve()), '30', '60', '1280',
                                           str((Path(d) / 'stop').resolve())])
            source, command = recorder.recorder_command(self.args(Path(d)), client)
            self.assertNotIn('--showcase', command)

    def test_showcase_pitch_follows_the_flag(self):
        with tempfile.TemporaryDirectory() as d:
            client = types.SimpleNamespace(pid=42, simulator=Path(d) / 'sim', created=0)
            source, command = recorder.recorder_command(self.args(Path(d), style='showcase', pitch=-15), client)
            self.assertEqual(command[1:3], ['--showcase', '--pitch=-15'])
            source, command = recorder.recorder_command(self.args(Path(d), style='sbs', pitch=-15), client)
            self.assertFalse(any(c.startswith('--pitch') for c in command))
            with self.assertRaisesRegex(ValueError, 'pitch'):
                recorder.recorder_command(self.args(Path(d), style='showcase', pitch=30), client)

    def test_sound_flag_for_steamvr(self):
        with tempfile.TemporaryDirectory() as d:
            client = types.SimpleNamespace(pid=42, simulator=Path(d) / 'sim', created=0)
            source, command = recorder.recorder_command(self.args(Path(d), sound=True, eye_width=2048), client)
            self.assertEqual(command[1], '--sound')
            self.assertEqual(command[-2], '2048')
            source, command = recorder.recorder_command(self.args(Path(d), style='showcase', pitch=-10, sound=True), client)
            self.assertEqual(command[1:4], ['--showcase', '--pitch=-10', '--sound'])
            source, command = recorder.recorder_command(self.args(Path(d)), client)
            self.assertNotIn('--sound', command)

    def test_simulator_stops_at_1280(self):
        with tempfile.TemporaryDirectory() as d:
            client = types.SimpleNamespace(pid=42, simulator=Path(d) / 'sim', created=0)
            client.simulator.mkdir()
            (client.simulator / 'runtime_status.json').write_text('{}', encoding='utf-8')
            with patch.object(recorder.live, 'fresh_json', return_value={'runtime_pid': 42, 'video_stream_version': 1}):
                with self.assertRaisesRegex(RuntimeError, '1280'):
                    recorder.recorder_command(self.args(Path(d), eye_width=1536, source='auto'), client)

    def test_simulator_refuses_showcase(self):
        with tempfile.TemporaryDirectory() as d:
            client = types.SimpleNamespace(pid=42, simulator=Path(d) / 'sim', created=0)
            client.simulator.mkdir()
            (client.simulator / 'runtime_status.json').write_text('{}', encoding='utf-8')
            with patch.object(recorder.live, 'fresh_json', return_value={'runtime_pid': 42, 'video_stream_version': 1}):
                with self.assertRaisesRegex(RuntimeError, 'needs SteamVR'):
                    recorder.recorder_command(self.args(Path(d), style='showcase', source='auto'), client)

    def test_unknown_style_is_refused(self):
        with tempfile.TemporaryDirectory() as d:
            client = types.SimpleNamespace(pid=42, simulator=Path(d) / 'sim', created=0)
            with self.assertRaises(ValueError):
                recorder.recorder_command(self.args(Path(d), style='mono'), client)


class LauncherJob(unittest.TestCase):
    def test_style_reaches_every_part(self):
        with tempfile.TemporaryDirectory(prefix='WuWa showcase job ') as d:
            root = Path(d)
            (root / 'app/dev').mkdir(parents=True)
            (root / 'data').mkdir()
            seen = []

            def record(args):
                seen.append(args.style)
                args.output.mkdir(parents=True)
                (args.output / 'recording.json').write_text(json.dumps({'end_reason': 'stopped'}), encoding='utf-8')
            loader = types.SimpleNamespace(exec_module=lambda module: module.__dict__.update(record=record))
            with patch.object(player, 'CONFIG', player.Config(root / 'app', root / 'data', root / 'profile')), \
                 patch.dict(player.RECORDING, {'running': False, 'stopFile': '', 'folder': '', 'id': '', 'pid': None,
                                               'stopping': False}, clear=True), \
                 patch.object(player.importlib.util, 'spec_from_file_location', return_value=types.SimpleNamespace(loader=loader)), \
                 patch.object(player.importlib.util, 'module_from_spec', return_value=types.SimpleNamespace()):
                message = player.record_playtest(style='showcase')
            self.assertEqual(seen, ['showcase'])
            self.assertIn('showcase.mp4', message)
            with self.assertRaises(ValueError):
                player.record_playtest(style='mono')


if __name__ == '__main__':
    unittest.main()
