#!/usr/bin/env python3
"""Offline summary of `eye_pair_diff` records from a WuWa LOD trace (lod-*.jsonl).

Read-only: it never touches the game, a headset or the profile. Each record lists
the dwords that differ between the two main-eye scene views (view_region) and
between their per-eye view states (state_region). Equal dwords are omitted, so the
interesting question is WHICH offsets differ, whether that changes between a far
(failing) capture and a near (control) capture, and whether the values look like
clocks, flags or pointers. Nothing here proves a GPU draw input; see the progress
document for what each outcome does and does not establish.

    wuwa_eye_diff_summary.py lod-far.jsonl
    wuwa_eye_diff_summary.py --compare lod-far.jsonl lod-near.jsonl
    wuwa_eye_diff_summary.py --raw state:0x528-0x560 lod-far.jsonl lod-near.jsonl

--raw reads the opt-in eye_pair_raw rows (every dword of both eye slots, equal values included,
with read validity) and prints absolute values; slots are not physical eyes until verified.
"""
import argparse
import json
import math
import struct
import sys
from bisect import bisect_left
from collections import defaultdict
from pathlib import Path

# Offsets already verified in this codebase (WuWaLodSnapshot/WuWaPlanarSnapshot/
# WuWaStereoParameters). `geometry` deltas are expected between eyes; `unexpected`
# ones (rect, pass, instanced/multiview flags) would be a finding by themselves.
KNOWN_VIEW = (
    (0x000, 0x008, 'family pointer', 'unexpected'),
    (0x008, 0x010, 'view-state pointer', 'expected'),
    (0x1a0, 0x1a4, 'stereo pass (early-primary mirror)', 'unexpected'),
    (0x2f8, 0x308, 'view rect', 'unexpected'),
    (0x320, 0x360, 'projection matrix', 'geometry'),
    (0x3e0, 0x420, 'view matrix', 'geometry'),
    (0x6ec, 0x6f8, 'temporal-LOD fallback origin', 'geometry'),
    (0xc90, 0xc94, 'stereo pass', 'unexpected'),
    (0xfea, 0x1002, 'mode bytes (+0xffe instanced, +0x1000 multiview)', 'unexpected'),
)
VERIFIED_VIEW_END = 0x1004  # beyond this the game-thread view extent is unproven
TIME_WINDOW_SECONDS = 120.0


def _float(bits):
    return struct.unpack('<f', struct.pack('<I', bits & 0xffffffff))[0]


def _plausible_float(bits):
    value = _float(bits)
    return math.isfinite(value) and (value == 0 or 1e-6 <= abs(value) <= 1e9)


def _is_time_like(a, b, clock):
    if clock is None:
        return False
    fa, fb = _float(a), _float(b)
    return (math.isfinite(fa) and math.isfinite(fb) and fa != fb and
            abs(fa - clock) <= TIME_WINDOW_SECONDS and abs(fb - clock) <= TIME_WINDOW_SECONDS)


def _text_like(bits):
    """Printable bytes that do not also read as an ordinary float, e.g. script text. Their origin is
    unknown: they do not show object extent or constructor behaviour. (0.9 is 0x3f666666, "fff?",
    so printable alone is not enough.)"""
    raw = (bits & 0xffffffff).to_bytes(4, 'little')
    printable = sum(32 <= c < 127 for c in raw)
    return (printable == 4 or (printable == 3 and 0 in raw)) and not _plausible_float(bits)


def _horizon(region):
    """Highest offset whose equality is known. Deltas are kept in ascending order, so a truncated
    region knows nothing above its last retained delta: absent there means unknown, not equal."""
    if region['truncated'] and region['deltas']:
        return region['deltas'][-1][0]
    return region['end'] - 4


def _known(offset):
    for begin, end, label, kind in KNOWN_VIEW:
        if begin <= offset < end:
            return label, kind
    if offset >= VERIFIED_VIEW_END:
        return 'tail (extent unverified, may be neighbouring heap)', 'unverified'
    return None, 'unclassified'


def load(path):
    """Return (samples, clock) where clock is a sorted list of (tick_ms, real_time seconds)."""
    samples, clock = [], []
    for line in Path(path).read_text(encoding='utf-8').splitlines():
        if not line.strip():
            continue
        row = json.loads(line)
        if row.get('type') == 'eye_pair_diff':
            samples.append(row)
        elif row.get('type') == 'uniforms' and isinstance(row.get('real_time'), (int, float)):
            clock.append((row['tick_ms'], float(row['real_time'])))
    clock.sort()
    return samples, clock


