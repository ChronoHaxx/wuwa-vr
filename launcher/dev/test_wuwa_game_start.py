"""Inert filesystem/settings tests. Never launch Steam, inject, or read a real profile."""
import importlib.util
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


def load(name):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


game = load('wuwa_game_start')
player = load('wuwa_player')


class SteamStartTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='wuwa-steam-fixture-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.apps = self.root / 'steamapps'
        self.install = self.apps / 'common/Wuthering Waves'
        self.bootstrap = self.install / 'Wuthering Waves.exe'
        self.shipping = self.install / 'Client/Binaries/Win64/Client-Win64-Shipping.exe'
        for file in (self.bootstrap, self.shipping):
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text('inert fixture, never execute')
        self.manifest = self.apps / 'appmanifest_3513350.acf'
        self.manifest.write_text('"AppState" { "appid" "3513350" "installdir" "Wuthering Waves" }')
        self.official = self.root / 'official/launcher.exe'
        self.official.parent.mkdir()
        self.official.write_text('inert fixture')

    def test_exact_identity_and_required_shipping(self):
        self.assertEqual(game.validate_steam(str(self.bootstrap)), str(self.bootstrap))
        self.shipping.unlink()
        with self.assertRaises(ValueError):
            game.validate_steam(str(self.bootstrap))

    def test_manifest_mismatch_duplicate_and_path_rejection(self):
        for text in ('"appid" "123" "installdir" "Wuthering Waves"',
                     '"appid" "3513350" "installdir" "Other Game"',
                     '"appid" "3513350" "appid" "3513350" "installdir" "Wuthering Waves"'):
            self.manifest.write_text(text)
            with self.assertRaises(ValueError):
                game.validate_steam(str(self.bootstrap))
        for path in ('Wuthering Waves.exe', r'\\server\steamapps\common\Wuthering Waves\Wuthering Waves.exe', str(self.bootstrap) + '\n'):
            # Trailing whitespace is harmlessly trimmed by the settings contract;
            # injected embedded newlines are rejected.
            if path.endswith('\n'):
                path = str(self.bootstrap).replace('common', 'com\nmon')
            with self.assertRaises(ValueError):
                game.validate_steam(path)

    def test_library_metadata_discovers_valid_install_only(self):
        other = self.root / 'Steam client'
        (other / 'steamapps').mkdir(parents=True)
        escaped = str(self.root).replace('\\', '\\\\')
        (other / 'steamapps/libraryfolders.vdf').write_text('"libraryfolders" { "1" { "path" "' + escaped + '" } }')
        self.assertEqual(game.steam_candidates([str(other)]), [str(self.bootstrap)])
        self.manifest.write_text('"appid" "3513350" "installdir" "../../elsewhere"')
        self.assertEqual(game.steam_candidates([str(other)]), [])

    def test_saved_choices_are_not_replaced_by_discovery(self):
        detected = [{'kind': 'steam', 'path': str(self.bootstrap)}, {'kind': 'official', 'path': str(self.official)}]
        for mode, path in (('manual', ''), ('launcher', str(self.official)), ('steam', str(self.bootstrap))):
            with patch.object(player, 'detect_games', return_value=detected), patch.object(player, 'settings', return_value={'gameStart': mode, 'gameLauncher': path}):
                actual = player.game_status()
                self.assertEqual((actual['mode'], actual['problem']), (mode, ''))
                self.assertEqual(Path(actual['launcher']).resolve(), Path(path).resolve())
        with patch.object(player, 'detect_games', return_value=detected), patch.object(player, 'settings', return_value={'gameStart': 'steam', 'gameLauncher': str(self.root / 'missing.exe')}):
            actual = player.game_status()
            self.assertEqual(actual['mode'], 'steam')
            self.assertTrue(actual['problem'])

    def test_settings_endpoint_validates_before_save(self):
        handler = object.__new__(player.Handler)
        with patch.object(player, 'save_settings') as save, patch.dict(player.JOB, running=False):
            self.assertTrue(handler.post_action('/api/settings', {'gameStart': 'steam', 'gameLauncher': str(self.bootstrap)})['ok'])
            save.assert_called_once_with({'gameStart': 'steam', 'gameLauncher': str(self.bootstrap)})
            save.reset_mock()
            with self.assertRaises(ValueError):
                handler.post_action('/api/settings', {'gameStart': 'steam', 'gameLauncher': str(self.official)})
            save.assert_not_called()

    def test_missing_saved_choice_only_defaults_to_sole_steam(self):
        with patch.object(player, 'settings', return_value={}), patch.object(player, 'detect_games', return_value=[{'kind': 'steam', 'path': str(self.bootstrap)}]):
            self.assertEqual(player.game_status()['mode'], 'steam')
        with patch.object(player, 'settings', return_value={}), patch.object(player, 'detect_games', return_value=[{'kind': 'steam', 'path': str(self.bootstrap)}, {'kind': 'official', 'path': str(self.official)}]):
            self.assertTrue(player.game_status()['problem'])
        with patch.object(player, 'settings', return_value={'gameStart': 'unknown'}), patch.object(player, 'detect_games', return_value=[{'kind': 'steam', 'path': str(self.bootstrap)}]):
            self.assertTrue(player.game_status()['problem'])


if __name__ == '__main__':
    unittest.main()
