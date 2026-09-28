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
