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
    wuwa_eye_diff_summary.py --layout lod-far.jsonl
    wuwa_eye_diff_summary.py --conditions far/lod.jsonl far-early/lod.jsonl near/lod.jsonl

--raw reads the opt-in eye_pair_raw rows (every dword of both eye slots, equal values included,
with read validity) and prints absolute values. View-state offsets stop at the state extent (see
STATE_EXTENT; --state-extent overrides it). --layout lists the class-pointer markers behind that
extent. --conditions prints what was in effect for each trace (settings, each slot's stereo pass per
phase, uniform producers per slot, which display half shows which slot).
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
# Per-eye view state (the object at view +0x8). Its sizeof is NOT known. The game binary would give it,
# for example the size passed to operator delete in the class's deleting destructor, reachable from the
# class pointer at state +0x0. STATE_EXTENT is an evidence-backed LOWER bound from the 29 Sep raw
# snapshots: both eyes' states, allocated separately, hold the same game-image pointer at +0x2058 and
# +0x2080 (one class, a member repeated at a 0x28 stride; see layout_markers), so their common layout
# runs to at least +0x20a8. A same-type neighbour at the same distance from both objects would look the
# same, so this bounds the shared layout, not the object. Text bytes do not mark the end: slot 1 holds
# script text at +0x1800..+0x1e00, below that member, and in its padding halves at +0x2074/+0x209c.
# --compare and --raw stop here; state offsets at or above it are counted, never compared.
STATE_EXTENT = 0x20a8
# Game-image address range used to recognise class pointers in raw snapshots. The image loads at its
# preferred base (class pointers 0x1677a0b40 etc. in every capture so far); heap objects sit far above.
IMAGE_RANGE = (0x140000000, 0x180000000)
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


def summarize(samples, clock=(), phase='before', state_extent=STATE_EXTENT):
    """`before` is the default: with r.OneFrameThreadLag=0 the previous frame has finished, so those
    reads are stable. `after` runs while the render thread may still be writing the view states and
    can show torn values; use it only to see what the submissions themselves changed.

    State deltas at or above `state_extent` are counted (`beyond_extent`) but not summarized: past the
    view state's known layout a difference says nothing about the eye's state."""
    clock = list(clock)
    samples = [s for s in samples if s.get('phase') in PHASES[phase]]
    out = {'samples': len(samples), 'phase': phase, 'pairs': len({s['sequence'] for s in samples}),
           'regions': {}, 'warnings': []}
    for name in ('view_region', 'state_region'):
        per_offset = defaultdict(lambda: {'seen': 0, 'pairs': set(), 'values': [], 'time_like': 0})
        valid = unreadable = truncated = compared = 0
        horizons, beyond = [], set()
        extent = state_extent if name == 'state_region' else None
        for s in samples:
            region = s[name]
            if not region['valid']:
                continue
            valid += 1
            limit = region['end'] - 4 if extent is None else min(region['end'], extent) - 4
            horizon = min(_horizon(region), limit)
            horizons.append(horizon)
            unreadable += region['unreadable']
            compared += region['compared']
            truncated += horizon < limit  # truncation below the extent; above it nothing is compared anyway
            now = _clock_at(clock, s['tick_ms'])
            for offset, a, b in region['deltas']:
                if extent is not None and offset >= extent:
                    beyond.add(offset)
                    continue
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
                                'known_until': known_until, 'extent': extent,
                                'beyond_extent': len(beyond), 'differing_offsets': rows}
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
                                   'differs_only_in_control': only_control, 'known_until': known_until,
                                   'extent': failing['regions'][name].get('extent'),
                                   'beyond_extent': [failing['regions'][name].get('beyond_extent', 0),
                                                     control['regions'][name].get('beyond_extent', 0)]}
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
    if summary.get('conditions'):
        lines.append(render_conditions('conditions', summary['conditions']))
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
        if region.get('extent') is not None:
            lines.append(f"  {_extent_note(region['extent'])}; {region['beyond_extent']} differing offset(s) "
                         f"at or above it not summarized")
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
    if result.get('display'):
        lines.append(f"display (failing trace's Swap Eyes setting): left = {result['display']['left']}, "
                     f"right = {result['display']['right']}")
    for name, region in result['regions'].items():
        lines.append('')
        lines.append(f"{name}: differs only while failing: {len(region['differs_only_while_failing'])}, "
                     f"only in control: {len(region['differs_only_in_control'])}"
                     + (f" (compared up to +{region['known_until']:#06x}; above is unknown)"
                        if region.get('known_until') is not None else ''))
        if region.get('extent') is not None:
            failing_beyond, control_beyond = region['beyond_extent']
            lines.append(f"  {_extent_note(region['extent'])}; not compared at or above it "
                         f"(failing {failing_beyond}, control {control_beyond} differing offsets there)")
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


