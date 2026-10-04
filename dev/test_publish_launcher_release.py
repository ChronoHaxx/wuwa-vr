"""Offline fixture checks: no subprocesses, network, package build or app launch."""
from pathlib import Path
import copy
import importlib.util
import io
import json
import sys
import tempfile
import unittest
import zipfile
from contextlib import redirect_stdout
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
SOURCE = HERE / 'publish-launcher-release.py'
spec = importlib.util.spec_from_file_location('publish_release', SOURCE)
pub = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pub)


def write(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(content)
    return {'name': path.name, 'bytes': len(content), 'sha256': pub.digest(path)}


class ReceiptTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='fixture-', dir=HERE)
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.checkout = self.base / 'checkout'
        self.packed = self.base / 'packed'
        native = self.checkout / 'launcher/native'
        inputs = []
        docs = ['README.txt', 'TEST THIS.txt', 'PlayerGuide.html', 'LICENSE.txt', 'THIRD-PARTY-NOTICES.txt']
        binaries = ['WuWa VR.exe', 'Velopack.dll', 'Newtonsoft.Json.dll']
        for name in docs + binaries:
            data = ('fixture:' + name).encode()
            inputs.append(write(self.packed / 'app-stage' / name, data))
            if name in docs:
                write(native / name, data)
        icon = write(native / 'assets/wuwa-vr.ico', b'fixture icon')
        packer = write(native / 'pack-installer.ps1', b'fixture packer')
        pkg = pub.APP_ID + '-1.0.0-full.nupkg'
        self.assets = [{'FileName': pkg, 'Version': '1.0.0', 'PackageId': pub.APP_ID,
                        'Type': 'Full', 'Size': 15, 'SHA256': ''}]
        artifacts = [write(self.packed / 'releases' / name, data) for name, data in [
            ('WuWa-VR-Setup.exe', b'MZfixture setup'),
            (pub.APP_ID + '-win-beta-Setup.exe', b'MZfixture setup'),
            (pkg, b'fixture package'),
            ('releases.win-beta.json', json.dumps({'Assets': self.assets}).encode())]]
        self.receipt = {'status': 'packed', 'appId': pub.APP_ID, 'channel': pub.CHANNEL,
                        'launcherVersion': '1.0.0', 'artifacts': artifacts,
                        'downloadAlias': 'WuWa-VR-Setup.exe', 'inputs': inputs,
                        'icon': icon, 'scriptSha256': packer['sha256']}
        self.save()

    def save(self):
        (self.packed / 'pack-receipt.json').write_text(json.dumps(self.receipt), encoding='utf-8')

    def check(self):
        return pub.verify_pack_receipt(self.checkout, self.packed, self.assets)

    def test_exact_pack_and_checkout_pass(self):
        self.assertEqual(self.check()['status'], 'packed')

    def test_optional_config_passes(self):
        self.receipt['inputs'].append(write(self.packed / 'app-stage/WuWa VR.exe.config', b'<configuration/>'))
        self.save()
        self.check()

    def test_wrong_mz_setup_rejected(self):
        write(self.packed / 'releases/WuWa-VR-Setup.exe', b'MZanother setup')
        with self.assertRaisesRegex(ValueError, 'identity mismatch'):
            self.check()

    def test_rehashed_alias_still_must_equal_canonical(self):
        record = write(self.packed / 'releases/WuWa-VR-Setup.exe', b'MZanother setup')
        self.receipt['artifacts'][0] = record
        self.save()
        with self.assertRaisesRegex(ValueError, 'alias differs'):
            self.check()

    def test_feed_and_package_checked_against_receipt(self):
        for name in [self.assets[0]['FileName'], 'releases.win-beta.json']:
            with self.subTest(name=name):
                path = self.packed / 'releases' / name
                original = path.read_bytes()
                path.write_bytes(b'tampered')
                with self.assertRaisesRegex(ValueError, 'identity mismatch'):
                    self.check()
                path.write_bytes(original)

    def test_receipt_identity_and_success_required(self):
        for key, value in [('status', 'failed'), ('appId', 'Other'), ('channel', 'stable'), ('launcherVersion', '9.0.0')]:
            with self.subTest(key=key):
                original = self.receipt[key]
                self.receipt[key] = value
                self.save()
                with self.assertRaises(ValueError):
                    self.check()
                self.receipt[key] = original

    def test_required_artifact_and_unique_names(self):
        original = copy.deepcopy(self.receipt['artifacts'])
        for records in [original[1:], original + [original[0]]]:
            self.receipt['artifacts'] = records
            self.save()
            with self.assertRaises(ValueError):
                self.check()

    def test_path_escape_rejected(self):
        for name in ['../outside', 'C:outside', 'one/two', 'one\\two', '.', '..']:
            with self.subTest(name=name):
                self.assertFalse(pub.safe_name(name))
        self.receipt['artifacts'][0]['name'] = '../outside'
        self.save()
        with self.assertRaisesRegex(ValueError, 'Unsafe receipt'):
            self.check()

    def test_staged_dll_and_checkout_docs_icon_packer_bound(self):
        paths = [self.packed / 'app-stage/Velopack.dll',
                 self.checkout / 'launcher/native/PlayerGuide.html',
                 self.checkout / 'launcher/native/assets/wuwa-vr.ico',
                 self.checkout / 'launcher/native/pack-installer.ps1']
        for path in paths:
            with self.subTest(name=path.name):
                original = path.read_bytes()
                path.write_bytes(b'changed')
                with self.assertRaises(ValueError):
                    self.check()
                path.write_bytes(original)

    def test_input_inventory_must_match(self):
        original = copy.deepcopy(self.receipt['inputs'])
        for inputs in [original[1:], original + [original[0]], original + [dict(original[0], name='secret.txt')]]:
            self.receipt['inputs'] = inputs
            self.save()
            with self.assertRaisesRegex(ValueError, 'staged launcher inputs'):
                self.check()


