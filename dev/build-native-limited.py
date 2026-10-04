"""Build one existing MSVC project with one compiler worker and memory headroom.

Does not configure CMake, install a DLL, stop the game or change global settings.
Refuses to start alongside another compiler/ffmpeg job. Run again after a failed
or memory-limited attempt to let MSBuild reuse completed objects.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
import psutil


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
              'installed': False}
    receipt.write_text(json.dumps(result, indent=2), encoding='utf-8')
    with log.open('w', encoding='utf-8') as output:
        proc = subprocess.Popen(cmd, stdout=output, stderr=subprocess.STDOUT, env=env,
                                creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS)
        print(f'Build PID {proc.pid}; one compiler worker; {log}', flush=True)
        try:
            last_report = time.monotonic()
            tree = psutil.Process(proc.pid)
            peak = 0
            telemetry = args.log_dir / ('processes-' + stamp + '.jsonl')
            result['process_telemetry'] = str(telemetry)
            while proc.poll() is None:
                result['last_memory'] = check_headroom()
                children = []
                try:
                    for child in tree.children(recursive=True):
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
            receipt.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2), flush=True)
    return result['exit_code']


if __name__ == '__main__':
    raise SystemExit(main())
