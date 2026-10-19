"""Rebuild the video of a recording whose clean-sbs.mp4 was never finalised (no moov box).

    python launcher/dev/recover-recording.py RECORDING_FOLDER [REFERENCE_RECORDING_FOLDER]

A recorder that is killed before Media Foundation's Finalize (19 Oct 2026: SteamVR lost the
headset and shut down, ending wuwa-recorder five seconds later) leaves ftyp + mdat with no index.
The mdat holds H.264 frames as length-prefixed NAL units, each frame starting with an access unit
delimiter, with AAC audio interleaved between them. SPS/PPS live only in the missing moov, so they
are copied from the reference recording's avcC; every recording with the same encoder settings
has the same one. Writes clean-sbs-recovered.mp4 (video only, 30 fps) beside the broken file and
never changes it.
"""
import os
import struct
import subprocess
import sys
from pathlib import Path

FFMPEG = os.environ.get('WUWA_FFMPEG', r'C:\Users\ChronoHax\ffmpeg-2023-11-02-git-4dbfb52230-full_build\bin\ffmpeg.exe')
AUD = b'\x00\x00\x00\x02\x09'  # 4-byte length 2, then nal_unit_type 9 (access unit delimiter)
START = b'\x00\x00\x00\x01'


def boxes(data):
    pos = 0
    while pos + 8 <= len(data):
        size, kind = struct.unpack('>I4s', data[pos:pos + 8])
        header = 8
        if size == 1:
            size, header = struct.unpack('>Q', data[pos + 8:pos + 16])[0], 16
        elif size == 0:
            size = len(data) - pos
        yield kind, pos, header, size
        if size < header:
            return
        pos += size


def avcc_parameter_sets(reference_mp4):
    """SPS and PPS NAL units from the reference recording's avcC box."""
    size = reference_mp4.stat().st_size
    with reference_mp4.open('rb') as f:
        f.seek(max(0, size - 16_000_000))
        tail = f.read()
    at = tail.find(b'avcC')
    if at < 4:
        raise SystemExit(f'no avcC in {reference_mp4}')
    box = tail[at + 4:at - 4 + int.from_bytes(tail[at - 4:at], 'big')]
    pos, units = 6, []
    for count_byte in (box[5] & 0x1f, None):
        if count_byte is None:
            count_byte = box[pos]; pos += 1
        for _ in range(count_byte):
            length = int.from_bytes(box[pos:pos + 2], 'big')
            units.append(box[pos + 2:pos + 2 + length])
            pos += 2 + length
    return units


def frames(mdat):
    """Yield each video frame as a list of NAL units; skip the audio between frames."""
    pos = mdat.find(AUD)
    while 0 <= pos < len(mdat):
        units = []
        while pos + 5 <= len(mdat):
            length = int.from_bytes(mdat[pos:pos + 4], 'big')
            nal = mdat[pos + 4] if pos + 4 < len(mdat) else 0x80
            kind = nal & 0x1f
            valid = (nal & 0x80) == 0 and kind in (1, 5, 6, 9) and 1 <= length <= len(mdat) - pos - 4
            if not valid or (units and kind == 9):
                break
            units.append(mdat[pos + 4:pos + 4 + length])
            pos += 4 + length
            if kind in (1, 5):
                break  # one slice per frame: audio follows
        if any(u[0] & 0x1f in (1, 5) for u in units) and pos <= len(mdat):
            yield units
        pos = mdat.find(AUD, pos)


def main():
    folder = Path(sys.argv[1])
    broken = folder / 'clean-sbs.mp4'
    out = folder / 'clean-sbs-recovered.mp4'
    if out.exists():
        raise SystemExit(f'{out} exists; move it first')
    reference = Path(sys.argv[2]) if len(sys.argv) > 2 else next(
        p for p in sorted(folder.parent.iterdir(), reverse=True)
        if p.is_dir() and p != folder and (p / 'recording.json').is_file() and (p / 'clean-sbs.mp4').is_file())
    data = broken.read_bytes()
    kinds = {kind: (pos, header, size) for kind, pos, header, size in boxes(data)}
    if b'moov' in kinds:
        raise SystemExit('this file has a moov box; it should play as it is')
    pos, header, size = kinds[b'mdat']
    mdat = data[pos + header:pos + size]
    parameter_sets = avcc_parameter_sets(reference / 'clean-sbs.mp4')
    elementary = folder.parent / (folder.name + '.recovering.h264')
    count = keyframes = 0
    with elementary.open('wb') as f:
        for units in frames(mdat):
            if any(u[0] & 0x1f == 5 for u in units):
                keyframes += 1
                f.write(b''.join(START + u for u in parameter_sets))
            f.write(b''.join(START + u for u in units))
            count += 1
    try:
        subprocess.run([FFMPEG, '-v', 'error', '-r', '30', '-i', str(elementary), '-c', 'copy',
                        '-movflags', '+faststart', str(out)], check=True)
    finally:
        elementary.unlink(missing_ok=True)
    print(f'{count} frames ({keyframes} keyframes, {count / 30:.1f} s at 30 fps) -> {out}; reference {reference.name}')


if __name__ == '__main__':
    main()
