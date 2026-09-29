#!/usr/bin/env python3
"""Compile the probe's VERBATIM pair() and eye-diff serializer against real nlohmann/json and
replay the real NSF callback lifecycle through them.

WuWaLodProbe.hpp needs Windows headers, so this extracts the two eye-diff serializers,
`struct Record` and `pair()` verbatim, compiles them inside eye-diff-lifecycle-harness.cpp
(instrumented SRW lock, clock and guarded reader over fake game memory) and drives the call
order FFakeStereoRenderingHook.cpp uses:

    token = pair(family, view0, view1, 0x64)     # phase 1, before the first submission
    <first submission assigns the family frame>
    if token: pair(..., token, 3)                # phase 3, after the first submission
    <second submission; render callbacks now hold the shared callback lock>
    if token: pair(..., token)                   # phase 2, after both submissions

Each scenario's outcome is checked here, and the raw-snapshot rows (both eyes, every dword,
equal values included) are decoded and checked against the values planted in fake memory. The fixture scenario's JSONL is the schema the
offline summarizer reads; default mode fails if it differs from the committed fixture.

    check-eye-diff-json.py --json-include DIR                 # verify (DIR holds nlohmann/json.hpp)
    check-eye-diff-json.py --json-include DIR --update        # rewrite the fixture
    check-eye-diff-json.py --json-include DIR --utility OLD   # lifecycle only, against other sources

`--utility` points at another copy of mod/uevr/src/utility (for example merged PR #1) to show
which lifecycle expectations that version fails. Needs g++ (or $CXX) with C++20. This is not
an MSVC build and not game evidence: the full native target still needs the Windows toolchain,
and only a capture in the game shows what the game does.
"""
import argparse
import json
import os
import pathlib
import subprocess
import sys
import tempfile

here = pathlib.Path(__file__).resolve().parent
default_utility = (here / '../../mod/uevr/src/utility').resolve()
if not default_utility.is_dir():
    default_utility = (here / '../upstream/UEVR/src/utility').resolve()
fixture = here / 'fixtures' / 'eye-pair-diff-native-sample.jsonl'
harness = here / 'eye-diff-lifecycle-harness.cpp'
INTERVAL = 60
UNASSIGNED = 0xffffffff
RAW_SEQUENCES = [1, 1, 301, 301, 601, 601]  # eye-diff pairs 0, 5 and 10, before and after


