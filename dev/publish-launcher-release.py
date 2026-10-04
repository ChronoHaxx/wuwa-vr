"""Prepare launcher/source assets and an optional mod release; publish only with --publish.

Run from a clean, reviewed release checkout after building and testing. This tool
never commits, pushes, launches a game, installs software or changes the website.
Publish release assets first, then push the reviewed site/feed commit to main.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import zipfile

REPO = 'ChronoHaxx/wuwa-vr'
APP_ID = 'ChronoHaxx.WuWaVR'
CHANNEL = 'win-beta'


def run(*args):
    result = subprocess.run(args, capture_output=True, text=True, encoding='utf-8')
    if result.returncode:
        raise RuntimeError(result.stderr[-2500:] or result.stdout[-2500:])
    return result.stdout.strip()


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def require(value, message):
    if not value:
        raise ValueError(message)


def safe_name(name):
    return isinstance(name, str) and name not in ('', '.', '..') and not re.search(r'[/\\:\x00-\x1f]', name)


def verify_file(path, record, label):
    require(path.is_file() and not path.is_symlink(), 'Missing or linked ' + label)
    require(path.stat().st_size == record['bytes'] and digest(path) == record['sha256'].lower(),
            'Packaging receipt identity mismatch: ' + label)


def verify_pack_receipt(checkout, packed, assets):
    """Bind Setup/update assets and copied source inputs to one successful pack."""
    receipt = read(packed / 'pack-receipt.json')
    require(receipt['status'] == 'packed', 'Packaging receipt is not successful')
    require(receipt['appId'] == APP_ID and receipt['channel'] == CHANNEL and
            receipt['launcherVersion'] == assets[0]['Version'], 'Packaging receipt app/version/channel mismatch')
    records = receipt['artifacts']
    require(records and all(safe_name(x['name']) for x in records), 'Unsafe receipt artifact name')
    recorded = {x['name']: x for x in records}
    require(len(recorded) == len(records), 'Duplicate receipt artifact name')
    required = {'WuWa-VR-Setup.exe', 'releases.' + CHANNEL + '.json'} | {x['FileName'] for x in assets}
    require(required.issubset(recorded), 'Required update assets are missing from the packaging receipt')
    canonical = [name for name in recorded if name.startswith(APP_ID + '-') and name.endswith('-Setup.exe')]
    require(len(canonical) == 1, 'Receipt must identify one canonical generated Setup')
    for name, record in recorded.items():
        verify_file(packed / 'releases' / name, record, name)
    require(recorded['WuWa-VR-Setup.exe']['sha256'].lower() == recorded[canonical[0]]['sha256'].lower(),
            'Public Setup alias differs from the canonical generated Setup')
    require(receipt.get('downloadAlias') == 'WuWa-VR-Setup.exe', 'Unexpected installer alias')

    # Compiled binaries are normally ignored by Git. Verify their exact staged
    # inputs, and bind every copied guide/licence plus the icon/packer to the
    # release checkout. This does not claim a reproducible source-to-binary build.
    docs = {'README.txt', 'TEST THIS.txt', 'PlayerGuide.html', 'LICENSE.txt', 'THIRD-PARTY-NOTICES.txt'}
    binaries = {'WuWa VR.exe', 'Velopack.dll', 'Newtonsoft.Json.dll'}
    inputs = receipt['inputs']
    require(all(safe_name(x['name']) for x in inputs), 'Unsafe receipt input name')
    names = {x['name'] for x in inputs}
    require(len(names) == len(inputs) and (docs | binaries) <= names and
            names <= (docs | binaries | {'WuWa VR.exe.config'}), 'Unexpected or missing staged launcher inputs')
    for record in inputs:
        verify_file(packed / 'app-stage' / record['name'], record, 'staged ' + record['name'])
        if record['name'] in docs:
            verify_file(checkout / 'launcher/native' / record['name'], record, 'checkout ' + record['name'])
    verify_file(checkout / 'launcher/native/assets/wuwa-vr.ico', receipt['icon'], 'checkout icon')
    require(digest(checkout / 'launcher/native/pack-installer.ps1') == receipt['scriptSha256'].lower(),
            'Checkout pack-installer.ps1 differs from the packaging receipt')
    return receipt


def remote_tag_commit(tag):
    """Resolve lightweight and annotated tags without accepting a branch name."""
    ref = 'refs/tags/' + tag
    output = run('git', 'ls-remote', '--tags', 'https://github.com/' + REPO + '.git', ref, ref + '^{}')
    refs = {}
    for line in output.splitlines():
        fields = line.split()
        require(len(fields) == 2 and re.fullmatch(r'[0-9a-fA-F]{40}', fields[0]) and
                fields[1] in (ref, ref + '^{}'), 'Unexpected remote tag response')
        require(fields[1] not in refs, 'Ambiguous remote tag response')
        refs[fields[1]] = fields[0].lower()
    return refs.get(ref + '^{}', refs.get(ref))


def verify_release_target(remote, tag, commit):
    require(remote['tag_name'] == tag and remote['draft'], 'Refusing to modify a different or published release')
    actual = remote_tag_commit(tag)
    if actual is None:
        # GitHub can retain a draft's intended target before materializing its tag.
        require(remote.get('target_commitish') == commit, 'Untagged draft does not target the reviewed source commit')
    else:
        require(actual == commit, 'Release tag resolves to a different source commit')


def select_backend(checkout, tag, portable=None):
    """Read existing backend context; only an explicit portable adds a mod asset."""
    catalog = read(checkout / 'launcher/native/catalog.public.json')
    releases = catalog['releases']
    require(releases, 'Public backend catalog is empty')
    mod_files = {}
    if portable is None:
        require(all(x['id'] != tag for x in releases),
                'Launcher-only publication requires a distinct app tag, not a backend catalog entry')
        release = releases[0]
    else:
        matches = [x for x in releases if x['id'] == tag]
        require(len(matches) == 1, 'Paired publication requires exactly one matching backend catalog entry')
        release = matches[0]
        portable = portable.resolve(strict=True)
        require(release['sha256'] == digest(portable) and release['size'] == portable.stat().st_size,
                'Mod catalog/archive mismatch')
        with zipfile.ZipFile(portable) as archive:
            manifests = [x for x in archive.namelist() if x.count('/') == 1 and x.endswith('/manifest.json')]
            require(len(manifests) == 1, 'Ambiguous portable manifest')
            prefix = manifests[0][:-len('manifest.json')]
            manifest = json.loads(archive.read(manifests[0]))
            require(manifest['packageId'] == 'wuwa-vr-launcher-' + tag and
                    manifest['defaultBuild'] == release['buildId'], 'Portable release identity mismatch')
            for name, expected in manifest['files'].items():
                require(hashlib.sha256(archive.read(prefix + name)).hexdigest() == expected,
                        'Portable file differs: ' + name)
        mod_files['WuWa-VR-Launcher.zip'] = portable
    checkpoint = read(checkout / 'mod/checkpoint.json')
    require(release.get('buildId') and checkpoint.get('build') == release['buildId'],
            'Source checkpoint backend differs from the selected public catalog release')
    return release, mod_files


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--checkout', type=Path, required=True)
    p.add_argument('--packed', type=Path, required=True, help='pack-installer.ps1 output directory')
    p.add_argument('--portable', type=Path, help='Optional new backend ZIP; omit for a launcher-only release')
    p.add_argument('--notes', type=Path, required=True)
    p.add_argument('--tag', required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--publish', action='store_true')
    a = p.parse_args()
    require(re.fullmatch(r'beta-[A-Za-z0-9_-]{1,90}', a.tag), 'Use a new beta- tag')
    checkout = a.checkout.resolve(strict=True)
    git = ['git', '-c', 'safe.directory=' + checkout.as_posix(), '-C', str(checkout)]
    require(not run(*git, 'status', '--porcelain'), 'Commit/review the source and site/feed first')
    commit = run(*git, 'rev-parse', 'HEAD')
    output = a.out.resolve()
    require(not output.is_relative_to(checkout), 'Keep release artifacts outside the source checkout')
    output.mkdir(parents=True, exist_ok=True)
    packed = a.packed.resolve(strict=True)
    releases = packed / 'releases'
    setup = releases / 'WuWa-VR-Setup.exe'
    require(setup.is_file() and setup.read_bytes()[:2] == b'MZ', 'Missing Windows Setup EXE')
    source_feed = read(releases / 'releases.win-beta.json')
    assets = source_feed['Assets']
    require(assets and all(x['PackageId'] == APP_ID for x in assets), 'Wrong application feed')
    require(len({x['Version'] for x in assets}) == 1, 'Use one isolated app-version output')
    require(sum(x['Type'] == 'Full' for x in assets) == 1 and
            all(x['Type'] in ('Full', 'Delta') for x in assets), 'Expected one full package and optional deltas')
    require(all(safe_name(x['FileName']) and x['FileName'].endswith('.nupkg') for x in assets), 'Unsafe package filename')
    verify_pack_receipt(checkout, packed, assets)
    files = {'WuWa-VR-Setup.exe': setup,
             'START-HERE.txt': a.notes.resolve(strict=True),
             'releases.win-beta.json': releases / 'releases.win-beta.json'}
    for item in assets:
        name = item['FileName']
        require(Path(name).name == name and '/' not in name and '\\' not in name and name.endswith('.nupkg'), 'Unsafe package filename')
        file = releases / name
        require(file.stat().st_size == item['Size'] and digest(file) == item['SHA256'].lower(), 'Update package identity mismatch')
        files[name] = file
    release, mod_files = select_backend(checkout, a.tag, a.portable)
    files.update(mod_files)
    site_feed = read(checkout / 'site/launcher-updates/releases.win-beta.json')
    wanted = json.loads(json.dumps(source_feed))
    for item in wanted['Assets']:
        item['FileName'] = 'https://github.com/' + REPO + '/releases/download/' + a.tag + '/' + item['FileName']
    require(site_feed == wanted, 'Website feed differs from the verified update packages')
    for name, source in files.items():
        target = output / name
        if target.exists():
            require(digest(source) == digest(target), 'Refusing to replace different staged asset: ' + name)
        else:
            shutil.copy2(source, target)
    source_zip = output / 'WuWa-VR-Source.zip'
    archive_tmp = output / 'source.pending.zip'
    require(not archive_tmp.exists(), 'Resolve previous source.pending.zip first')
    run(*git, 'archive', '--format=zip', '--output=' + str(archive_tmp), commit)
    if source_zip.exists():
        require(digest(source_zip) == digest(archive_tmp), 'Existing source archive belongs to a different commit')
        archive_tmp.unlink()
    else:
        archive_tmp.rename(source_zip)
    files['WuWa-VR-Source.zip'] = source_zip
    records = [{'name': name, 'size': (output / name).stat().st_size, 'sha256': digest(output / name)} for name in sorted(files)]
    (output / 'SHA256SUMS.txt').write_text(''.join(x['sha256'] + '  ' + x['name'] + '\n' for x in records), encoding='utf-8')
    sums = output / 'SHA256SUMS.txt'
    records.append({'name': sums.name, 'size': sums.stat().st_size, 'sha256': digest(sums)})
    plan = {'tag': a.tag, 'source_commit': commit, 'launcher_version': assets[0]['Version'],
            'release_mode': 'paired' if a.portable is not None else 'launcher-only',
            'backend_release': release['id'], 'backend_build': release['buildId'],
            'assets': records, 'prerelease': True}
    (output / 'release-plan.json').write_text(json.dumps(plan, indent=2) + '\n', encoding='utf-8')
    if not a.publish:
        print(json.dumps(plan, indent=2))
        return
    # The target must already be pushed. Never publish against an unrelated SHA.
    require(json.loads(run('gh', 'api', 'repos/' + REPO + '/commits/' + commit))['sha'] == commit, 'Push the reviewed release branch first')
    tag_commit = remote_tag_commit(a.tag)
    require(tag_commit is None or tag_commit == commit, 'Existing release tag resolves to a different source commit')
    existing = json.loads(run('gh', 'api', 'repos/' + REPO + '/releases?per_page=100'))
    remote = next((x for x in existing if x['tag_name'] == a.tag), None)
    if remote is None:
        run('gh', 'release', 'create', a.tag, '--repo', REPO, '--target', commit, '--draft', '--prerelease', '--title', 'WuWa VR beta - installer ' + assets[0]['Version'], '--notes-file', str(a.notes))
        created = json.loads(run('gh', 'api', 'repos/' + REPO + '/releases?per_page=100'))
        remote = next(x for x in created if x['tag_name'] == a.tag)
    verify_release_target(remote, a.tag, commit)
    uploaded = {x['name']: x for x in remote['assets']}
    require(set(uploaded).issubset(x['name'] for x in records), 'Unexpected draft assets')
    for item in records:
        if item['name'] in uploaded:
            require(uploaded[item['name']].get('digest') == 'sha256:' + item['sha256'], 'Different asset already exists in draft')
        else:
            print('Uploading ' + item['name'], flush=True)
            run('gh', 'release', 'upload', a.tag, str(output / item['name']), '--repo', REPO)
    remote = json.loads(run('gh', 'api', 'repos/' + REPO + '/releases/' + str(remote['id'])))
    verify_release_target(remote, a.tag, commit)
    actual = {x['name']: x for x in remote['assets']}
    require(set(actual) == {x['name'] for x in records}, 'Release asset inventory mismatch')
    for item in records:
        require(actual[item['name']]['size'] == item['size'] and actual[item['name']].get('digest') == 'sha256:' + item['sha256'], 'Remote asset mismatch: ' + item['name'])
    run('gh', 'release', 'edit', a.tag, '--repo', REPO, '--draft=false', '--prerelease', '--latest=false')
    require(remote_tag_commit(a.tag) == commit, 'Published tag does not resolve to the reviewed source commit')
    plan['published_release'] = json.loads(run('gh', 'api', 'repos/' + REPO + '/releases/' + str(remote['id'])))['html_url']
    (output / 'published.json').write_text(json.dumps(plan, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(plan, indent=2))


if __name__ == '__main__':
    main()
