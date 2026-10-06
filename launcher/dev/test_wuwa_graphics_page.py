"""Real page/HTTP interaction with inert graphics responses, never a game."""
from datetime import datetime, timezone
import hashlib, json
from pathlib import Path
import tempfile, unittest
from urllib.request import Request, urlopen
from urllib.error import HTTPError
from playwright.sync_api import sync_playwright, expect
from test_wuwa_incident_page import Fixture, HERE

ARTIFACTS = HERE.parent / 'native/work/graphics-ui-20261006'


class GraphicsPageTests(unittest.TestCase):
    def test_read_only_clicks_unknown_values_errors_retry_and_auth(self):
        ARTIFACTS.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=ARTIFACTS) as tmp, sync_playwright() as pw:
            fixture = Fixture(Path(tmp))
            calls = []
            fixture.helper.run_build = lambda action: (calls.append(action) or json.dumps({
                'schema': 1, 'mode': 'custom', 'message': 'Custom overrides retained; saved data only'}))
            def read():
                calls.append('graphics_snapshot')
                return {'pid': 123, 'captured_at': '2026-10-06T10:00:00Z', 'limitations': 'No setter provenance',
                        'values': {'sg.ShadowQuality': {'available': True, 'float': 3},
                                   'r.ShadowQuality': {'available': True, 'float': 5},
                                   'r.ReflectionMethod': {'available': False, 'error': 'not found'}}}
            fixture.helper.graphics_snapshot = read
            original_handler = fixture.server.RequestHandlerClass
            class GraphicsHandler(original_handler):
                def post_action(self, path, body):
                    if path == '/api/graphics/read':
                        return fixture.helper.Handler.post_action(self, path, body)
                    return super().post_action(path, body)
            fixture.server.RequestHandlerClass = GraphicsHandler
            browser = pw.chromium.launch(headless=True)
            page = browser.new_page(viewport={'width': 1100, 'height': 950})
            errors = []; page.on('pageerror', lambda error: errors.append(str(error)))
            page.route('**/*', lambda route: route.continue_() if route.request.url.startswith(fixture.url+'/') else route.abort())
            try:
                page.goto(fixture.url+'/')
                self.assertEqual(calls, [])
                panel = page.locator('section[aria-labelledby="graphics-heading"]'); panel.scroll_into_view_if_needed()
                page.locator('#graphicsSaved').click()
                expect(page.locator('#graphicsPolicy')).to_contain_text('Custom overrides retained')
                self.assertEqual(calls, ['GraphicsStatus'])
                page.locator('#graphicsLive').click()
                expect(page.locator('#graphicsValues')).to_be_visible()
                expect(page.locator('#graphicsValues tbody tr')).to_have_count(3)
                expect(page.locator('#graphicsValues')).to_contain_text('Unavailable')
                self.assertEqual(calls, ['GraphicsStatus', 'graphics_snapshot'])
                def failure(): raise RuntimeError('Old backend <img src=x onerror=alert(1)>')
                fixture.helper.graphics_snapshot = failure
                page.locator('#graphicsLive').click()
                expect(page.locator('#graphicsResult')).to_contain_text('Old backend')
                expect(page.locator('#graphicsValues')).to_be_hidden()
                self.assertEqual(page.locator('#graphicsResult img').count(), 0)
                expect(page.locator('#graphicsLive')).to_be_enabled()
                fixture.helper.graphics_snapshot = read
                page.locator('#graphicsLive').click()
                expect(page.locator('#graphicsValues')).to_be_visible()
                for path, data, token, code in [('/api/graphics',None,None,403),
                                              ('/api/graphics/read',b'{}',None,403),
                                              ('/api/graphics/read',b'{"command":"set"}',fixture.helper.TOKEN,400)]:
                    headers = {'Content-Type':'application/json', 'Origin': fixture.url}
                    if token: headers['X-WuWa-Token'] = token
                    with self.assertRaises(HTTPError) as err: urlopen(Request(fixture.url+path,data=data,headers=headers))
                    self.assertEqual(err.exception.code, code)
                self.assertEqual(errors, [])
                panel.screenshot(path=str(ARTIFACTS/'graphics-panel.png'))
                (ARTIFACTS/'results.json').write_text(json.dumps({'passed': True, 'at_utc':datetime.now(timezone.utc).isoformat(),
                    'real':['page','HTTP handler'], 'fake':['game','graphics reader','saved audit'], 'game_launched':False,
                    'errors':errors, 'calls':calls, 'sha256':{name:hashlib.sha256((HERE/name).read_bytes()).hexdigest()
                    for name in ('wuwa-player.html','wuwa_player.py','wuwa_graphics.py')}},indent=2),encoding='utf-8')
            finally:
                browser.close(); fixture.close()


if __name__ == '__main__': unittest.main()