def _extent_note(extent):
    source = ('default: evidence-backed lower bound, sizeof unknown' if extent == STATE_EXTENT
              else 'set with --state-extent')
    return f"state extent +{extent:#06x} ({source})"


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


def split_extent(region, offsets, state_extent=STATE_EXTENT):
    """(kept, dropped) offsets: view-state offsets at or above the extent are not read."""
    if region != 'state':
        return list(offsets), []
    return [o for o in offsets if o < state_extent], [o for o in offsets if o >= state_extent]


def _raw_rows(path):
    for line in Path(path).read_text(encoding='utf-8').splitlines():
        if '"eye_pair_raw"' in line:
            row = json.loads(line)
            if row.get('type') == 'eye_pair_raw':
                yield row


def _qwords(block):
    """{offset: value} for every 8-aligned qword whose two dwords were both read."""
    raw = bytes.fromhex(block['bytes_hex'])
    bad = block['unreadable']
    return {o: int.from_bytes(raw[o:o + 8], 'little') for o in range(0, len(raw) - 7, 8)
            if not any(b < o + 8 and o < e for b, e in bad)}


def layout_markers(path, region='state', image=IMAGE_RANGE):
    """Offsets where both eye slots hold the same game-image pointer (a class or function pointer) in every
    raw snapshot of a trace. Two separately allocated objects with one class's pointer at the same offset
    is what a shared member looks like, so the last marker bounds their common layout from below: its
    offset plus the member stride when its value repeats at an earlier marker, else plus 8. A lower bound
    only; the object's sizeof has to come from the binary."""
    common, snapshots = None, 0
    for row in _raw_rows(path):
        slots = {slot['slot']: _qwords(slot[region]) for slot in row['slots']}
        a, b = slots.get(0, {}), slots.get(1, {})
        here = {o: v for o, v in a.items() if b.get(o) == v and image[0] <= v < image[1]}
        common = here if common is None else {o: v for o, v in common.items() if here.get(o) == v}
        snapshots += 1
    markers = sorted((common or {}).items())
    bound = None
    if markers:
        last, value = markers[-1]
        earlier = [o for o, v in markers[:-1] if v == value]
        bound = last + (last - earlier[-1] if earlier else 8)
    return {'snapshots': snapshots, 'markers': markers, 'lower_bound': bound}


def render_layout(path, layout, state_extent=STATE_EXTENT):
    lines = [f"{path}: {layout['snapshots']} raw snapshot(s)"]
    if not layout['snapshots']:
        lines.append('  no eye_pair_raw rows: capture with --raw-snapshots')
        return '\n'.join(lines)
    for offset, value in layout['markers']:
        lines.append(f"  +{offset:#06x} both slots {value:#x}")
    bound = layout['lower_bound']
    if bound is None:
        lines.append('  no shared image pointer: no layout bound from this trace')
    elif bound == state_extent:
        lines.append(f"  shared layout runs to at least +{bound:#06x}, matching the state extent in use")
    else:
        lines.append(f"  WARNING: shared layout runs to at least +{bound:#06x} but the state extent in use is "
                     f"+{state_extent:#06x}; a game update may have moved the layout, re-check before comparing")
    return '\n'.join(lines)


