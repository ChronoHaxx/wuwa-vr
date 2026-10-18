#!/usr/bin/env python3
"""Package a private test candidate in one command, from the newest native build.

    python dev/candidate.py NAME "TITLE" --notes NOTES.txt [--receipt RECEIPT.json] [--plan PLAN.json]
                            [--base "builds/<release>/portable/WuWa VR <version> beta"] [--skip-unit]
                            [--recorder builds/next-release-<version>/recorder/wuwa-recorder.exe]

NAME is the test name (17r). TITLE is what the launcher's version list shows, at most 100 characters,
for example "Test 17r: steadier desktop view (1.2.4, private)". NOTES is the player-facing TEST THIS.txt
(what changed, what to check, how to install); its first paragraph after the title line becomes the
package's scope.

Steps, each one refusing rather than guessing:
 1. No game running (never package while someone plays, above all in the headset).
 2. `qa.py unit`: every offline test passes (--skip-unit only for a packaging retry of the same commit).
 3. The build receipt (newest in %TEMP%/wuwa-build-logs unless --receipt) says built, exit 0, no stale
    embedded assets; its end time names everything: builds/YYYY-MM-DD-HHMM-test<NAME>, build
    steam-YYYYMMDD-HHMM, package candidate-YYYYMMDD-test<NAME>.
 4. The compiled tree matches the committed mod/uevr; mod/ and launcher/ have no uncommitted changes.
 5. backend-YYYYMMDD-HHMM gets the DLL and the PDB whose debug GUID matches it.
 6. The package is the newest public portable package (or --base) with this DLL, the branch's Lua and
    launcher dev scripts, and the uevr-mcp test plugin in the profile seed (private packages only).
 7. A Test Launcher with the package bundled is built, and an isolated install of the package is checked.
 8. With --plan, the steps card's plan is checked and installed (dev/steps-plan.py).
With --recorder (from dev/build-recorder.py --out), that SteamVR recorder replaces the base package's, after
its self-tests pass.

Never touches installed packages, the public catalog or feed, the game or OpenXR.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
WORKSPACE = REPO.parent
BUILDS = WORKSPACE / 'builds'
UPSTREAM = WORKSPACE / 'upstream/UEVR'
BIN = UPSTREAM / 'out/build/x64-RelWithDebInfo/bin/uevr'
MCP = WORKSPACE / 'upstream/uevr-mcp/plugin/build/Release/uevr_mcp.dll'
RECEIPTS = Path(os.environ.get('TEMP', '')) / 'wuwa-build-logs'


def sha(path: Path) -> str:
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def read(path: Path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def write(path: Path, value) -> None:
    Path(path).write_bytes((json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode('utf-8'))


def git(*args: str) -> str:
    return subprocess.check_output(['git', '-C', str(REPO), *args], text=True).strip()


def game_running() -> bool:
    out = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq Client-Win64-Shipping.exe', '/NH'],
                         capture_output=True, text=True).stdout
    return 'client-win64-shipping.exe' in out.lower()


def newest_base() -> Path:
    found = sorted(BUILDS.glob('*-release-*/portable/WuWa VR * beta'))
    if not found:
        sys.exit('no public portable package under builds/*-release-*/portable; pass --base')
    return found[-1]


def matching_pdb(dll: Path) -> Path:
    """The PDB named in the DLL's RSDS record, after checking it carries the same GUID."""
    image = dll.read_bytes()
    at = image.rfind(b'RSDS')
    guid, name = image[at + 4:at + 20], image[at + 24:image.index(b'\0', at + 24)].decode()
    pdb = dll.parent / Path(name).name
    if not pdb.exists() or guid not in pdb.read_bytes():
        sys.exit(f'{pdb.name} does not carry the DLL debug GUID; rebuild before packaging')
    return pdb


