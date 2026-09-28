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
        for s in samples:
            region = s[name]
            if not region['valid']:
                continue
            valid += 1
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
        for offset in sorted(per_offset):
            entry = per_offset[offset]
            label, kind = _known(offset) if name == 'view_region' else (None, 'unclassified')
            a, b = entry['values'][0]
            rows.append({
                'offset': offset, 'seen': entry['seen'], 'of': valid,
                'fraction': entry['seen'] / valid if valid else 0.0, 'kind': kind, 'label': label,
                'first_values': [a, b], 'as_float': [_float(a), _float(b)],
                'distinct_value_pairs': len(entry['values']),
                'time_like': entry['time_like'] > 0,
                'small_int': a < 0x10000 and b < 0x10000,
                # A pointer's low half at an 8-aligned offset: its equal high half is omitted, and
                # neither value reads as an ordinary float. A hint only; a float can still hide here.
                'pointer_low_half_candidate': offset % 8 == 0 and a >= 0x10000 and b >= 0x10000 and
                    (offset + 4) not in per_offset and not _plausible_float(a) and not _plausible_float(b),
            })
        out['regions'][name] = {'valid_samples': valid, 'dwords_compared': compared,
                                'unreadable_dwords': unreadable, 'truncated_samples': truncated,
                                'differing_offsets': rows}
        if valid == 0 and samples:
            out['warnings'].append(f'{name}: no valid comparison in any sample')
        if truncated:
            out['warnings'].append(f'{name}: {truncated} sample(s) truncated; high offsets may be missing')
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
        only_failing, only_control = [], []
        for offset in sorted(set(a) | set(b)):
            fa = a[offset]['fraction'] if offset in a else 0.0
            fb = b[offset]['fraction'] if offset in b else 0.0
            source = a.get(offset) or b.get(offset)
            item = {'offset': offset, 'failing_fraction': fa, 'control_fraction': fb,
                    'kind': source['kind'], 'label': source['label'],
                    'first_values': source['first_values'], 'as_float': source['as_float'],
                    'time_like': source['time_like'], 'small_int': source['small_int']}
            if fa >= threshold and fb <= 1 - threshold:
                only_failing.append(item)
            elif fb >= threshold and fa <= 1 - threshold:
                only_control.append(item)
        result['regions'][name] = {'differs_only_while_failing': only_failing,
                                   'differs_only_in_control': only_control}
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
    return ', '.join(bits)


def render(summary):
    lines = [f"{summary['samples']} {summary['phase']}-phase samples from {summary['pairs']} pairs"]
    for warning in summary['warnings']:
        lines.append(f'WARNING: {warning}')
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
    for name, region in result['regions'].items():
        lines.append('')
        lines.append(f"{name}: differs only while failing: {len(region['differs_only_while_failing'])}, "
                     f"only in control: {len(region['differs_only_in_control'])}")
        for title, items in (('FAILING ONLY', region['differs_only_while_failing']),
                             ('control only', region['differs_only_in_control'])):
            for row in items[:100]:
                a, b = row['first_values']
                lines.append(f"  {title} +{row['offset']:#06x} fail={row['failing_fraction']:.2f} "
                             f"ctrl={row['control_fraction']:.2f} [{row['kind']}] eye0={a:#010x} eye1={b:#010x} "
                             f"f=({row['as_float'][0]:.6g},{row['as_float'][1]:.6g}) "
                             f"{'TIME-LIKE ' if row['time_like'] else ''}{row['label'] or ''}")
    return '\n'.join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('trace', nargs='+', help='one trace, or with --compare: failing then control')
    parser.add_argument('--compare', action='store_true', help='compare a failing capture against a control capture')
    parser.add_argument('--json', action='store_true', help='machine-readable output')
    parser.add_argument('--phase', choices=sorted(PHASES), default='before',
                        help="which snapshot to read (default: before; 'after' may race with the render thread)")
    args = parser.parse_args(argv)
    if args.compare != (len(args.trace) == 2) or len(args.trace) > 2:
        parser.error('--compare takes exactly two traces (failing, control); otherwise pass one trace')
    summaries = [summarize(*load(path), phase=args.phase) for path in args.trace]
    result = compare(*summaries) if args.compare else summaries[0]
    if args.json:
        json.dump(result, sys.stdout, indent=1)
        sys.stdout.write('\n')
    else:
        print(render_compare(result) if args.compare else render(result))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
