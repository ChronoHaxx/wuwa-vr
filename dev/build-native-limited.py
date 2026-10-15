"""Build one existing MSVC project with one compiler worker and memory headroom.

Does not configure CMake, install a DLL, stop the game or change global settings.
Refuses to start alongside another compiler/ffmpeg job. Run again after a failed
or memory-limited attempt to let MSBuild reuse completed objects.

Sources: mod/uevr is copied into the upstream tree first (the tree MSBuild
compiles; sync-native-patch.py only rewrites the patch file), and the build
refuses to start if any file still differs. MSBuild does not notice a changed
RCDATA asset, so the .rc is touched when an asset is newer than its .res, and
after linking the DLL must contain every asset's current bytes. On 15 Oct 2026
test 17 shipped without six changed files and with a stale language bundle.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time
import psutil

REPO = Path(__file__).resolve().parents[1]
SOURCE = REPO / 'mod/uevr'


def lf(data):
    return data.replace(b'\r\n', b'\n')


def mirror_sources(tree):
    """Copy mod/uevr into the upstream tree (keeping each target's line endings); return what changed."""
    copied = []
    for source in sorted(p for p in SOURCE.rglob('*') if p.is_file()):
        target = tree / source.relative_to(SOURCE)
        data = source.read_bytes()
        if target.exists():
            old = target.read_bytes()
            if data == old or (b'\0' not in data and lf(data) == lf(old)):
                continue
            if b'\0' not in data and old.count(b'\r\n') and old.count(b'\r\n') == old.count(b'\n'):
                data = lf(data).replace(b'\n', b'\r\n')
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        copied.append(source.relative_to(SOURCE).as_posix())
    return copied


def source_hash():
    """sha256 over mod/uevr's paths and line-ending-normalised contents: what this build compiled."""
    digest = hashlib.sha256()
    for source in sorted(p for p in SOURCE.rglob('*') if p.is_file()):
        data = source.read_bytes()
        digest.update(source.relative_to(SOURCE).as_posix().encode() + b'\0')
        digest.update((data if b'\0' in data else lf(data)) + b'\0')
    return digest.hexdigest()


def source_differences(tree):
    out = []
    for source in sorted(p for p in SOURCE.rglob('*') if p.is_file()):
        target = tree / source.relative_to(SOURCE)
        data = source.read_bytes()
        if not target.exists():
            out.append(source.relative_to(SOURCE).as_posix() + ' (missing)')
        elif data != target.read_bytes() and (b'\0' in data or lf(data) != lf(target.read_bytes())):
            out.append(source.relative_to(SOURCE).as_posix())
    return out


def embedded_assets(tree):
    """(rc file, asset path) for every RCDATA asset in the tree's resource scripts that mod/uevr carries."""
    assets = []
    for rc in sorted((SOURCE / 'src').rglob('*.rc')):
        rc_tree = tree / rc.relative_to(SOURCE)
        for name in re.findall(r'RCDATA\s+"([^"]+)"', rc.read_text(encoding='utf-8', errors='replace')):
            assets.append((rc_tree, rc_tree.parent / name))
    return assets


def refresh_resources(tree, build_dir):
    """Touch each .rc whose assets are newer than its compiled .res (MSBuild misses them)."""
    touched = []
    for rc, asset in embedded_assets(tree):
        res = list(build_dir.rglob(rc.stem + '.res'))
        newest_res = max((r.stat().st_mtime for r in res), default=0)
        if asset.exists() and asset.stat().st_mtime > newest_res and str(rc) not in touched:
            os.utime(rc, None)
            touched.append(str(rc))
    return touched


def stale_assets(tree, backend):
    image = backend.read_bytes()
    return [str(asset) for _, asset in embedded_assets(tree) if asset.exists() and asset.read_bytes() not in image]


def memory():
    class Memory(ctypes.Structure):
        _fields_ = [('size', ctypes.c_ulong), ('load', ctypes.c_ulong)] + [
            (k, ctypes.c_ulonglong) for k in ('total', 'available', 'pageTotal',
            'pageAvailable', 'virtualTotal', 'virtualAvailable', 'extended')]
    m = Memory()
    m.size = ctypes.sizeof(m)
    if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(m)):
        raise OSError('Cannot read memory headroom')
    return {'load_percent': m.load, 'available_gib': round(m.available / 2**30, 2)}


