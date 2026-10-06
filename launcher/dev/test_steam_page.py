"""Game-route selection over real HTTP, with all OS/game actions disabled."""
from pathlib import Path
import tempfile
import unittest
from playwright.sync_api import sync_playwright, expect
from test_wuwa_incident_page import Fixture


class SteamPageTests(unittest.TestCase):
    def test_routes_custom_path_and_saved_choice(self):
        with tempfile.TemporaryDirectory() as directory, sync_playwright() as pw:
            fixture = Fixture(Path(directory))
            official = r'C:\Games\Wuthering Waves\launcher.exe'
            steam = r'D:\SteamLibrary\steamapps\common\Wuthering Waves\Wuthering Waves.exe'
            fixture.status['game'] = {'mode': 'steam', 'launcher': steam, 'saved': True, 'problem': '',
                'detected': [{'kind': 'official', 'path': official, 'label': 'Official'},
                             {'kind': 'steam', 'path': steam, 'label': 'Steam'}]}
            calls = []
            base = fixture.server.RequestHandlerClass
            class Handler(base):
                def post_action(self, path, body):
                    if path != '/api/settings':
                        raise ValueError('Only inert settings allowed')
                    calls.append(body)
                    fixture.status['game'].update(mode=body['gameStart'], launcher=body.get('gameLauncher', ''))
                    return {'ok': True}
            fixture.server.RequestHandlerClass = Handler
            browser = pw.chromium.launch(headless=True)
            page = browser.new_page()
            page.set_default_timeout(5000)
            errors = []
            page.on('pageerror', lambda error: errors.append(str(error)))
            page.route('**/*', lambda route: route.continue_() if route.request.url.startswith(fixture.url+'/') else route.abort())
            try:
                page.goto(fixture.url + '/')
                steam_choice = page.get_by_label('Play the Steam version', exact=True)
                official_choice = page.get_by_label('Open the official launcher for me', exact=True)
                expect(steam_choice).to_be_checked()
                self.assertEqual(calls, [])
                with page.expect_response(lambda r: r.url.endswith('/api/status')):
                    official_choice.check()
                expect(official_choice).to_be_checked()
                self.assertEqual(calls[-1], {'gameStart': 'launcher', 'gameLauncher': official})
                with page.expect_response(lambda r: r.url.endswith('/api/status')):
                    steam_choice.check()
                self.assertEqual(calls[-1], {'gameStart': 'steam', 'gameLauncher': steam})
                with page.expect_response(lambda r: r.url.endswith('/api/status')):
                    page.get_by_label('I will start the game myself', exact=True).check()
                self.assertEqual(calls[-1], {'gameStart': 'manual'})
                page.locator('#customPath summary').click()
                page.locator('#launcherPath').fill(steam)
                with page.expect_response(lambda r: r.url.endswith('/api/status')):
                    page.locator('#savePath').click()
                self.assertEqual(calls[-1], {'gameStart': 'steam', 'gameLauncher': steam})
                page.reload()
                expect(page.get_by_label('Play the Steam version', exact=True)).to_be_checked()
                self.assertEqual(errors, [])
            finally:
                browser.close()
                fixture.close()


if __name__ == '__main__':
    unittest.main()