def _clock_at(clock, tick):
    if not clock:
        return None
    ticks = [item[0] for item in clock]
    i = bisect_left(ticks, tick)
    best = min((c for c in clock[max(0, i - 1):i + 1]), key=lambda item: abs(item[0] - tick))
    return best[1] if abs(best[0] - tick) <= 2000 else None


PHASES = {'before': ('before_submissions',), 'after': ('after_submissions',),
          'both': ('before_submissions', 'after_submissions')}


def summarize(samples, clock=(), phase='before'):
    """`before` is the default: with r.OneFrameThreadLag=0 the previous frame has finished, so those
    reads are stable. `after` runs while the render thread may still be writing the view states and
    can show torn values; use it only to see what the submissions themselves changed."""
    clock = list(clock)
    samples = [s for s in samples if s.get('phase') in PHASES[phase]]
    out = {'samples': len(samples), 'phase': phase, 'pairs': len({s['sequence'] for s in samples}),
           'regions': {}, 'warnings': []}
    for name in ('view_region', 'state_region'):
        per_offset = defaultdict(lambda: {'seen': 0, 'pairs': set(), 'values': [], 'time_like': 0})
        valid = unreadable = truncated = compared = 0
        horizons = []
        for s in samples:
            region = s[name]
            if not region['valid']:
                continue
            valid += 1
            horizons.append(_horizon(region))
            unreadable += region['unreadable']
            compared += region['compared']
            truncated += bool(region['truncated'])
            now = _clock_at(clock, s['tick_ms'])
            for offset, a, b in region['deltas']:
                entry = per_offset[offset]
                entry['seen'] += 1
                entry['pairs'].add(s['sequence'])
                if (a, b) not in entry['values'] and len(entry['values']) < 6:
                    entry['values'].append((a, b))
                entry['time_like'] += _is_time_like(a, b, now)
        rows = []
        known_until = min(horizons) if horizons else None
        for offset in sorted(per_offset):
            entry = per_offset[offset]
            label, kind = _known(offset) if name == 'view_region' else (None, 'unclassified')
            a, b = entry['values'][0]
            of = sum(1 for h in horizons if h >= offset)  # samples in which this offset is known
            rows.append({
                'offset': offset, 'seen': entry['seen'], 'of': of,
                'fraction': entry['seen'] / of if of else 0.0, 'kind': kind, 'label': label,
                'first_values': [a, b], 'as_float': [_float(a), _float(b)],
                'distinct_value_pairs': len(entry['values']),
                # The same unequal pair in every sample that knows it: a value that did not change during
                # the capture (a live field that held still, or bytes nothing rewrote). Not a cause by
                # itself either way, and not evidence of where an object ends.
                'static': of > 1 and entry['seen'] == of and len(entry['values']) == 1,
                'text_like': _text_like(a) or _text_like(b),
                'time_like': entry['time_like'] > 0,
                'small_int': a < 0x10000 and b < 0x10000,
                # A pointer's low half at an 8-aligned offset: its equal high half is omitted, and
                # neither value reads as an ordinary float. A hint only; a float can still hide here.
                'pointer_low_half_candidate': offset % 8 == 0 and a >= 0x10000 and b >= 0x10000 and
                    (offset + 4) not in per_offset and not _plausible_float(a) and not _plausible_float(b),
            })
        out['regions'][name] = {'valid_samples': valid, 'dwords_compared': compared,
                                'unreadable_dwords': unreadable, 'truncated_samples': truncated,
                                'known_until': known_until, 'differing_offsets': rows}
        if valid == 0 and samples:
            out['warnings'].append(f'{name}: no valid comparison in any sample')
        if truncated:
            out['warnings'].append(f'{name}: {truncated} sample(s) truncated; offsets above '
                                   f'{known_until:#x} are unknown in at least one sample and are '
                                   f'excluded from --compare')
        if unreadable:
            out['warnings'].append(f'{name}: {unreadable} unreadable dword(s) were skipped')
    if not samples:
        out['warnings'].append(f'no {phase!r}-phase eye_pair_diff records: old DLL, no LOD trace, or no valid NSF pair')
    return out