def check_headroom():
    m = memory()
    if m['load_percent'] > 85 or m['available_gib'] < 8:
        raise MemoryError(f'Insufficient headroom: {m}')
    return m


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project', type=Path, required=True)
    parser.add_argument('--log-dir', type=Path, required=True)
    parser.add_argument('--msbuild', type=Path, default=Path(
        'C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe'))
    args = parser.parse_args()
    if os.name != 'nt' or not args.project.is_file() or not args.msbuild.is_file():
        parser.error('A Windows MSBuild executable and an existing .vcxproj are required')
    active = subprocess.check_output(['powershell.exe', '-NoProfile', '-Command',
        "[System.Diagnostics.Process]::GetProcesses() | Where-Object { $_.ProcessName -in @('ffmpeg','cl','MSBuild') } | Select-Object ProcessName,Id | ConvertTo-Json"],
        text=True).strip()
    if active:
        parser.error('Wait for the existing heavy job to finish: ' + active)
    # The project sits at <upstream>/out/build/<preset>/uevr.vcxproj.
    tree = args.project.resolve().parents[3]
    if not (tree / 'src/Framework.cpp').is_file():
        parser.error(f'{tree} does not look like the upstream UEVR tree')
    copied = mirror_sources(tree)
    differ = source_differences(tree)
    if differ:
        parser.error('Upstream tree still differs from mod/uevr: ' + ', '.join(differ))
    touched = refresh_resources(tree, args.project.parent)
    commit = subprocess.run(['git', '-C', str(REPO), 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout.strip()
    dirty = subprocess.run(['git', '-C', str(REPO), 'status', '--porcelain', '--', 'mod/uevr'],
                           capture_output=True, text=True).stdout.strip()
    for name in copied:
        print('copied into the upstream tree:', name, flush=True)
    for rc in touched:
        print('resource script touched (asset newer than .res):', rc, flush=True)
    args.log_dir.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime('%Y%m%d-%H%M%S')
    log = args.log_dir / ('build-' + stamp + '.log')
    receipt = args.log_dir / ('build-' + stamp + '.json')
    # Set /MP1 on the command line, not _CL_: workers inherit the environment.
    # The imported item definition comes after CMake's explicit bare /MP.
    env = os.environ.copy()
    if '/MP' in env.get('_CL_', '').upper() or '/MP' in env.get('CL', '').upper():
        parser.error('Remove the process-level CL/_CL_ /MP override before this bounded build')
    targets = args.project.parent / 'portal-toggle-pdb.targets'
    pdb = args.project.parent / 'bin/uevr/UEVRBackend.portal-toggle.pdb'
    targets.write_text(
        '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
        '<ItemDefinitionGroup>\n<ClCompile><MultiProcessorCompilation>false</MultiProcessorCompilation>'
        '<AdditionalOptions>%(AdditionalOptions) /MP1</AdditionalOptions></ClCompile>\n</ItemDefinitionGroup>\n'
        '<ItemDefinitionGroup Condition="\'$(MSBuildProjectName)\' == \'uevr\' and '
        '\'$(Configuration)\' == \'RelWithDebInfo\'">\n<Link><ProgramDatabaseFile>'
        + str(pdb) + '</ProgramDatabaseFile></Link>\n</ItemDefinitionGroup>\n</Project>\n', encoding='utf-8')
    cmd = [str(args.msbuild), str(args.project), '/t:Build', '/m:1', '/nr:false',
           '/p:Configuration=RelWithDebInfo', '/p:Platform=x64', '/p:BuildInParallel=false',
           '/p:MultiProcessorCompilation=false', '/p:CL_MPCount=1',
           '/p:ForceImportAfterCppTargets=' + str(targets), '/v:minimal']
    result = {'command': cmd, 'compiler_tail_options': '/MP1 (imported command-line option, not environment)',
              'started': time.strftime('%Y-%m-%dT%H:%M:%S%z'), 'before': check_headroom(),
              'log': str(log), 'status': 'running', 'priority': 'BelowNormal',
              'installed': False,
              'sources': {'repo_commit': commit, 'mod_uevr_uncommitted': bool(dirty), 'mod_uevr_sha256': source_hash(),
                          'copied_into_tree': copied, 'parity': 'mod/uevr == upstream tree (line endings aside)',
                          'resource_scripts_touched': touched}}
    receipt.write_text(json.dumps(result, indent=2), encoding='utf-8')
    with log.open('w', encoding='utf-8') as output:
        proc = subprocess.Popen(cmd, stdout=output, stderr=subprocess.STDOUT, env=env,
                                creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS)
        print(f'Build PID {proc.pid}; one compiler worker; {log}', flush=True)
        try:
            last_report = time.monotonic()
            process = psutil.Process(proc.pid)
            peak = 0
            telemetry = args.log_dir / ('processes-' + stamp + '.jsonl')
            result['process_telemetry'] = str(telemetry)
            while proc.poll() is None:
                result['last_memory'] = check_headroom()
                children = []
                try:
                    for child in process.children(recursive=True):
                        try:
                            m = child.memory_info()
                            children.append({'pid': child.pid, 'name': child.name(), 'rss': m.rss,
                                             'private': getattr(m, 'private', m.vms), 'command': child.cmdline()})
                        except psutil.NoSuchProcess:
                            pass
                except psutil.NoSuchProcess:
                    pass
                private = sum(p['private'] for p in children)
                peak = max(peak, private)
                result['peak_child_private_gib'] = round(peak/2**30, 2)
                with telemetry.open('a', encoding='utf-8') as t:
                    t.write(json.dumps({'time': time.time(), 'memory': result['last_memory'], 'children': children})+'\n')
                if private > 8 * 2**30:
                    raise MemoryError('Build tree exceeded its 8 GiB private-memory budget')
                if time.monotonic() - last_report >= 30:
                    print(f'Building; memory {result["last_memory"]}; build tree {private/2**30:.2f} GiB', flush=True)
                    last_report = time.monotonic()
                time.sleep(2)
            result['exit_code'] = proc.returncode
            result['status'] = 'built' if proc.returncode == 0 else 'failed'
        except BaseException as error:
            result.update(status='stopped', error=str(error))
            if proc.poll() is None:
                subprocess.run(['taskkill', '/PID', str(proc.pid), '/T', '/F'], capture_output=True)
                proc.wait()
            raise
        finally:
            result['ended'] = time.strftime('%Y-%m-%dT%H:%M:%S%z')
            if result['status'] == 'built':
                backend = args.project.parent / 'bin/uevr/UEVRBackend.dll'
                result['backend'] = {'path': str(backend), 'bytes': backend.stat().st_size,
                                     'sha256': hashlib.sha256(backend.read_bytes()).hexdigest()}
                stale = stale_assets(tree, backend)
                result['embedded_assets'] = {'checked': len(embedded_assets(tree)), 'stale': stale}
                if stale:
                    result.update(status='failed', exit_code=1,
                                  error='The DLL does not contain the current bytes of: ' + ', '.join(stale))
            receipt.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2), flush=True)
    return result['exit_code']


if __name__ == '__main__':
    raise SystemExit(main())
