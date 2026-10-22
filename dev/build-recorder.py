#!/usr/bin/env python3
"""Build the SteamVR recorder (launcher/dev/steamvr-capture/record.cpp) and run its self-tests.

    python dev/build-recorder.py [--out builds/next-release-1.2.4/recorder]

Builds in %TEMP%/wuwa-recorder-build (the system SSD, not the USB drive), then runs the GPU and encoder
self-tests: side by side, the showcase view, a 1440p High-profile encode, and the largest side-by-side
video (4096x2048) with a sound track, the sound mixer and the microphone list, then crash safety: a killed writer and a blocked capture loop must
leave playable files (checked with ffprobe when present). With --out, the tested
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
FFPROBE = r'C:\Users\ChronoHax\ffmpeg-2023-11-02-git-4dbfb52230-full_build\bin\ffprobe.exe'


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
        # 2048 px per eye (4096x2048) with a sound track, and 1.2 s of real loopback when there is an output.
        run(exe, '--self-test-large', Path(scratch) / 'large.mp4')
        # The sound mixer (1.2.6): what the PC plays and a microphone, placed by time; synthetic, no devices.
        run(exe, '--self-test-mixer')
        # The microphone list the launcher shows (it may be empty).
        run(exe, '--list-microphones')
        # Crash safety (19 Oct 2026): a recorder killed mid-part, and one whose capture loop blocks, still leave a
        # playable file (fragmented MP4; the watchdog closes the file and writes the report).
        run(exe, '--self-test-kill', Path(scratch) / 'killed.mp4')
        run(exe, '--self-test-stall', Path(scratch) / 'stalled')
        ffprobe = shutil.which('ffprobe') or FFPROBE
        if Path(ffprobe).is_file():
            for video in (Path(scratch) / 'killed.mp4', Path(scratch) / 'stalled/clean-sbs.mp4', Path(scratch) / 'large.mp4'):
                frames = subprocess.run([ffprobe, '-v', 'error', '-count_frames', '-select_streams', 'v:0', '-show_entries',
                                         'stream=nb_read_frames', '-of', 'csv=p=0', str(video)],
                                        capture_output=True, text=True, check=True).stdout.strip()
                print(f'ffprobe decodes {frames} frames of {video.name}', flush=True)
                if not frames.isdigit() or int(frames) < 40:
                    sys.exit(f'{video} does not decode: {frames!r}')
    if a.out:
        a.out.mkdir(parents=True, exist_ok=True)
        shutil.copy2(exe, a.out / exe.name)
        print('tested recorder:', a.out / exe.name)
    return 0


if __name__ == '__main__':
    sys.exit(main())
