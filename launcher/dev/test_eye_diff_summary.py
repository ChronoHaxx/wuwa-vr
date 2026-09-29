"""Offline checks for the eye-pair diff summarizer. No game, headset or native build is used."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(file))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


s = module('eye_diff_summary_test', 'wuwa_eye_diff_summary.py')
# Produced by the probe's own serializer (WuWaLodProbe.hpp eye_diff_json) compiled against
# nlohmann/json, so this test pins the field names and shapes the native side really writes.
NATIVE = Path(__file__).with_name('fixtures') / 'eye-pair-diff-native-sample.jsonl'


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def region(deltas, valid=True, compared=1936, unreadable=0, truncated=False):
    return {'begin': 0, 'end': 0x1e40, 'valid': valid, 'compared': compared, 'unreadable': unreadable,
            'differing': len(deltas), 'truncated': truncated, 'deltas': [list(d) for d in deltas]}


def sample(sequence, view=(), state=(), phase='before_submissions', tick=1000, **kwargs):
    return {'type': 'eye_pair_diff', 'sequence': sequence, 'phase': phase, 'tick_ms': tick,
            'view_region': region(view, **kwargs), 'state_region': region(state, compared=4096)}


def write(directory, rows):
    path = Path(directory) / 'trace.jsonl'
    path.write_text('\n'.join(json.dumps(r) for r in rows) + '\n')
    return path


class NativeShape(unittest.TestCase):
    def test_native_fixture_loads_and_classifies(self):
        samples, clock = s.load(NATIVE)
        self.assertEqual(len(samples), 4)
        self.assertEqual(clock, [])
        out = s.summarize(samples, clock)
        self.assertEqual((out['samples'], out['pairs'], out['phase']), (2, 2, 'before'))  # default: stable phase
        both = s.summarize(samples, clock, phase='both')
        self.assertEqual((both['samples'], both['pairs']), (4, 2))
        self.assertEqual(s.summarize(samples, clock, phase='after')['samples'], 2)
        view = {r['offset']: r for r in out['regions']['view_region']['differing_offsets']}
        # +0x8 is each eye's own view-state pointer, read through the real pair() path.
        self.assertEqual(sorted(view), [0x8, 0x2f8, 0x320, 0x324])
        self.assertEqual((view[0x8]['kind'], view[0x8]['label']), ('expected', 'view-state pointer'))
        self.assertEqual((view[0x2f8]['kind'], view[0x2f8]['label']), ('unexpected', 'view rect'))
        self.assertEqual(view[0x320]['kind'], 'geometry')
        self.assertTrue(all(r['seen'] == r['of'] == 2 for r in view.values()))
        state = {r['offset']: r for r in out['regions']['state_region']['differing_offsets']}
        self.assertEqual(sorted(state), [0x40, 0x1c0, 0x300])
        self.assertTrue(state[0x40]['pointer_low_half_candidate'])
        self.assertFalse(state[0x1c0]['pointer_low_half_candidate'])  # 1234.5 vs 1231.25 reads as a float
        self.assertFalse(state[0x1c0]['time_like'])  # no clock in this file, so never claimed
        self.assertTrue(state[0x300]['small_int'])
        self.assertEqual(out['warnings'], [])

    def test_time_like_needs_a_nearby_game_clock(self):
        with tempfile.TemporaryDirectory() as d:
            rows = [json.loads(line) for line in NATIVE.read_text().splitlines()]
            near = [{'type': 'uniforms', 'tick_ms': 100005, 'real_time': 1233.0, 'eye_slot': 0}]
            samples, clock = s.load(write(d, rows + near))
            self.assertEqual(clock, [(100005, 1233.0)])
            state = {r['offset']: r for r in s.summarize(samples, clock)['regions']['state_region']['differing_offsets']}
            self.assertTrue(state[0x1c0]['time_like'])          # 1234.5 vs 1231.25 around 1233
            self.assertFalse(state[0x300]['time_like'])
            far = [{'type': 'uniforms', 'tick_ms': 900000, 'real_time': 1233.0, 'eye_slot': 0}]
            samples, clock = s.load(write(d, rows + far))       # nearest clock sample is minutes away
            state = {r['offset']: r for r in s.summarize(samples, clock)['regions']['state_region']['differing_offsets']}
            self.assertFalse(state[0x1c0]['time_like'])


class Classification(unittest.TestCase):
    def test_offsets_outside_verified_extent_are_flagged_unverified(self):
        self.assertEqual(s._known(0x1004)[1], 'unverified')
        self.assertEqual(s._known(0x1e30)[1], 'unverified')
        self.assertEqual(s._known(0x100)[1], 'unclassified')
        self.assertEqual(s._known(0xffe)[1], 'unexpected')  # instanced flag: a difference is itself a finding
        self.assertEqual(s._known(0x1000)[1], 'unexpected')  # multiview flag
        self.assertEqual(s._known(0x8)[1], 'expected')

    def test_time_like_rejects_equal_nonfinite_and_far_values(self):
        a, b = bits(1234.5), bits(1231.25)
        self.assertTrue(s._is_time_like(a, b, 1233.0))
        self.assertFalse(s._is_time_like(a, a, 1233.0))               # equal is not a difference
        self.assertFalse(s._is_time_like(a, b, None))
        self.assertFalse(s._is_time_like(a, b, 9999.0))               # not near the clock
        self.assertFalse(s._is_time_like(0x7f800000, b, 1233.0))      # infinity
        self.assertFalse(s._is_time_like(0x7fc00000, b, 1233.0))      # NaN


class PhaseFilter(unittest.TestCase):
    def test_after_phase_records_are_ignored_by_default(self):
        rows = [sample(1, view=[(0x140, 1, 2)], state=[(0x1c0, 5, 6)]),
                sample(1, view=[(0x140, 1, 2), (0x180, 3, 4)], state=[(0x1c0, 5, 7)], phase='after_submissions')]
        default = s.summarize(rows)
        self.assertEqual(default['samples'], 1)
        self.assertEqual([r['offset'] for r in default['regions']['view_region']['differing_offsets']], [0x140])
        wide = s.summarize(rows, phase='both')
        self.assertEqual([r['offset'] for r in wide['regions']['view_region']['differing_offsets']], [0x140, 0x180])
        self.assertEqual(wide['regions']['view_region']['differing_offsets'][0]['of'], 2)


class Coverage(unittest.TestCase):
    def test_missing_records_invalid_regions_unreadable_and_truncation_warn(self):
        self.assertIn('no eye_pair_diff records', ' '.join(s.summarize([])['warnings']).replace("'before'-phase ", ''))
        bad = s.summarize([sample(1, valid=False)])
        self.assertIn('view_region: no valid comparison', ' '.join(bad['warnings']))
        self.assertEqual(bad['regions']['view_region']['differing_offsets'], [])
        damaged = s.summarize([sample(1, view=[(0x100, 1, 2)], unreadable=7, truncated=True)])
        text = ' '.join(damaged['warnings'])
        self.assertIn('7 unreadable dword', text)
        self.assertIn('truncated', text)

    def test_load_ignores_blank_and_other_record_types(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / 't.jsonl'
            path.write_text('\n{"type":"header"}\n\n{"type":"pair","phase":"before_submissions"}\n'
                            + json.dumps(sample(1)) + '\n')
            samples, clock = s.load(path)
            self.assertEqual((len(samples), clock), (1, []))


class Comparison(unittest.TestCase):
    def build(self, failing, control):
        f = s.summarize([sample(i, view=v, state=st) for i, (v, st) in enumerate(failing, 1)])
        c = s.summarize([sample(i, view=v, state=st) for i, (v, st) in enumerate(control, 1)])
        return s.compare(f, c)

    def test_difference_only_while_failing_is_isolated(self):
        marker = [(0x140, 1, 2)]      # unclassified view dword
        both = [(0x320, 5, 6)]        # ordinary eye geometry, present in both captures
        result = self.build([(marker + both, [(0x1c0, bits(2.0), bits(3.0))])] * 5,
                            [(both, [])] * 5)
        view = result['regions']['view_region']
        self.assertEqual([r['offset'] for r in view['differs_only_while_failing']], [0x140])
        self.assertEqual(view['differs_only_in_control'], [])
        state = result['regions']['state_region']
        self.assertEqual([r['offset'] for r in state['differs_only_while_failing']], [0x1c0])

    def test_reverse_direction_and_intermittent_offsets(self):
        # 0x140 differs in 2 of 5 failing samples only: intermittent is neither side.
        failing = [([(0x140, 1, 2)], [])] * 2 + [([], [])] * 3
        control = [([(0x180, 3, 4)], [])] * 5
        result = self.build(failing, control)['regions']['view_region']
        self.assertEqual(result['differs_only_while_failing'], [])
        self.assertEqual([r['offset'] for r in result['differs_only_in_control']], [0x180])

    def test_identical_captures_report_nothing(self):
        same = [([(0x140, 1, 2)], [(0x200, 7, 8)])] * 4
        result = self.build(same, same)
        for region in result['regions'].values():
            self.assertEqual((region['differs_only_while_failing'], region['differs_only_in_control']), ([], []))


class CommandLine(unittest.TestCase):
    def run_main(self, *argv):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = s.main(list(argv))
        return code, out.getvalue()

    def test_single_trace_text_and_json(self):
        code, text = self.run_main(str(NATIVE))
        self.assertEqual(code, 0)
        self.assertIn('2 before-phase samples from 2 pairs', text)
        code, both = self.run_main('--phase', 'both', str(NATIVE))
        self.assertIn('4 both-phase samples from 2 pairs', both)
        self.assertIn('+0x02f8', text)
        self.assertIn('view rect', text)
        self.assertNotIn('+0x0320', text)  # ordinary eye geometry is summarised, not listed
        code, blob = self.run_main('--json', str(NATIVE))
        self.assertEqual(json.loads(blob)['samples'], 2)

    def test_compare_requires_exactly_two_traces(self):
        for argv in (['--compare', str(NATIVE)], [str(NATIVE), str(NATIVE)]):
            with self.assertRaises(SystemExit) as raised, contextlib.redirect_stderr(io.StringIO()):
                s.main(argv)
            self.assertEqual(raised.exception.code, 2)
        code, text = self.run_main('--compare', str(NATIVE), str(NATIVE))
        self.assertEqual(code, 0)
        self.assertIn('differs only while failing: 0', text)


def state_sample(sequence, state, truncated=False):
    row = sample(sequence)
    row['state_region'] = dict(region(state, compared=4096, truncated=truncated), end=0x4000)
    return row


class Truncation(unittest.TestCase):
    """A truncated region keeps its lowest deltas; above the last one, absence means unknown."""

    def test_fractions_count_only_samples_that_know_the_offset(self):
        far = [state_sample(1, [(0x10, 1, 2), (0x100, 3, 4)], truncated=True),
               state_sample(2, [(0x10, 1, 2), (0x200, 5, 6)])]
        out = s.summarize(far)
        region = out['regions']['state_region']
        self.assertEqual(region['known_until'], 0x100)
        rows = {r['offset']: r for r in region['differing_offsets']}
        self.assertEqual((rows[0x200]['seen'], rows[0x200]['of'], rows[0x200]['fraction']), (1, 1, 1.0))
        self.assertEqual((rows[0x100]['seen'], rows[0x100]['of']), (1, 2))
        self.assertIn('offsets above 0x100 are unknown', ' '.join(out['warnings']))

    def test_compare_never_reads_unknown_as_equal(self):
        # 0x200 differs in the control, but one failing sample stopped recording at 0x100:
        # its absence there is unknown, so it must not be reported as "control only".
        failing = s.summarize([state_sample(1, [(0x10, 1, 2), (0x100, 3, 4)], truncated=True)])
        control = s.summarize([state_sample(2, [(0x80, 7, 8), (0x200, 5, 6)])])
        region = s.compare(failing, control)['regions']['state_region']
        self.assertEqual(region['known_until'], 0x100)
        self.assertEqual([r['offset'] for r in region['differs_only_in_control']], [0x80])
        self.assertEqual([r['offset'] for r in region['differs_only_while_failing']], [0x10, 0x100])


class Remnants(unittest.TestCase):
    def test_static_and_text_bytes_are_labelled_not_hidden(self):
        text = int.from_bytes(b'.Pla', 'little')
        rows = [state_sample(n, [(0x40, 2, 0), (0x44, 0xffffffff, text), (0x48, n, n + 1)]) for n in (1, 2, 3)]
        by = {r['offset']: r for r in s.summarize(rows)['regions']['state_region']['differing_offsets']}
        self.assertTrue(by[0x40]['static'] and not by[0x40]['text_like'])
        self.assertTrue(by[0x44]['static'] and by[0x44]['text_like'])
        self.assertFalse(by[0x48]['static'])  # rewritten every sample: live
        self.assertFalse(s._text_like(bits(0.9)))   # 0x3f666666 is printable ("fff?") but a float
        self.assertTrue(s._text_like(int.from_bytes(b' dom', 'little')))
        self.assertEqual(s._describe(by[0x44]), 'unchanged in every sample, text bytes')


class EyeSides(unittest.TestCase):
    def test_slots_follow_projection_off_centre(self):
        def view(m20):
            projection = [0.9, 0, 0, 0, 0, 0.88, 0, 0, m20, 0.2, 0, 1, 0, 0, 10, 0]
            return {'projection': projection}
        rows = [{'type': 'pair', 'phase': 'before_submissions', 'views': [view(-0.2448), view(0.2448)]},
                {'type': 'pair', 'phase': 'after_submissions', 'views': [view(0.5), view(-0.5)]}]
        with tempfile.TemporaryDirectory() as directory:
            path = write(directory, rows)
            self.assertEqual(s.eye_sides(path), {'slot0': 'right', 'slot1': 'left'})
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                s.main([str(path)])
            self.assertIn('eye slots (provisional, from projection off-centre; not verified against output): 0 = right, 1 = left', out.getvalue())
        self.assertIsNone(s.eye_sides(NATIVE))  # no pair rows: no claim


def raw_row(sequence, phase, state0, state1, unreadable1=()):
    def block(words, unreadable=()):
        raw = b''.join(w.to_bytes(4, 'little') for w in words)
        return {'address': 1, 'begin': 0, 'end': len(raw), 'readable_dwords': len(words),
                'unreadable': [list(r) for r in unreadable], 'bytes_hex': raw.hex()}
    return {'type': 'eye_pair_raw', 'sequence': sequence, 'phase': phase, 'raw_ordinal': 0, 'tick_ms': 1,
            'slots': [{'slot': 0, 'view': block([0]), 'state': block(state0)},
                      {'slot': 1, 'view': block([0]), 'state': block(state1, unreadable1)}]}


class Raw(unittest.TestCase):
    """Absolute values from raw rows: the one thing the diff format cannot show is equality."""

    def test_equal_values_and_unreadable_dwords(self):
        words0 = [0] * 0x160; words1 = [0] * 0x160
        words0[0x550 // 4] = words1[0x550 // 4] = 2      # equal: invisible to eye_pair_diff
        words0[0x554 // 4], words1[0x554 // 4] = 1, 0
        rows = [raw_row(1, 'before_submissions', words0, words1, unreadable1=[(0x558, 0x560)])]
        with tempfile.TemporaryDirectory() as directory:
            path = write(directory, [{'type': 'header'}] + rows)
            region, offsets = s.parse_raw_spec('state:0x550-0x560')
            values = s.raw_values(path, region, offsets)[0]['values']
            self.assertEqual([(v['slot0'], v['slot1'], v['equal']) for v in values],
                             [(2, 2, True), (1, 0, False), (0, None, False), (0, None, False)])
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(s.main(['--raw', 'state:0x550,0x558', str(path), str(path)]), 0)
            text = out.getvalue()
            self.assertIn('+0x0550 slot0=0x00000002 slot1=0x00000002  equal', text)
            self.assertIn('+0x0558 slot0=0x00000000 slot1=--', text)
            self.assertIn('slot 1 is views[1]', text)
            self.assertEqual(text.count('raw snapshot(s)'), 2)  # one table per trace

    def test_trace_without_raw_rows_says_so(self):
        region, offsets = s.parse_raw_spec('view:0x8')
        self.assertEqual(s.raw_values(NATIVE, region, offsets), [])
        self.assertIn('not requested, or the DLL predates them', s.render_raw('x', region, []))

    def test_bad_specs_are_refused(self):
        for spec in ('heap:0x10', 'state:', 'state:0x552'):
            with self.assertRaises(ValueError):
                s.parse_raw_spec(spec)


TEXT = int.from_bytes(b't")}', 'little')   # script-text bytes, as slot 1 holds in the 29 Sep captures


class Extent(unittest.TestCase):
    """The view-state compare stops at the state extent: past the known layout a difference is not eye state."""

    def captures(self):
        failing = [state_sample(n, [(0x0e24, 0, 3), (0x2070, 0xbe0, 0xbd0), (0x2074, 0, TEXT),
                                    (0x2100, 1, 2), (0x3000, TEXT, 0)]) for n in (1, 2)]
        control = [state_sample(n, [(0x2074, 0, TEXT), (0x2800, 5, 6)]) for n in (1, 2)]
        return failing, control

    def test_compare_stops_at_the_state_extent_by_default(self):
        failing, control = self.captures()
        result = s.compare(s.summarize(failing), s.summarize(control))
        region = result['regions']['state_region']
        self.assertEqual(s.STATE_EXTENT, 0x20a8)
        self.assertEqual(region['known_until'], 0x20a4)
        self.assertEqual(region['extent'], 0x20a8)
        # Inside the shared layout: still reported. 0x2074 differs in both captures: not failing-only.
        self.assertEqual([r['offset'] for r in region['differs_only_while_failing']], [0x0e24, 0x2070])
        self.assertEqual(region['differs_only_in_control'], [])
        self.assertEqual(region['beyond_extent'], [2, 1])  # counted, never compared
        text = s.render_compare(result)
        self.assertIn('compared up to +0x20a4', text)
        self.assertIn('not compared at or above it (failing 2, control 1 differing offsets there)', text)
        self.assertNotIn('+0x2100', text)
        self.assertNotIn('+0x3000', text)

    def test_a_larger_extent_from_the_binary_compares_further(self):
        failing, control = self.captures()
        result = s.compare(s.summarize(failing, state_extent=0x4000), s.summarize(control, state_extent=0x4000))
        region = result['regions']['state_region']
        self.assertEqual([r['offset'] for r in region['differs_only_while_failing']], [0x0e24, 0x2070, 0x2100, 0x3000])
        self.assertEqual([r['offset'] for r in region['differs_only_in_control']], [0x2800])
        self.assertIn('set with --state-extent', s.render_compare(result))

    def test_view_region_is_not_cut_by_the_state_extent(self):
        out = s.summarize([sample(1, view=[(0x1e00, 1, 2)])])
        view = out['regions']['view_region']
        self.assertEqual(([r['offset'] for r in view['differing_offsets']], view['extent']), ([0x1e00], None))

    def test_truncation_above_the_extent_is_not_a_gap(self):
        # 1,536 kept deltas that end at +0x2800: everything below the extent was recorded.
        out = s.summarize([state_sample(1, [(0x10, 1, 2), (0x2800, 3, 4)], truncated=True)])
        region = out['regions']['state_region']
        self.assertEqual((region['known_until'], region['truncated_samples']), (0x20a4, 0))
        self.assertFalse([w for w in out['warnings'] if 'truncated' in w])

    def test_command_line_extent(self):
        failing, control = self.captures()
        with tempfile.TemporaryDirectory() as directory:
            a = Path(directory) / 'a'; b = Path(directory) / 'b'
            a.mkdir(); b.mkdir()
            paths = [str(write(a, failing)), str(write(b, control))]
            for argv, present, absent in ((['--compare'], '+0x2070', '+0x3000'),
                                          (['--compare', '--state-extent', '0x4000'], '+0x3000', None)):
                out = io.StringIO()
                with contextlib.redirect_stdout(out):
                    self.assertEqual(s.main(argv + paths), 0)
                self.assertIn(present, out.getvalue())
                if absent:
                    self.assertNotIn(absent, out.getvalue())
            for bad in ('0x2001', '0', 'x'):
                with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
                    s.main(['--compare', '--state-extent', bad] + paths)


def layout_words(marker_at_0x2080=True):
    words0, words1 = [0] * (0x2100 // 4), [0] * (0x2100 // 4)
    def qword(words, offset, value):
        words[offset // 4], words[offset // 4 + 1] = value & 0xffffffff, value >> 32
    for words, heap in ((words0, 0x1a8d831a80), (words1, 0x1a4591d9c0)):
        qword(words, 0x0, 0x1677a0b40)               # class pointer in the game image
        qword(words, 0x8, heap)                      # each object's own heap pointer: not a marker
        qword(words, 0x2058, 0x16755f188)
        qword(words, 0x2060, heap + 0x40)
        qword(words, 0x2070, 0xbe0)
    qword(words0, 0x2080, 0x16755f188)
    qword(words1, 0x2080, 0x16755f188 if marker_at_0x2080 else 0)
    words1[0x2074 // 4] = TEXT                       # padding half: not a boundary
    return words0, words1


class Layout(unittest.TestCase):
    def test_shared_class_pointers_bound_the_layout(self):
        rows = [raw_row(n, 'before_submissions', *layout_words()) for n in (1, 301)]
        with tempfile.TemporaryDirectory() as directory:
            path = write(directory, rows)
            layout = s.layout_markers(path)
            self.assertEqual(layout['snapshots'], 2)
            self.assertEqual([o for o, _ in layout['markers']], [0x0, 0x2058, 0x2080])
            self.assertEqual(layout['lower_bound'], 0x20a8)   # repeated member: last marker + 0x28 stride
            self.assertIn('matching the state extent in use', s.render_layout(path, layout))
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(s.main(['--layout', str(path)]), 0)
            self.assertIn('+0x2080 both slots 0x16755f188', out.getvalue())

    def test_a_marker_missing_from_one_snapshot_is_not_shared(self):
        rows = [raw_row(1, 'before_submissions', *layout_words()),
                raw_row(301, 'before_submissions', *layout_words(marker_at_0x2080=False))]
        with tempfile.TemporaryDirectory() as directory:
            path = write(directory, rows)
            layout = s.layout_markers(path)
            self.assertEqual(([o for o, _ in layout['markers']], layout['lower_bound']), ([0x0, 0x2058], 0x2060))
            self.assertIn('WARNING: shared layout runs to at least +0x2060', s.render_layout(path, layout))
        self.assertEqual(s.layout_markers(NATIVE), {'snapshots': 0, 'markers': [], 'lower_bound': None})

    def test_raw_reads_stop_at_the_extent(self):
        words0, words1 = layout_words()
        with tempfile.TemporaryDirectory() as directory:
            path = str(write(directory, [raw_row(1, 'before_submissions', words0, words1)]))
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                s.main(['--raw', 'state:0x20a0-0x20b0', path])
            text = out.getvalue()
            self.assertIn('+0x20a4 slot0=', text)
            self.assertNotIn('+0x20a8 slot0=', text)
            self.assertIn('2 requested offset(s) at or above the state extent +0x20a8', text)
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                s.main(['--raw', 'state:0x20a0-0x20b0', '--state-extent', '0x2100', path])
            self.assertIn('+0x20ac slot0=', out.getvalue())
            self.assertNotIn('not read', out.getvalue())
        self.assertEqual(s.split_extent('view', [0x2100]), ([0x2100], []))  # the view window is separate


class Conditions(unittest.TestCase):
    def make(self, directory, swap, early, passes, states=(0x3cd8aac0, 0x43ee8020), state_swap=None):
        root = Path(directory)
        (root / 'baseline').mkdir()
        (root / 'baseline' / 'config.txt').write_text(
            f'VR_NativeStereoFixSamePass=true\nVR_NativeStereoFixSwapEyes={str(swap).lower()}\n'
            f'VR_WuWaEarlyStereoViews={str(early).lower()}\nOther=1\n', encoding='utf-8')
        shadow = {'early_configured': early, 'early_applied': 40 if early else 0, 'early_skipped': 0,
                  'applied': 0 if early else 900, 'restored': 0 if early else 900,
                  'full_view_enabled': False, 'full_applied': 0, 'faulted': False}
        (root / 'baseline' / 'capture.json').write_text(json.dumps({'backend': {'shadow': shadow}}), encoding='utf-8')
        rows = [{'type': 'pair', 'phase': 'before_submissions',
                 'views': [{'pass': passes[0], 'state': states[0]}, {'pass': passes[1], 'state': states[1]}]}] * 3
        if state_swap is not None:
            (root / 'lod-inputs.json').write_text(json.dumps(state_swap), encoding='utf-8')
        rows += [{'type': 'uniforms', 'eye_slot': 0, 'producer_context': {'source_relation': 'external'}}] * 2
        rows += [{'type': 'uniforms', 'eye_slot': 1, 'producer_context': {'source_relation': 'view_plus_0x320'}}]
        return write(root, rows)

    def test_settings_passes_producers_and_display_halves(self):
        with tempfile.TemporaryDirectory() as directory:
            c = s.conditions(self.make(directory, swap=True, early=False, passes=(2, 3)))
            self.assertEqual(c['settings'], {'VR_NativeStereoFixSamePass': True, 'VR_NativeStereoFixSwapEyes': True,
                                             'VR_WuWaEarlyStereoViews': False})
            self.assertEqual(c['display']['left'], 'slot1 (views[1], scene capture)')
            self.assertEqual(c['passes'], [{'phase': 'before_submissions', 'passes': [2, 3], 'rows': 3}])
            self.assertEqual(c['producers'], [{'slot': 0, 'relation': 'external', 'rows': 2},
                                              {'slot': 1, 'relation': 'view_plus_0x320', 'rows': 1}])
            self.assertIsNone(c['shadow']['after'])
        with tempfile.TemporaryDirectory() as directory:
            path = self.make(directory, swap=False, early=True, passes=(2, 2))
            c = s.conditions(path)
            self.assertEqual(c['display']['left'], 'slot0 (views[0], game target)')
            text = s.render_conditions(path, c)
            self.assertIn('Early stereo view setup=on', text)
            self.assertIn('early configured=True applied=40', text)
            self.assertIn('pass slot0/slot1 before_submissions: 2/2 x3', text)
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(s.main(['--conditions', str(path), str(path)]), 0)
            self.assertEqual(out.getvalue().count('uniform producers: slot0 external x2'), 2)

    def test_state_swap_run_shows_exchanged_states_and_its_counters(self):
        report = {'state_swap_requested': True, 'state_swap_begin': {'state_swap_applied': 7},
                  'state_swap_restored': {'state_swap_applied': 1807, 'state_swap_restored': 1807, 'faulted': False}}
        with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
            plain = self.make(a, swap=True, early=False, passes=(2, 3), state_swap={'state_swap_requested': False})
            swapped = self.make(b, swap=True, early=False, passes=(2, 3), states=(0x43ee8020, 0x3cd8aac0),
                                state_swap=report)
            c = s.conditions(swapped)
            self.assertEqual(c['states'], [{'slot': 0, 'state': 0x43ee8020, 'rows': 3},
                                           {'slot': 1, 'state': 0x3cd8aac0, 'rows': 3}])
            self.assertEqual((c['swap']['applied_before'], c['swap']['applied_after'], c['swap']['restored_after']),
                             (7, 1807, 1807))
            text = s.render_conditions(swapped, c)
            self.assertIn('state swap: requested; applied 7 -> 1807, restored 1807; faulted=False', text)
            self.assertIn('state slot0: 0x43ee8020 x3', text)
            self.assertEqual(s.conditions(plain)['swap'], {'requested': False})
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(s.main(['--conditions', str(plain), str(swapped)]), 0)
            self.assertIn(f'states exchanged: {plain} carries the states of {swapped} the other way round',
                          out.getvalue())
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                s.main(['--conditions', str(plain), str(plain)])
            self.assertNotIn('states exchanged', out.getvalue())   # same assignment: nothing to report

    def test_a_bare_trace_claims_nothing(self):
        c = s.conditions(NATIVE)
        self.assertEqual((c['settings'], c['display'], c['passes'], c['states'], c['swap']), ({}, None, [], [], None))
        self.assertIn('settings: unknown', s.render_conditions('x', c))


def pair_counts(calls, rows, lock_misses=0, invalid=0):
    return {'calls': calls, 'rows': rows, 'lock_misses': lock_misses, 'invalid': invalid}


class Lifecycle(unittest.TestCase):
    """The status wuwa-test.py saves beside a trace explains an empty or thin one."""

    def run_with_inputs(self, probe):
        with tempfile.TemporaryDirectory() as directory:
            trace = write(directory, [{'type': 'header'}])
            if probe is not None:
                (Path(directory) / 'lod-inputs.json').write_text(json.dumps({'end': {'lod_probe': probe}}))
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                s.main([str(trace)])
            return s.lifecycle(trace), out.getvalue()

    def test_no_inputs_file_adds_nothing(self):
        life, text = self.run_with_inputs(None)
        self.assertIsNone(life)
        self.assertNotIn('LIFECYCLE', text)

    def test_merged_pr1_status_cannot_explain_an_empty_trace(self):
        # Shape of the 28 Sep far capture's end status: zero written, no per-phase counters.
        life, text = self.run_with_inputs({'eye_pair_diff': {'written': 0, 'capacity': 128, 'dropped': 0},
                                           'lock_misses': 478, 'read_failures': 68})
        self.assertIn('predates the sampling repair', life['warnings'][0])
        self.assertIn('LIFECYCLE: status has no per-phase pair counters', text)

    def test_starved_sampler_and_lost_after_snapshots_are_named(self):
        life, text = self.run_with_inputs({
            'eye_pair_diff': {'before_seen': 450, 'before_taken': 0, 'after_taken': 0, 'orphaned': 0, 'ring_full': 0},
            'pairs': {'before_submissions': pair_counts(450, 60),
                      'after_first_submission': pair_counts(450, 60),
                      'after_submissions': pair_counts(12, 2, lock_misses=438)}})
        self.assertIn('none was scheduled', ' '.join(life['warnings']))
        self.assertIn('after_submissions: 438 lock miss(es), 0 invalid snapshot(s) of 12 calls', text)
        self.assertIn('sampler: 450 pairs seen, 0 scheduled, 0 completed', text)

    def test_healthy_status_has_no_warning(self):
        life, text = self.run_with_inputs({
            'eye_pair_diff': {'before_seen': 475, 'before_taken': 8, 'after_taken': 8, 'orphaned': 0, 'ring_full': 0},
            'pairs': {'before_submissions': pair_counts(475, 64), 'after_first_submission': pair_counts(475, 64),
                      'after_submissions': pair_counts(475, 64)}})
        self.assertEqual(life['warnings'], [])
        self.assertIn('8 scheduled, 8 completed, 0 orphaned', text)
        self.assertNotIn('LIFECYCLE', text)


if __name__ == '__main__':
    unittest.main()
