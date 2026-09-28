"""Offline checks for registering a local candidate in the portable launcher catalog."""
import datetime
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('reg', Path(__file__).with_name('wuwa_register_candidate.py'))
reg = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reg)
CATALOG = Path(__file__).with_name('wuwa-builds.json')
NOW = datetime.datetime(2026, 9, 29, 9, 30, tzinfo=datetime.timezone(datetime.timedelta(hours=1)))


class Register(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.app = Path(self.tmp.name) / 'app'
        (self.app / 'dev').mkdir(parents=True)
        self.original = CATALOG.read_bytes()
        (self.app / 'dev' / 'wuwa-builds.json').write_bytes(self.original)
        for build in json.loads(self.original.decode('utf-8-sig'))['builds']:
            runtime = self.app / build['runtime']
            runtime.mkdir(parents=True)
            for name in reg.REQUIRED:
                (runtime / name).write_bytes(f'{build["id"]}:{name}'.encode())
            (self.app / build['seed']).mkdir(parents=True)
            (self.app / build['seed'] / 'config.txt').write_text('r.OneFrameThreadLag=0\n')
        self.backend = Path(self.tmp.name) / 'UEVRBackend.dll'
        self.backend.write_bytes(b'locally built backend')

    def tearDown(self):
        self.tmp.cleanup()

    def catalog(self):
        return json.loads((self.app / 'dev' / 'wuwa-builds.json').read_bytes().decode('utf-8-sig'))

    def test_appends_one_candidate_and_leaves_rollback_builds_untouched(self):
        before = json.loads(self.original.decode('utf-8-sig'))['builds']
        result = reg.register(self.app, self.backend, now=NOW)
        builds = self.catalog()['builds']
        self.assertEqual(builds[:-1], before)                       # baseline and older candidate unchanged
        entry = builds[-1]
        self.assertEqual((entry['id'], entry['role']), (reg.DEFAULT_ID, 'candidate'))
        self.assertEqual(entry['sha256'], reg.sha256(self.backend))
        runtime = self.app / entry['runtime']
        self.assertEqual((runtime / 'UEVRBackend.dll').read_bytes(), b'locally built backend')
        base = next(b for b in before if b['id'] == reg.DEFAULT_BASE)
        for name in reg.REQUIRED[1:]:                               # everything else is the base runtime's
            self.assertEqual((runtime / name).read_bytes(), (self.app / base['runtime'] / name).read_bytes())
        self.assertEqual((self.app / base['runtime'] / 'UEVRBackend.dll').read_bytes(),
                         f'{reg.DEFAULT_BASE}:UEVRBackend.dll'.encode())
        self.assertEqual((self.app / entry['seed'] / 'config.txt').read_text(), 'r.OneFrameThreadLag=0\n')
        self.assertTrue(result['changed'])
        self.assertIn('Not a foliage', entry['known'])

    def test_catalog_keeps_bom_and_non_ascii_names(self):
        reg.register(self.app, self.backend, now=NOW)
        raw = (self.app / 'dev' / 'wuwa-builds.json').read_bytes()
        self.assertTrue(raw.startswith(b'\xef\xbb\xbf'))
        self.assertIn('·', raw.decode('utf-8-sig'))

    def test_rerun_is_idempotent_and_conflicts_are_refused(self):
        reg.register(self.app, self.backend, now=NOW)
        snapshot = (self.app / 'dev' / 'wuwa-builds.json').read_bytes()
        self.assertFalse(reg.register(self.app, self.backend, now=NOW)['changed'])
        self.assertEqual((self.app / 'dev' / 'wuwa-builds.json').read_bytes(), snapshot)
        self.backend.write_bytes(b'a different build')
        with self.assertRaisesRegex(ValueError, 'already registered with another backend'):
            reg.register(self.app, self.backend, now=NOW)
        self.backend.write_bytes(b'locally built backend')
        with self.assertRaisesRegex(ValueError, 'already registered under another id'):
            reg.register(self.app, self.backend, build_id='second-id', now=NOW)

    def test_dry_run_and_bad_inputs_change_nothing(self):
        plan = reg.register(self.app, self.backend, dry_run=True, now=NOW)
        self.assertEqual(plan['entry']['sha256'], reg.sha256(self.backend))
        self.assertEqual((self.app / 'dev' / 'wuwa-builds.json').read_bytes(), self.original)
        self.assertFalse(Path(plan['runtime']).exists())
        with self.assertRaisesRegex(ValueError, 'not in the catalog'):
            reg.register(self.app, self.backend, base_id='missing', now=NOW)
        for bad in ('../escape', 'a/b', '.hidden', ''):
            with self.assertRaisesRegex(ValueError, 'invalid build id'):
                reg.register(self.app, self.backend, build_id=bad, now=NOW)
        (self.app / 'runtime' / f'Wuthering Waves UEVR - {reg.DEFAULT_ID}').mkdir()
        with self.assertRaisesRegex(ValueError, 'already exists'):
            reg.register(self.app, self.backend, now=NOW)
        self.assertEqual((self.app / 'dev' / 'wuwa-builds.json').read_bytes(), self.original)


if __name__ == '__main__':
    unittest.main()
