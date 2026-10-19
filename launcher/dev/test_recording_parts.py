"""Recording parts: a part that ran its full time starts the next (same id and stop file)."""
import importlib.util
import json
from pathlib import Path
import tempfile
import types
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('player_parts', Path(__file__).with_name('wuwa_player.py'))
player = importlib.util.module_from_spec(spec)
spec.loader.exec_module(player)


class RecordingParts(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='WuWa parts ')
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        (root / 'app/dev').mkdir(parents=True)
        (root / 'app/dev/record-wuwa.py').write_text('', encoding='utf-8')
        (root / 'data').mkdir()
        self.addCleanup(patch.stopall)
        patch.object(player, 'CONFIG', player.Config(root / 'app', root / 'data', root / 'profile')).start()
        patch.dict(player.RECORDING, {'running': False, 'stopFile': '', 'folder': '', 'id': '', 'pid': None,
                                      'stopping': False}, clear=True).start()
        self.calls = []

    def run_with(self, endings, fail_at=None):
        """endings: each part's recorder end_reason; fail_at: part index whose record() raises."""
        def record(args):
            index = len(self.calls)
            self.calls.append((Path(args.output), Path(args.stop_file), args.seconds, dict(player.RECORDING)))
            if index == fail_at:
                raise RuntimeError('Game process changed')
            args.output.mkdir(parents=True)
            (args.output / 'recording.json').write_text(json.dumps({'end_reason': endings[index]}), encoding='utf-8')
            if endings[index] == 'stopped':
                Path(args.stop_file).write_text('stop', encoding='ascii')
        fake = types.SimpleNamespace(record=record)
        loader = types.SimpleNamespace(exec_module=lambda module: module.__dict__.update(record=record))
        with patch.object(player.importlib.util, 'spec_from_file_location', return_value=types.SimpleNamespace(loader=loader)), \
             patch.object(player.importlib.util, 'module_from_spec', return_value=types.SimpleNamespace()):
            return player.record_playtest()

    def test_full_parts_continue_until_stopped(self):
        message = self.run_with(['time_limit', 'time_limit', 'stopped'])
        folders = [c[0] for c in self.calls]
        self.assertEqual(len(folders), 3)
        self.assertEqual(folders[1].name, folders[0].name + '-part2')
        self.assertEqual(folders[2].name, folders[0].name + '-part3')
        self.assertEqual(len({c[1] for c in self.calls}), 1, 'every part shares one stop file')
        self.assertEqual(len({c[3]['id'] for c in self.calls}), 1, 'every part keeps the recording id')
        self.assertTrue(all(c[2] == 300 for c in self.calls))
        self.assertEqual(self.calls[1][3]['folder'], str(folders[1]), 'the snapshot reports the current part')
        self.assertIn('+2 more parts', message)
        self.assertFalse(player.RECORDING['running'])

    def test_game_exit_or_pause_end_ends_the_recording(self):
        self.run_with(['game_exited'])
        self.assertEqual(len(self.calls), 1)

    def test_simulator_without_end_reason_records_one_part(self):
        self.run_with([None])
        self.assertEqual(len(self.calls), 1)

    def test_failure_after_a_saved_part_keeps_it(self):
        message = self.run_with(['time_limit', 'time_limit'], fail_at=1)
        self.assertEqual(len(self.calls), 2)
        self.assertIn('Recording saved', message)

    def test_first_part_failure_is_reported(self):
        with self.assertRaises(RuntimeError):
            self.run_with(['time_limit'], fail_at=0)

    def test_long_sessions_are_not_cut_at_an_hour(self):
        message = self.run_with(['time_limit'] * 19 + ['stopped'])
        self.assertEqual(len(self.calls), 20)
        self.assertIn('+19 more parts', message)

    def test_low_space_ends_between_parts_and_keeps_them(self):
        free = iter([50 * 1024 ** 3, 50 * 1024 ** 3, 5 * 1024 ** 3])
        with patch.object(player, 'recording_space_left', lambda folder: next(free)):
            message = self.run_with(['time_limit'] * 5)
        self.assertEqual(len(self.calls), 2)
        self.assertIn('less than 10 GB free', message)
        self.assertIn('+1 more parts', message)

    def test_low_space_before_the_first_part_is_an_error(self):
        with patch.object(player, 'recording_space_left', lambda folder: 1024 ** 3):
            with self.assertRaises(ValueError):
                self.run_with(['time_limit'])
        self.assertEqual(self.calls, [])


if __name__ == '__main__':
    unittest.main()
