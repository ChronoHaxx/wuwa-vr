#!/usr/bin/env python3
"""Build the SteamVR recorder (launcher/dev/steamvr-capture/record.cpp) and run its self-tests.

    python dev/build-recorder.py [--out builds/next-release-1.2.4/recorder]

Builds in %TEMP%/wuwa-recorder-build (the system SSD, not the USB drive), then runs the GPU and encoder
self-tests: side by side, the showcase view, and a 1440p High-profile encode. With --out, the tested
wuwa-recorder.exe is copied there (for dev/candidate.py --recorder or a release). No game or VR access.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
SOURCE = REPO / 'launcher/dev/steamvr-capture'
CMAKE = Path(r'C:\Program Files\CMake\bin\cmake.exe')


def run(*command):
    print('>', ' '.join(str(c) for c in command), flush=True)
    subprocess.run([str(c) for c in command], check=True)


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--out', type=Path, help='folder for the tested wuwa-recorder.exe')
    a = p.parse_args(argv)
    cmake = CMAKE if CMAKE.exists() else shutil.which('cmake')
    if not cmake:
        sys.exit('CMake is missing')
    build = Path(os.environ.get('TEMP', tempfile.gettempdir())) / 'wuwa-recorder-build'
    run(cmake, '-S', SOURCE, '-B', build, '-G', 'Visual Studio 17 2022', '-A', 'x64')
    run(cmake, '--build', build, '--config', 'Release', '--target', 'wuwa-recorder', '--', '/m', '/v:minimal')
    exe = build / 'Release/wuwa-recorder.exe'
    with tempfile.TemporaryDirectory(prefix='wuwa-recorder-test ') as scratch:
        run(exe, '--self-test', Path(scratch) / 'sbs.mp4', '60')
        run(exe, '--self-test-showcase', Path(scratch) / 'showcase.mp4')
    if a.out:
        a.out.mkdir(parents=True, exist_ok=True)
        shutil.copy2(exe, a.out / exe.name)
        print('tested recorder:', a.out / exe.name)
    return 0


if __name__ == '__main__':
    sys.exit(main())