class TagTests(unittest.TestCase):
    tag = 'beta-fixture'
    commit = 'a' * 40
    other = 'b' * 40

    def resolve(self, stdout):
        with patch.object(pub, 'run', return_value=stdout) as mock:
            result = pub.remote_tag_commit(self.tag)
            self.assertEqual(mock.call_args.args, ('git', 'ls-remote', '--tags',
                'https://github.com/ChronoHaxx/wuwa-vr.git', 'refs/tags/' + self.tag,
                'refs/tags/' + self.tag + '^{}'))
            return result

    def test_lightweight_annotated_and_missing(self):
        ref = 'refs/tags/' + self.tag
        self.assertEqual(self.resolve(self.commit + '\t' + ref), self.commit)
        self.assertEqual(self.resolve(self.other + '\t' + ref + '\n' + self.commit + '\t' + ref + '^{}'), self.commit)
        self.assertIsNone(self.resolve(''))

    def test_wrong_or_ambiguous_response_rejected(self):
        ref = 'refs/tags/' + self.tag
        for response in ['not a ref', self.commit + '\trefs/heads/main',
                         self.commit + '\t' + ref + '\n' + self.other + '\t' + ref]:
            with self.subTest(response=response), self.assertRaises(ValueError):
                self.resolve(response)

    def verify(self, resolved, **overrides):
        remote = dict(tag_name=self.tag, draft=True, target_commitish=self.commit)
        remote.update(overrides)
        with patch.object(pub, 'remote_tag_commit', return_value=resolved):
            pub.verify_release_target(remote, self.tag, self.commit)

    def test_exact_tag_or_unmaterialized_explicit_commit(self):
        self.verify(self.commit)
        self.verify(None)

    def test_existing_other_commit_rejected(self):
        with self.assertRaisesRegex(ValueError, 'different source commit'):
            self.verify(self.other)

    def test_unmaterialized_branch_or_other_commit_rejected(self):
        for target in ['main', self.other, None]:
            with self.subTest(target=target), self.assertRaisesRegex(ValueError, 'reviewed source commit'):
                self.verify(None, target_commitish=target)

    def test_published_or_different_release_rejected(self):
        for overrides in [dict(draft=False), dict(tag_name='beta-other')]:
            with self.subTest(overrides=overrides), self.assertRaisesRegex(ValueError, 'different or published'):
                self.verify(self.commit, **overrides)