def raw_row_checks(path):
    """Decode the raw rows the verbatim serializer wrote and compare with the planted memory."""
    rows = [json.loads(line) for line in path.read_text().splitlines()]
    checks = [('raw rows written', len(rows), 6)]
    for row in rows:
        slots = {slot['slot']: slot for slot in row['slots']}
        def dword(slot, region, offset):
            raw = bytes.fromhex(slots[slot][region]['bytes_hex'])
            return int.from_bytes(raw[offset:offset + 4], 'little')
        tag = f"raw seq {row['sequence']} {row['phase']}"
        checks += [
            (f'{tag}: markers', (row['type'], row['equal_values_included'], row['physical_eye']),
             ('eye_pair_raw', True, 'unverified; slot order only')),
            (f'{tag}: full windows', [len(slots[k][r]['bytes_hex']) // 2 for k in (0, 1) for r in ('view', 'state')],
             [0x1e40, 0x4000, 0x1e40, 0x4000]),
            (f'{tag}: equal value kept in both eyes (+0x550)', (dword(0, 'state', 0x550), dword(1, 'state', 0x550)), (2, 2)),
            (f'{tag}: unequal value (+0x554)', (dword(0, 'state', 0x554), dword(1, 'state', 0x554)), (1, 0)),
            (f'{tag}: identity is the view-state pointer (+0x8)', (dword(0, 'view', 8), dword(1, 'view', 8)),
             (0x1100000, 0x1900000)),
            (f'{tag}: read validity', (slots[0]['state']['unreadable'], slots[1]['state']['unreadable'],
                                       slots[1]['view']['unreadable']), ([], [[0x3c00, 0x4000]], [])),
            (f'{tag}: unreadable bytes are zero', dword(1, 'state', 0x3ffc), 0),
        ]
    return checks


def extract(utility):
    probe = (utility / 'WuWaLodProbe.hpp').read_bytes().decode().replace('\r\n', '\n')
    start = probe.index('inline Json eye_diff_region_json')
    record = probe[start:probe.index('struct Slot {', start)]
    pair_start = probe.index('inline uint64_t pair(')
    pair = probe[pair_start:probe.index('inline Json status_locked()', pair_start)]
    return record, pair


def row_frames(frames):
    return sum(1 for f in frames if f % 30 < 4)


def expectations(o):
    """(label, actual, expected) for one scenario's output."""
    name, e = o['scenario'], o['eye_diff']
    seqs = o['sequences']
    scheduled = [q for q in range(1, seqs + 1) if (q - 1) % INTERVAL == 0]
    raw = o.get('raw')
    checks = [('pair() released every lock and never lost the guard', o['guard_misses'], 0),
              ('no eye-diff sample dropped', e['dropped'], 0),
              ('eye-diff ring never overflowed', e['truncated'], False),
              ('every sampled region was valid', e['invalid_regions'], 0)]
    if name not in ('raw_snapshots', 'ring_capacity'):
        checks.append(('raw snapshots are opt-in', (raw or {}).get('snapshots', 0), 0))
    if name == 'far_capture_lifecycle':
        on = row_frames(o['valid_frames'])
        checks += [
            ('invalid early snapshots issue no sequence', seqs, 475),
            ('invalid early snapshots are counted', o['pairs']['before']['invalid'], 5),
            ('before-submission samples on the pair-sequence schedule', e['before_sequences'], scheduled),
            ('each before-sample has its after-sample', (e['after'], e['after_paired']), (len(scheduled),) * 2),
            ('before-sample frames are recorded as read (unassigned)', e['before_frames'], [UNASSIGNED]),
            ('before rows emitted once the pair frame is assigned', o['rows']['before'], on),
            ('after-first-submission rows', o['rows']['after_first'], on),
            ('after-submission rows despite render callbacks holding the lock', o['rows']['after'], on),
            ('no lock miss after the submissions', o['pairs']['after']['lock_misses'], 0),
        ]
    elif name == 'stride_two_odd_frames':
        either = sum(1 for f, g in zip(o['valid_frames'], o['after_frames']) if f % 30 < 4 or g % 30 < 4)
        checks += [
            ('stride does not alias the schedule away', e['before_sequences'], scheduled),
            ('each before-sample has its after-sample', e['after_paired'], len(scheduled)),
            ('before row follows whichever later snapshot is on interval', o['rows']['before'], either),
        ]
    elif name == 'after_snapshot_lost':
        checks += [
            ('scheduled before-samples still taken', e['before'], len(scheduled)),
            ('no after-sample without its phase-2 call', e['after'], 0),
            ('lost after-snapshots counted as orphans', (o['sampler'] or {}).get('orphaned'), len(scheduled)),
        ]
    elif name == 'render_holds_lock_before':
        misses = 420 // 7
        checks += [
            ('phase-1 lock misses counted per phase', o['pairs']['before']['lock_misses'], misses),
            ('a missed phase 1 consumes no sequence', seqs, 420 - misses),
            ('schedule continues over missed pairs', e['before_sequences'], scheduled),
            ('each before-sample has its after-sample', e['after_paired'], len(scheduled)),
        ]
    elif name == 'state_changed_mid_pair':
        changed = len(scheduled)  # indices 0, 60, ... are exactly the scheduled sequences
        checks += [
            ('changed state refused after the first submission', o['pairs']['after_first']['invalid'], changed),
            ('changed state refused after both submissions', o['pairs']['after']['invalid'], changed),
            ('no after-sample of a changed pair; orphan counted',
             (e['after'], (o['sampler'] or {}).get('orphaned')), (0, changed)),
            ('rows of unchanged pairs unaffected', o['rows']['after'],
             row_frames(f for i, f in enumerate(o['valid_frames']) if i % 60)),
        ]
    elif name == 'ring_capacity':
        checks += [
            ('ring filled with whole pairs only', (e['before'], e['after'], e['after_paired']), (64, 64, 64)),
            ('pairs refused for room are counted', (o['sampler'] or {}).get('ring_full'), len(scheduled) - 64),
            ('raw snapshots stop at their fixed pair budget', (raw or {}).get('sequences'), RAW_SEQUENCES),
            ('raw ring never overflowed', ((raw or {}).get('dropped'), (raw or {}).get('truncated')), (0, False)),
        ]
    elif name == 'raw_snapshots':
        checks += [
            ('raw pairs follow every 5th eye-diff pair', (raw or {}).get('sequences'), RAW_SEQUENCES),
            ('raw pairs are before/after of one sequence', (raw or {}).get('phases'), [1, 2] * 3),
            ('raw ordinals', (raw or {}).get('ordinals'), [0, 0, 1, 1, 2, 2]),
            ('readable dwords: views whole, second state short by 0x400 bytes',
             (raw or {}).get('readable'), [[0x1e40 // 4, 0x1e40 // 4, 0x4000 // 4, 0x3c00 // 4]] * 6),
            ('each raw pair is also an eye-diff pair', set((raw or {}).get('sequences') or [0]) <= set(e['before_sequences']), True),
        ]
    elif name == 'fixture':
        checks += [('fixture holds two whole pairs', (e['before'], e['after_paired']), (2, 2))]
    return checks


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--json-include', required=True, help='directory that contains nlohmann/json.hpp')
    parser.add_argument('--utility', type=pathlib.Path, help='other WuWa utility sources; lifecycle only')
    parser.add_argument('--update', action='store_true', help='rewrite the committed fixture')
    args = parser.parse_args()
    utility = (args.utility or default_utility).resolve()
    record, pair = extract(utility)
    with tempfile.TemporaryDirectory() as work:
        work = pathlib.Path(work)
        (work / 'probe-record.inc').write_text(record)
        (work / 'probe-pair.inc').write_text(pair)
        binary = work / 'harness'
        build = subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++20', '-Wall', '-Wextra', '-Werror', '-O1',
                                f'-I{args.json_include}', f'-I{utility}', f'-I{work}', str(harness),
                                '-o', str(binary)], capture_output=True, text=True)
        if build.returncode:
            sys.exit('pair()/serializer compile failed:\n' + build.stdout + build.stderr)
        produced = work / 'produced.jsonl'
        raw_rows = work / 'raw.jsonl'
        run = subprocess.run([str(binary), str(produced), str(raw_rows)], capture_output=True, text=True)
        if run.returncode:
            sys.exit('harness failed:\n' + run.stdout + run.stderr)
        failures = 0
        results = []
        for line in run.stdout.splitlines():
            outcome = json.loads(line)
            results += [(outcome['scenario'], *check) for check in expectations(outcome)]
        results += [('raw_rows', *check) for check in raw_row_checks(raw_rows)]
        for scenario, label, actual, expected in results:
            if True:
                ok = actual == expected
                failures += not ok
                if not ok or os.environ.get('VERBOSE'):
                    print(f"{'ok  ' if ok else 'FAIL'} {scenario}: {label}: {actual!r}"
                          + ('' if ok else f' (expected {expected!r})'))
        if failures:
            sys.exit(f'{failures} lifecycle expectation(s) failed for {utility}')
        print('pair() lifecycle scenarios pass')
        if args.utility:
            return
        if args.update:
            fixture.parent.mkdir(exist_ok=True)
            fixture.write_bytes(produced.read_bytes())
            print('fixture rewritten:', fixture)
        elif produced.read_bytes() != fixture.read_bytes():
            sys.exit('native serializer output differs from ' + str(fixture) + '; review, then rerun with --update')
        else:
            print('eye-diff serializer compiles and matches the committed fixture')


if __name__ == '__main__':
    main()