SETTINGS = {'VR_NativeStereoFixSamePass': 'Same Pass', 'VR_NativeStereoFixSwapEyes': 'Swap Eyes',
            'VR_WuWaEarlyStereoViews': 'Early stereo view setup'}
SHADOW_KEYS = ('early_configured', 'early_applied', 'early_skipped', 'applied', 'restored',
               'full_view_enabled', 'full_applied', 'faulted')


def _read_settings(folder):
    try:
        text = (folder / 'config.txt').read_text(encoding='utf-8-sig')
    except OSError:
        return {}
    values = dict(line.split('=', 1) for line in text.splitlines() if '=' in line)
    return {key: values[key].strip().lower() == 'true' for key in SETTINGS if key in values}


def _read_shadow(folder):
    try:
        shadow = json.loads((folder / 'capture.json').read_text(encoding='utf-8'))['backend']['shadow']
    except (OSError, ValueError, KeyError, TypeError):
        return None
    return {key: shadow.get(key) for key in SHADOW_KEYS}


def conditions(trace):
    """What was in effect during one trace, from the trace and the folders wuwa-test.py writes beside it.

    `settings` is the profile's saved config.txt, which can lag a live UEVR menu change; the live
    evidence is the backend's `shadow` counters (early_configured is set on every view construction)
    and the passes the pair rows read.

    `passes`: each slot's stereo pass per pair phase (WuWa: 2 primary, 3 secondary; 0 full view). With Early
    stereo view setup working, slot 1 reads 2 already before the submissions. `producers`: uniform rows per
    slot and producer relation ('external' is the extra per-frame production of fact 12). `display`: which
    half shows which slot, from Swap Eyes and D3D12Component (the game texture holds views[0], the scene
    capture views[1])."""
    trace = Path(trace)
    passes, producers = defaultdict(int), defaultdict(int)
    for line in trace.read_text(encoding='utf-8').splitlines():
        if '"pair"' not in line and '"uniforms"' not in line:
            continue
        row = json.loads(line)
        if row.get('type') == 'pair':
            key = (row.get('phase'), tuple(view.get('pass') for view in row.get('views', [])[:2]))
            passes[key] += 1
        elif row.get('type') == 'uniforms':
            context = row.get('producer_context')
            relation = context.get('source_relation') if isinstance(context, dict) else None
            producers[(row.get('eye_slot'), relation)] += 1
    settings = _read_settings(trace.parent / 'baseline')
    swap = settings.get('VR_NativeStereoFixSwapEyes')
    display = None if swap is None else (
        {'left': 'slot1 (views[1], scene capture)', 'right': 'slot0 (views[0], game target)'} if swap else
        {'left': 'slot0 (views[0], game target)', 'right': 'slot1 (views[1], scene capture)'})
    return {'settings': settings, 'display': display,
            'shadow': {stage: _read_shadow(trace.parent / stage) for stage in ('baseline', 'after')},
            'passes': [{'phase': phase, 'passes': list(key), 'rows': n}
                       for (phase, key), n in sorted(passes.items(), key=lambda item: str(item[0]))],
            'producers': [{'slot': slot, 'relation': relation, 'rows': n}
                          for (slot, relation), n in sorted(producers.items(), key=lambda item: str(item[0]))]}