def compare(failing, control, threshold=0.8):
    """Offsets whose eye difference is present in one capture and (nearly) absent in the other."""
    result = {'failing_samples': failing['samples'], 'control_samples': control['samples'], 'regions': {}}
    for name in ('view_region', 'state_region'):
        a = {r['offset']: r for r in failing['regions'][name]['differing_offsets']}
        b = {r['offset']: r for r in control['regions'][name]['differing_offsets']}
        limits = [x for x in (failing['regions'][name].get('known_until'),
                              control['regions'][name].get('known_until')) if x is not None]
        known_until = min(limits) if limits else None
        only_failing, only_control = [], []
        for offset in sorted(set(a) | set(b)):
            if known_until is not None and offset > known_until:
                continue  # unknown in at least one sample: absence would be read as equality
            fa = a[offset]['fraction'] if offset in a else 0.0
            fb = b[offset]['fraction'] if offset in b else 0.0
            source = a.get(offset) or b.get(offset)
            item = {'offset': offset, 'failing_fraction': fa, 'control_fraction': fb,
                    'kind': source['kind'], 'label': source['label'],
                    'first_values': source['first_values'], 'as_float': source['as_float'],
                    'time_like': source['time_like'], 'small_int': source['small_int'],
                    'static': source.get('static', False), 'text_like': source.get('text_like', False)}
            if fa >= threshold and fb <= 1 - threshold:
                only_failing.append(item)
            elif fb >= threshold and fa <= 1 - threshold:
                only_control.append(item)
        result['regions'][name] = {'differs_only_while_failing': only_failing,
                                   'differs_only_in_control': only_control, 'known_until': known_until}
    return result


def _describe(row):
    bits = []
    if row.get('label'):
        bits.append(row['label'])
    if row.get('time_like'):
        bits.append('TIME-LIKE')
    if row.get('small_int'):
        bits.append('small int')
    if row.get('pointer_low_half_candidate'):
        bits.append('pointer low half?')
    if row.get('static'):
        bits.append('unchanged in every sample')
    if row.get('text_like'):
        bits.append('text bytes')
    return ', '.join(bits)