class CreateDraftTests(unittest.TestCase):
    def test_post_json_returns_exact_draft_without_listing(self):
        with tempfile.TemporaryDirectory(prefix='draft-', dir=HERE) as directory:
            notes = Path(directory) / 'notes.txt'
            body = 'Launcher update\n\n中文 notes and literal `$(text)`\n'
            notes.write_text(body, encoding='utf-8')
            remote = {'id': 43210, 'tag_name': 'beta-fixture', 'target_commitish': 'a' * 40,
                      'draft': True, 'prerelease': True, 'assets': []}
            response = pub.subprocess.CompletedProcess([], 0, json.dumps(remote), '')
            with patch.object(pub.subprocess, 'run', return_value=response) as process:
                result = pub.create_draft('beta-fixture', 'a' * 40, '1.0.1', notes)
            self.assertEqual(result, remote)
            process.assert_called_once()
            self.assertEqual(process.call_args.args, (('gh', 'api', '--method', 'POST',
                'repos/ChronoHaxx/wuwa-vr/releases', '--input', '-'),))
            payload = json.loads(process.call_args.kwargs['input'])
            self.assertEqual(payload, {'tag_name': 'beta-fixture', 'target_commitish': 'a' * 40,
                'name': 'WuWa VR beta - installer 1.0.1', 'body': body, 'draft': True, 'prerelease': True})
            self.assertTrue(process.call_args.kwargs['text'])
            self.assertEqual(process.call_args.kwargs['encoding'], 'utf-8')
            # The existing target guard still validates the returned object.
            with patch.object(pub, 'remote_tag_commit', return_value=None):
                pub.verify_release_target(result, 'beta-fixture', 'a' * 40)
                result['target_commitish'] = 'b' * 40
                with self.assertRaisesRegex(ValueError, 'reviewed source commit'):
                    pub.verify_release_target(result, 'beta-fixture', 'a' * 40)

    def test_missing_invalid_or_boolean_release_id_rejected(self):
        notes = unittest.mock.Mock()
        notes.read_text.return_value = 'Fixture notes'
        for remote in [{}, {'id': 0}, {'id': '43210'}, {'id': True}]:
            with self.subTest(remote=remote), patch.object(pub, 'run', return_value=json.dumps(remote)):
                with self.assertRaisesRegex(ValueError, 'valid release ID'):
                    pub.create_draft('beta-fixture', 'a' * 40, '1.0.1', notes)


class BackendTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='backend-', dir=HERE)
        self.addCleanup(self.temp.cleanup)
        self.checkout = Path(self.temp.name) / 'checkout'
        self.catalog_path = self.checkout / 'launcher/native/catalog.public.json'
        self.checkpoint_path = self.checkout / 'mod/checkpoint.json'
        self.release = {'id': 'beta-old-mod', 'buildId': 'retained-backend',
                        'url': 'https://example.invalid/existing-only.zip'}
        self.catalog = {'releases': [self.release, {'id': 'beta-older-mod', 'buildId': 'older'}]}
        self.save()

    def save(self, build='retained-backend'):
        write(self.catalog_path, json.dumps(self.catalog).encode())
        write(self.checkpoint_path, json.dumps({'build': build}).encode())

    def portable(self, **manifest_changes):
        path = Path(self.temp.name) / 'mod.zip'
        payload = b'fixture backend'
        manifest = {'packageId': 'wuwa-vr-launcher-beta-old-mod', 'defaultBuild': 'retained-backend',
                    'files': {'backend.dll': pub.hashlib.sha256(payload).hexdigest()}}
        manifest.update(manifest_changes)
        with zipfile.ZipFile(path, 'w') as archive:
            archive.writestr('Portable/manifest.json', json.dumps(manifest))
            archive.writestr('Portable/backend.dll', payload)
        self.release.update(sha256=pub.digest(path), size=path.stat().st_size)
        self.save()
        return path

    def test_launcher_only_uses_existing_first_entry_without_archive_or_mutation(self):
        before = self.catalog_path.read_bytes(), self.checkpoint_path.read_bytes()
        release, files = pub.select_backend(self.checkout, 'beta-new-app')
        self.assertEqual(release, self.release)
        self.assertEqual(files, {})
        self.assertEqual(before, (self.catalog_path.read_bytes(), self.checkpoint_path.read_bytes()))

    def test_launcher_only_requires_distinct_app_tag(self):
        for tag in ['beta-old-mod', 'beta-older-mod']:
            with self.subTest(tag=tag), self.assertRaisesRegex(ValueError, 'distinct app tag'):
                pub.select_backend(self.checkout, tag)

    def test_launcher_only_rejects_unrelated_source_backend(self):
        self.save(build='unreleased-change')
        with self.assertRaisesRegex(ValueError, 'checkpoint backend differs'):
            pub.select_backend(self.checkout, 'beta-new-app')

    def test_paired_preserves_verification_and_adds_only_explicit_archive(self):
        path = self.portable()
        before = self.catalog_path.read_bytes()
        release, files = pub.select_backend(self.checkout, 'beta-old-mod', path)
        self.assertEqual(release['buildId'], 'retained-backend')
        self.assertEqual(files, {'WuWa-VR-Launcher.zip': path.resolve()})
        self.assertEqual(before, self.catalog_path.read_bytes())

    def test_paired_rejects_wrong_hash_or_missing_catalog_entry(self):
        path = self.portable()
        with self.assertRaisesRegex(ValueError, 'matching backend catalog entry'):
            pub.select_backend(self.checkout, 'beta-new-app', path)
        path.write_bytes(b'tampered zip')
        with self.assertRaisesRegex(ValueError, 'catalog/archive mismatch'):
            pub.select_backend(self.checkout, 'beta-old-mod', path)

    def test_paired_rejects_manifest_identity_and_payload_mismatch(self):
        for change in [dict(packageId='wrong'), dict(defaultBuild='wrong'), dict(files={'backend.dll': '0' * 64})]:
            with self.subTest(change=change):
                path = self.portable(**change)
                with self.assertRaises(ValueError):
                    pub.select_backend(self.checkout, 'beta-old-mod', path)

    def test_empty_catalog_rejected(self):
        self.catalog['releases'] = []
        self.save()
        with self.assertRaisesRegex(ValueError, 'catalog is empty'):
            pub.select_backend(self.checkout, 'beta-new-app')