def render_conditions(path, c):
    lines = [f"{path}:"]
    if c['settings']:
        lines.append('  saved settings (config.txt; can lag a live menu change): ' +
                     ', '.join(f"{SETTINGS[k]}={'on' if v else 'off'}" for k, v in c['settings'].items()))
    else:
        lines.append('  saved settings: unknown (no baseline/config.txt beside the trace)')
    if c['display']:
        lines.append(f"  display: left = {c['display']['left']}, right = {c['display']['right']}")
    for stage, shadow in c['shadow'].items():
        if shadow:
            lines.append(f"  {stage}: early configured={shadow['early_configured']} applied={shadow['early_applied']} "
                         f"skipped={shadow['early_skipped']}; same-pass applied={shadow['applied']}; "
                         f"full view={shadow['full_view_enabled']}; faulted={shadow['faulted']}")
    for item in c['passes']:
        lines.append(f"  pass slot0/slot1 {item['phase']}: {'/'.join(str(p) for p in item['passes'])} x{item['rows']}")
    if not c['passes']:
        lines.append('  no pair rows')
    lines.append('  uniform producers: ' + (', '.join(f"slot{p['slot']} {p['relation']} x{p['rows']}"
                                                       for p in c['producers']) or 'none (--view-uniforms not requested)'))
    return '\n'.join(lines)


def render_raw(path, region, rows):
    lines = [f"{path}: {len(rows)} raw snapshot(s), {region} region; slot 1 is views[1] (the scene-capture "
             "texture); which display half shows it depends on Swap Eyes (see --conditions); '--' = unreadable"]
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
    parser.add_argument('--state-extent', type=lambda text: int(text, 0), default=STATE_EXTENT, metavar='OFFSET',
                        help=f'view-state bytes to compare or read (default {STATE_EXTENT:#x}, an evidence-backed '
                             'lower bound; raise it only with a size read from the binary)')
    parser.add_argument('--layout', action='store_true',
                        help='list class pointers shared by both eye states in the raw rows (the extent evidence)')
    parser.add_argument('--conditions', action='store_true',
                        help='print settings, stereo passes, uniform producers and display halves for each trace')
    args = parser.parse_args(argv)
    if args.state_extent <= 0 or args.state_extent % 4:
        parser.error('--state-extent must be a positive multiple of 4')
    if args.layout or args.conditions:
        items = [(path, layout_markers(path) if args.layout else conditions(path)) for path in args.trace]
        if args.json:
            json.dump([{'trace': path, 'layout' if args.layout else 'conditions': item} for path, item in items],
                      sys.stdout, indent=1)
            sys.stdout.write('\n')
        else:
            print('\n\n'.join(render_layout(path, item, args.state_extent) if args.layout else
                                render_conditions(path, item) for path, item in items))
        return 0
    if args.raw:
        try:
            region, offsets = parse_raw_spec(args.raw)
        except ValueError as error:
            parser.error(str(error))
        offsets, dropped = split_extent(region, offsets, args.state_extent)
        tables = [(path, raw_values(path, region, offsets)) for path in args.trace]
        if args.json:
            json.dump([{'trace': path, 'rows': rows, 'not_read_beyond_extent': dropped} for path, rows in tables],
                      sys.stdout, indent=1)
            sys.stdout.write('\n')
        else:
            note = (f"\n{len(dropped)} requested offset(s) at or above the {_extent_note(args.state_extent)} were "
                    "not read; pass --state-extent to override" if dropped else '')
            print('\n\n'.join(render_raw(path, region, rows) for path, rows in tables) + note)
        return 0
    if args.compare != (len(args.trace) == 2) or len(args.trace) > 2:
        parser.error('--compare takes exactly two traces (failing, control); otherwise pass one trace')
    summaries = [summarize(*load(path), phase=args.phase, state_extent=args.state_extent) for path in args.trace]
    result = compare(*summaries) if args.compare else summaries[0]
    result['eye_sides'] = eye_sides(args.trace[0])
    if not args.compare:
        result['lifecycle'] = lifecycle(args.trace[0])
        result['conditions'] = conditions(args.trace[0])
    else:
        result['display'] = conditions(args.trace[0])['display']
    if args.json:
        json.dump(result, sys.stdout, indent=1)
        sys.stdout.write('\n')
    else:
        print(render_compare(result) if args.compare else render(result))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