def eye_sides(path):
    """Provisional left/right label per eye slot, from the before-submission pair rows' projections.

    UE projection M[2][0] (flat index 8) is the horizontal off-centre term; a negative value puts the
    frustum centre right of the optical axis. Headset eyes are wider on their outer side, so that is the
    right eye. Assumes the compositor does not swap eyes. Geometry only: not verified against the
    displayed output, so callers must present it as provisional."""
    terms = {0: [], 1: []}
    for line in Path(path).read_text(encoding='utf-8').splitlines():
        if '"pair"' not in line:
            continue
        row = json.loads(line)
        if row.get('type') != 'pair' or row.get('phase') != 'before_submissions':
            continue
        for slot, view in enumerate(row.get('views', [])[:2]):
            projection = view.get('projection')
            if isinstance(projection, list) and len(projection) == 16:
                terms[slot].append(projection[8])
    sides = {}
    for slot, values in terms.items():
        if values:
            middle = sorted(values)[len(values) // 2]
            sides[f'slot{slot}'] = 'right' if middle < 0 else 'left' if middle > 0 else None
    return sides or None


def lifecycle(trace):
    """Probe counters from the `lod-inputs.json` wuwa-test.py writes beside a trace, or None.

    They explain an empty or thin trace: how many pairs reached each phase, lock misses and
    invalid snapshots per phase, and what the eye-diff sampler scheduled, took or orphaned.
    """
    inputs = Path(trace).with_name('lod-inputs.json')
    if not inputs.is_file():
        return None
    try:
        probe = json.loads(inputs.read_text(encoding='utf-8'))['end']['lod_probe']
    except (OSError, ValueError, KeyError, TypeError):
        return {'warnings': [f'{inputs.name}: unreadable or has no end.lod_probe status']}
    sampler, pairs = probe.get('eye_pair_diff', {}), probe.get('pairs')
    out = {'eye_pair_diff': sampler, 'pairs': pairs, 'warnings': []}
    if pairs is None or 'before_taken' not in sampler:
        out['warnings'].append('status has no per-phase pair counters: this DLL predates the '
                               'sampling repair, so an empty trace cannot be explained from it')
        return out
    before = pairs.get('before_submissions', {})
    if before.get('calls') and not sampler.get('before_taken'):
        out['warnings'].append('validated pairs reached the sampler but none was scheduled')
    if sampler.get('orphaned'):
        out['warnings'].append(f"{sampler['orphaned']} scheduled pair(s) never got their after-submissions snapshot")
    for phase, counts in pairs.items():
        if isinstance(counts, dict) and (counts.get('lock_misses') or counts.get('invalid')):
            out['warnings'].append(f"{phase}: {counts.get('lock_misses', 0)} lock miss(es), "
                                   f"{counts.get('invalid', 0)} invalid snapshot(s) of {counts.get('calls', 0)} calls")
    return out


def render(summary):
    lines = [f"{summary['samples']} {summary['phase']}-phase samples from {summary['pairs']} pairs"]
    if summary.get('eye_sides'):
        sides = summary['eye_sides']
        lines.append(f"eye slots (provisional, from projection off-centre; not verified against output): "
                     f"0 = {sides.get('slot0')}, 1 = {sides.get('slot1')}; eye0/eye1 below are slots")
    for warning in summary['warnings']:
        lines.append(f'WARNING: {warning}')
    life = summary.get('lifecycle')
    if life:
        sampler, pairs = life.get('eye_pair_diff') or {}, life.get('pairs') or {}
        if 'before_taken' in sampler:
            lines.append(f"sampler: {sampler['before_seen']} pairs seen, {sampler['before_taken']} scheduled, "
                         f"{sampler['after_taken']} completed, {sampler['orphaned']} orphaned, "
                         f"{sampler['ring_full']} refused for ring room")
        for phase in ('before_submissions', 'after_first_submission', 'after_submissions'):
            if isinstance(pairs.get(phase), dict):
                c = pairs[phase]
                lines.append(f"  {phase}: {c['calls']} calls, {c['rows']} rows, "
                             f"{c['lock_misses']} lock misses, {c['invalid']} invalid")
        for warning in life['warnings']:
            lines.append(f'LIFECYCLE: {warning}')
    for name, region in summary['regions'].items():
        lines.append('')
        lines.append(f"{name}: {region['valid_samples']} valid samples, {region['dwords_compared']} dwords compared, "
                     f"{len(region['differing_offsets'])} offsets ever differ")
        interesting = [r for r in region['differing_offsets'] if r['kind'] in ('unclassified', 'unexpected', 'unverified')
                       or name == 'state_region']
        for row in interesting[:200]:
            a, b = row['first_values']
            lines.append(f"  +{row['offset']:#06x} {row['seen']}/{row['of']} [{row['kind']}] "
                         f"eye0={a:#010x} eye1={b:#010x} f=({row['as_float'][0]:.6g},{row['as_float'][1]:.6g}) "
                         f"{_describe(row)}")
        hidden = len(region['differing_offsets']) - len(interesting)
        if hidden > 0:
            lines.append(f'  ({hidden} expected eye-geometry / expected-pointer offsets not listed)')
    return '\n'.join(lines)


def render_compare(result):
    lines = [f"failing: {result['failing_samples']} samples, control: {result['control_samples']} samples"]
    if result.get('eye_sides'):
        lines.append(f"eye slots (provisional, not verified against output): {result['eye_sides']}")
    for name, region in result['regions'].items():
        lines.append('')
        lines.append(f"{name}: differs only while failing: {len(region['differs_only_while_failing'])}, "
                     f"only in control: {len(region['differs_only_in_control'])}"
                     + (f" (compared up to +{region['known_until']:#06x}; above is unknown)"
                        if region.get('known_until') is not None else ''))
        for title, items in (('FAILING ONLY', region['differs_only_while_failing']),
                             ('control only', region['differs_only_in_control'])):
            for row in items[:100]:
                a, b = row['first_values']
                lines.append(f"  {title} +{row['offset']:#06x} fail={row['failing_fraction']:.2f} "
                             f"ctrl={row['control_fraction']:.2f} [{row['kind']}] eye0={a:#010x} eye1={b:#010x} "
                             f"f=({row['as_float'][0]:.6g},{row['as_float'][1]:.6g}) "
                             f"{'TIME-LIKE ' if row['time_like'] else ''}{'UNCHANGED ' if row.get('static') else ''}"
                             f"{'TEXT ' if row.get('text_like') else ''}{row['label'] or ''}")
    return '\n'.join(lines)


def parse_raw_spec(spec):
    """'state:0x528-0x560' (end exclusive) or 'view:0x8,0x150' -> (region, [offsets])."""
    region, _, items = spec.partition(':')
    if region not in ('view', 'state') or not items:
        raise ValueError(f'raw spec {spec!r}: use view:OFFSETS or state:OFFSETS')
    offsets = []
    for item in items.split(','):
        if '-' in item:
            begin, end = (int(x, 0) for x in item.split('-', 1))
            offsets += range(begin, end, 4)
        else:
            offsets.append(int(item, 0))
    if any(o % 4 for o in offsets):
        raise ValueError(f'raw spec {spec!r}: offsets must be dword-aligned')
    return region, offsets


def raw_values(path, region, offsets):
    """Absolute dwords of both eye slots from eye_pair_raw rows, with read validity."""
    out = []
    for line in Path(path).read_text(encoding='utf-8').splitlines():
        if '"eye_pair_raw"' not in line:
            continue
        row = json.loads(line)
        if row.get('type') != 'eye_pair_raw':
            continue
        slots = {slot['slot']: slot[region] for slot in row['slots']}
        values = []
        for offset in offsets:
            cells = []
            for slot in (0, 1):
                block = slots[slot]
                raw = bytes.fromhex(block['bytes_hex'])
                inside = offset + 4 <= len(raw)
                readable = inside and not any(b <= offset < e for b, e in block['unreadable'])
                cells.append(int.from_bytes(raw[offset:offset + 4], 'little') if readable else None)
            values.append({'offset': offset, 'slot0': cells[0], 'slot1': cells[1],
                           'equal': cells[0] is not None and cells[0] == cells[1]})
        out.append({'sequence': row['sequence'], 'phase': row['phase'], 'raw_ordinal': row.get('raw_ordinal'),
                    'tick_ms': row.get('tick_ms'), 'values': values})
    return out


def render_raw(path, region, rows):
    lines = [f"{path}: {len(rows)} raw snapshot(s), {region} region; slot0/slot1 are not physical eyes "
             "until verified against output; '--' = unreadable"]
    if not rows:
        lines.append('  no eye_pair_raw rows: raw snapshots were not requested, or the DLL predates them')
    for row in rows:
        lines.append(f"  seq {row['sequence']} {row['phase']} (raw pair {row['raw_ordinal']})")
        for v in row['values']:
            fmt = lambda x: '--' if x is None else f'{x:#010x}'
            lines.append(f"    +{v['offset']:#06x} slot0={fmt(v['slot0'])} slot1={fmt(v['slot1'])}"
                         f"{'  equal' if v['equal'] else ''}")
    return '\n'.join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('trace', nargs='+', help='one trace, or with --compare: failing then control')
    parser.add_argument('--compare', action='store_true', help='compare a failing capture against a control capture')
    parser.add_argument('--json', action='store_true', help='machine-readable output')
    parser.add_argument('--phase', choices=sorted(PHASES), default='before',
                        help="which snapshot to read (default: before; 'after' may race with the render thread)")
    parser.add_argument('--raw', metavar='REGION:OFFSETS',
                        help="print absolute values from eye_pair_raw rows, e.g. state:0x528-0x560 (any number of traces)")
    args = parser.parse_args(argv)
    if args.raw:
        try:
            region, offsets = parse_raw_spec(args.raw)
        except ValueError as error:
            parser.error(str(error))
        tables = [(path, raw_values(path, region, offsets)) for path in args.trace]
        if args.json:
            json.dump([{'trace': path, 'rows': rows} for path, rows in tables], sys.stdout, indent=1)
            sys.stdout.write('\n')
        else:
            print('\n\n'.join(render_raw(path, region, rows) for path, rows in tables))
        return 0
    if args.compare != (len(args.trace) == 2) or len(args.trace) > 2:
        parser.error('--compare takes exactly two traces (failing, control); otherwise pass one trace')
    summaries = [summarize(*load(path), phase=args.phase) for path in args.trace]
    result = compare(*summaries) if args.compare else summaries[0]
    result['eye_sides'] = eye_sides(args.trace[0])
    if not args.compare:
        result['lifecycle'] = lifecycle(args.trace[0])
    if args.json:
        json.dump(result, sys.stdout, indent=1)
        sys.stdout.write('\n')
    else:
        print(render_compare(result) if args.compare else render(result))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