class LauncherOnlyCliTests(unittest.TestCase):
    def test_dry_run_omits_mod_zip_and_preserves_catalog(self):
        fixture = ReceiptTests()
        fixture.setUp()
        self.addCleanup(fixture.doCleanups)
        assets = fixture.assets
        package = fixture.packed / 'releases' / assets[0]['FileName']
        assets[0]['Size'] = package.stat().st_size
        assets[0]['SHA256'] = pub.digest(package)
        source_feed = {'Assets': assets}
        updated = write(fixture.packed / 'releases/releases.win-beta.json', json.dumps(source_feed).encode())
        fixture.receipt['artifacts'][-1] = updated
        fixture.save()
        tag = 'beta-new-app'
        site_feed = copy.deepcopy(source_feed)
        site_feed['Assets'][0]['FileName'] = 'https://github.com/' + pub.REPO + '/releases/download/' + tag + '/' + assets[0]['FileName']
        write(fixture.checkout / 'site/launcher-updates/releases.win-beta.json', json.dumps(site_feed).encode())
        catalog_path = fixture.checkout / 'launcher/native/catalog.public.json'
        catalog_bytes = b'{"releases":[{"id":"beta-existing-mod","buildId":"existing-backend"}]}'
        write(catalog_path, catalog_bytes)
        write(fixture.checkout / 'mod/checkpoint.json', b'{"build":"existing-backend"}')
        notes = fixture.base / 'notes.txt'
        write(notes, b'Launcher only')
        output = fixture.base / 'output'

        def fake_run(*args):
            self.assertEqual(args[0], 'git')
            if args[-2:] == ('status', '--porcelain'):
                return ''
            if args[-2:] == ('rev-parse', 'HEAD'):
                return 'a' * 40
            self.assertIn('archive', args)
            archive = Path(next(x.removeprefix('--output=') for x in args if x.startswith('--output=')))
            with zipfile.ZipFile(archive, 'w') as stream:
                stream.writestr('fixture-source.txt', b'Offline source archive fixture')
            return ''

        argv = [str(SOURCE), '--checkout', str(fixture.checkout), '--packed', str(fixture.packed),
                '--notes', str(notes), '--tag', tag, '--out', str(output)]
        with patch.object(sys, 'argv', argv), patch.object(pub, 'run', side_effect=fake_run), redirect_stdout(io.StringIO()):
            pub.main()
        plan = pub.read(output / 'release-plan.json')
        self.assertEqual(plan['release_mode'], 'launcher-only')
        self.assertEqual(plan['backend_release'], 'beta-existing-mod')
        self.assertEqual(plan['backend_build'], 'existing-backend')
        self.assertNotIn('WuWa-VR-Launcher.zip', {x['name'] for x in plan['assets']})
        self.assertFalse((output / 'WuWa-VR-Launcher.zip').exists())
        self.assertEqual(catalog_path.read_bytes(), catalog_bytes)


if __name__ == '__main__':
    # Any accidental subprocess (including network commands) is a fixture failure.
    with patch.object(pub.subprocess, 'run', side_effect=AssertionError('No subprocesses in fixtures')):
        unittest.main(verbosity=2)