def run(folder: Path, name: str, cmd: list) -> None:
    print('==', name, flush=True)
    with (folder / f'{name}.log').open('w', encoding='utf-8') as log:
        result = subprocess.run([str(c) for c in cmd], stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        sys.exit(f'{name} failed ({result.returncode}); see {folder / (name + ".log")}')


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('name')
    p.add_argument('title')
    p.add_argument('--notes', type=Path, required=True)
    p.add_argument('--receipt', type=Path)
    p.add_argument('--base', type=Path)
    p.add_argument('--plan', type=Path)
    p.add_argument('--skip-unit', action='store_true')
    p.add_argument('--recorder', type=Path, help='a rebuilt wuwa-recorder.exe (dev/build-recorder.py --out)')
    a = p.parse_args()

    if len(a.title) > 100:
        sys.exit(f'title is {len(a.title)} characters; the launcher shows at most 100')
    notes = a.notes.read_text(encoding='utf-8').strip()
    paragraphs = [x.strip() for x in notes.split('\n\n') if x.strip()]
    scope = ' '.join((paragraphs[1] if len(paragraphs) > 1 else a.title).split())
    if game_running():
        sys.exit('the game is running: package after it closes (never while someone plays)')
    if a.plan:
        subprocess.run([sys.executable, str(REPO / 'dev/steps-plan.py'), 'check', str(a.plan)], check=True)
    if not a.skip_unit:
        print('== offline tests', flush=True)
        if subprocess.run([sys.executable, str(REPO / 'dev/qa.py'), 'unit']).returncode:
            sys.exit('offline tests fail: fix them (or the code) before packaging')

    receipt_path = a.receipt or max(RECEIPTS.glob('build-*.json'), key=lambda f: f.stat().st_mtime)
    receipt = read(receipt_path)
    if receipt.get('status') != 'built' or receipt.get('exit_code') != 0:
        sys.exit(f'{receipt_path.name} is not a finished build')
    if receipt.get('embedded_assets', {}).get('stale') != []:
        sys.exit(f'stale embedded assets in the build: {receipt.get("embedded_assets")}')
    ended = datetime.strptime(receipt['ended'], '%Y-%m-%dT%H:%M:%S%z')
    zone = {3600: 'BST', 0: 'GMT'}.get(int(ended.utcoffset().total_seconds()), ended.strftime('%z'))
    build = f'steam-{ended:%Y%m%d-%H%M}'
    tag = f'candidate-{ended:%Y%m%d}-test{a.name}'
    label = f'Steam test · {ended.day} {ended:%b %H:%M} {zone}'
    folder = BUILDS / f'{ended:%Y-%m-%d-%H%M}-test{a.name}'
    if folder.exists():
        sys.exit(f'{folder} exists: one folder per build; rebuild or remove it first')

    branch, commit = git('branch', '--show-current'), git('rev-parse', 'HEAD')
    if git('status', '--porcelain', '--', 'mod', 'launcher'):
        sys.exit('uncommitted changes under mod/ or launcher/: commit them so the package names its source')
    norm = lambda b: b if b'\0' in b else b.replace(b'\r\n', b'\n')
    source = REPO / 'mod/uevr'
    differ = [f.relative_to(source).as_posix() for f in sorted(source.rglob('*')) if f.is_file() and (
        not (UPSTREAM / f.relative_to(source)).exists() or norm(f.read_bytes()) != norm((UPSTREAM / f.relative_to(source)).read_bytes()))]
    if differ:
        sys.exit(f'the compiled tree differs from mod/uevr (build with dev/build-native-limited.py): {differ[:8]}')
    dll = BIN / 'UEVRBackend.dll'
    if sha(dll) != receipt['backend']['sha256']:
        sys.exit('the DLL in the build tree is not the one in the receipt (a newer or older build replaced it)')
    image = dll.read_bytes()
    if (source / 'src/assets/wuwa-languages.json').read_bytes() not in image:
        sys.exit('the DLL lacks the current wuwa-languages.json')
    pdb = matching_pdb(dll)
    if not MCP.exists():
        sys.exit(f'missing the test plugin {MCP}')
    if a.recorder:
        # Refuse before anything is written: the recorder must pass its own GPU and encoder self-tests.
        if a.recorder.name != 'wuwa-recorder.exe' or not a.recorder.is_file():
            sys.exit(f'--recorder must be a wuwa-recorder.exe: {a.recorder}')
        test = subprocess.run([str(a.recorder), '--self-test-showcase'], capture_output=True, text=True)
        if test.returncode:
            sys.exit(f'the recorder failed its showcase self-test: {test.stderr.strip() or test.stdout.strip()}')
    base = a.base or newest_base()
    manifest = read(base / 'manifest.json')
    actual = {f.relative_to(base).as_posix() for f in base.rglob('*') if f.is_file()}
    if actual != set(manifest['files']) | {'manifest.json', 'SHA256SUMS.txt'}:
        sys.exit(f'{base} does not match its manifest')
    for name, digest in manifest['files'].items():
        if sha(base / name) != digest:
            sys.exit(f'{base / name} does not match its manifest')
    old = read(base / 'app/dev/wuwa-builds.json')['builds'][0]
    print(f'{tag}: {build} from {branch} {commit[:8]} on {base.name}', flush=True)

    folder.mkdir(parents=True)
    backend = folder / f'backend-{ended:%Y%m%d-%H%M}'
    backend.mkdir()
    shutil.copy2(dll, backend / 'UEVRBackend.dll')
    shutil.copy2(pdb, backend / 'UEVRBackend.pdb')
    shutil.copy2(receipt_path, folder / 'build-receipt.json')
    (folder / 'TEST THIS.txt').write_bytes((notes + '\n').encode('utf-8'))

    out = folder / 'package-source'
    shutil.copytree(base, out, ignore=lambda d, names: set(names) & (
        {'runtime', 'builds'} if Path(d).relative_to(base).as_posix() == 'app' else {'__pycache__'}))
    runtime, seed = f'runtime/Wuthering Waves UEVR - {build}', f'builds/{build}/profile'
    shutil.copytree(base / 'app' / old['runtime'], out / 'app' / runtime)
    shutil.copy2(backend / 'UEVRBackend.dll', out / 'app' / runtime / 'UEVRBackend.dll')
    shutil.copytree(base / 'app' / old['seed'], out / 'app' / seed)
    # Private packages only: the uevr-mcp test plugin rides in the profile seed, because an install rebuilds
    # the profile's plugins folder and a copy added afterwards costs a game restart. Never public.
    shutil.copy2(MCP, out / 'app' / seed / 'plugins' / MCP.name)
    for script in sorted((out / 'app' / seed / 'scripts').glob('*.lua')):
        branch_copy = REPO / 'mod/lua' / script.name
        if branch_copy.exists() and sha(branch_copy) != sha(script):
            shutil.copy2(branch_copy, script)
            print('script from branch:', script.name, flush=True)
    # A script the branch added (test 21: wuwa_cutscene_bars.lua) is not in the base seed yet; the seed is
    # every mod/lua script except the HUD probe, as the release staging requires.
    for branch_copy in sorted((REPO / 'mod/lua').glob('*.lua')):
        script = out / 'app' / seed / 'scripts' / branch_copy.name
        if branch_copy.name != 'zz_WuWaHudProbe.lua' and not script.exists():
            shutil.copy2(branch_copy, script)
            print('new script from branch:', script.name, flush=True)
    for script in sorted((out / 'app/dev').glob('*.py')):
        branch_copy = REPO / 'launcher/dev' / script.name
        if branch_copy.exists() and sha(branch_copy) != sha(script):
            shutil.copy2(branch_copy, script)
            print('launcher script from branch:', script.name, flush=True)
    if a.recorder:
        shutil.copy2(a.recorder, out / 'app/dev-tools/wuwa-recorder.exe')
        print('recorder from', a.recorder, flush=True)
    entry = dict(old, id=build, name=label, runtime=runtime, seed=seed, sha256=sha(backend / 'UEVRBackend.dll'),
                 savedAt=datetime.now(timezone.utc).isoformat(), description=a.title, summary=a.title, known=scope,
                 settingsFrom=[{k: old[k] for k in ('id', 'sha256', 'graphicsPolicy') if k in old}] + old.get('settingsFrom', []))
    write(out / 'app/dev/wuwa-builds.json', {'schema': 1, 'builds': [entry]})
    write(out / 'app/builds' / build / 'source-identity.json', dict(build=build, backendSha256=entry['sha256'],
          sourceCommit=commit, branch=branch, buildCompleted=receipt['ended'], scope=scope))
    portable = read(out / 'app/portable.json')
    portable['private'] = True
    write(out / 'app/portable.json', portable)
    manifest.update(private=True, sourceCommit=commit, scope=scope)
    write(out / 'manifest.json', manifest)
    for name in ('START HERE.txt', 'PLAYTEST.txt'):
        shutil.copy2(folder / 'TEST THIS.txt', out / name)
    run(folder, 'package', [sys.executable, REPO / 'dev/package-beta.py', '--source', out, '--build', build,
        '--out', folder / 'portable', '--tag', tag, '--folder-name', f'WuWa VR test {a.name}', '--notes', folder / 'TEST THIS.txt'])
    archive = folder / 'portable/WuWa-VR-Launcher.zip'
    package = dict(id=tag, displayName=a.title, gameVersion=old.get('gameVersion', '3.7'), buildId=build,
                   channel='candidate', created=datetime.now(timezone.utc).isoformat(), sha256=sha(archive),
                   size=archive.stat().st_size)

    native = folder / 'native'
    shutil.copytree(REPO / 'launcher/native', native,
                    ignore=lambda d, names: set(names) & {'bin', 'obj', 'work', '__pycache__', 'tests', 'packages'})
    catalog = read(REPO / 'launcher/native/catalog.json')
    catalog['releases'].insert(0, package)
    write(native / 'catalog.json', catalog)
    ps = ['powershell.exe', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File']
    run(folder, 'build-test-launcher', ps + [native / 'build.ps1'])
    binary = native / 'bin/Release/WuWa VR.exe'
    run(folder, 'verify-candidate-install', ps + [native / 'verify-package-install.ps1', '-Launcher', binary,
        '-Archive', archive, '-TestRoot', folder / 'isolated-install', '-Catalog', native / 'catalog.json'])

    delivery = folder / 'Test Launcher'
    delivery.mkdir()
    for f in binary.parent.iterdir():
        if f.is_file() and f.suffix.lower() in ('.exe', '.dll', '.config'):
            shutil.copy2(f, delivery / f.name)
    (delivery / 'packages' / tag).mkdir(parents=True)
    shutil.copy2(archive, delivery / 'packages' / tag / archive.name)
    for name in ('LICENSE.txt', 'THIRD-PARTY-NOTICES.txt', 'PlayerGuide.html'):
        shutil.copy2(native / name, delivery / name)
    shutil.copy2(folder / 'TEST THIS.txt', delivery / 'TEST THIS.txt')
    files = {f.relative_to(delivery).as_posix(): sha(f) for f in sorted(delivery.rglob('*')) if f.is_file()}
    (delivery / 'SHA256SUMS.txt').write_bytes(''.join(f'{v}  {k}\n' for k, v in files.items()).encode('utf-8'))
    ready = dict(preparedUtc=datetime.now(timezone.utc).isoformat(), executable=str(delivery / binary.name),
                 package=package, sourceCommit=commit, branch=branch, base=str(base), files=files,
                 gameTested=False, productionInstall=False, publicFeedChanged=False)
    write(folder / 'candidate-ready.json', ready)
    if a.plan:
        subprocess.run([sys.executable, str(REPO / 'dev/steps-plan.py'), 'install', str(a.plan)], check=True)
    print(json.dumps({k: v for k, v in ready.items() if k != 'files'}, indent=2))


if __name__ == '__main__':
    main()
