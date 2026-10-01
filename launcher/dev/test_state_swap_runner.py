"""wuwa-test.py lod-inputs --state-swap against a fake backend. No game, headset or Windows API."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(file))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


live = module('wuwa_test_state_swap', 'wuwa-test.py')
PID = 4242


class Backend:
    """Just enough of the control op surface for lod_inputs: shadow status, state_swap, lod_probe."""

    def __init__(self, profile, supported=True, restore_lags=False, fail_stop=False):
        self.profile, self.supported = profile, supported
        self.restore_lags, self.fail_stop = restore_lags, fail_stop
        self.ops, self.swap_active, self.applied, self.restored = [], False, 7, 7
        self.trace = profile / 'diagnostics' / f'lod-{PID}-1.jsonl'

    def shadow(self):
        status = {'ready': True, 'faulted': False, 'test_active': False, 'state_swap_active': self.swap_active,
                  'state_swap_applied': self.applied, 'state_swap_restored': self.restored}
        if self.supported:
            status['state_swap_supported'] = True
        return status

    def frames(self):  # one game frame per wait: a swapped pair is applied and restored
        if self.swap_active:
            self.applied += 1
            self.restored += 0 if self.restore_lags else 1

    def request(self, op, **fields):
        self.ops.append((op, fields))
        if op == 'state_swap':
            self.swap_active = fields['seconds'] > 0
            return {'shadow': self.shadow()}
        if op == 'shadow_query':
            return {'shadow': self.shadow()}
        if op == 'lod_probe':
            if fields['seconds'] == 0:
                if self.fail_stop:
                    raise RuntimeError('backend stopped responding')
                self.trace.write_text('{"type":"header"}\n', encoding='utf-8')
                return {'lod_probe': {'active': False, 'path': str(self.trace), 'written': 1}}
            return {'lod_probe': {'active': True, 'path': str(self.trace)}}
        raise AssertionError(f'unexpected op {op}')


def client(backend):
    c = object.__new__(live.LiveTest)
    c.pid, c.profile, c.capture_source = PID, backend.profile, 'simulator'
    c.assert_live = lambda: {'lod_probe': {'active': False}, 'shadow': backend.shadow()}
    c.assert_focus = lambda: None
    c.capture = lambda output, layer='all': output.mkdir(parents=True)
    c.wait_frames = lambda seconds=2, lease=None: backend.frames()
    c.request = backend.request
    return c


class StateSwapRunner(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.profile = Path(self.tmp.name)
        (self.profile / 'diagnostics').mkdir()
        self.output = self.profile / 'out'

    def tearDown(self):
        self.tmp.cleanup()

    def report(self):
        return json.loads((self.output / 'lod-inputs.json').read_text(encoding='utf-8'))

    def test_swap_brackets_the_trace_and_is_verified(self):
        backend = Backend(self.profile)
        result = client(backend).lod_inputs(self.output, seconds=3, state_swap=True)
        ops = [(op, f.get('seconds')) for op, f in backend.ops]
        self.assertEqual(ops[0], ('state_swap', 18))                  # window longer than the trace
        start, stop = ops.index(('lod_probe', 3)), ops.index(('lod_probe', 0))
        self.assertLess(ops.index(('state_swap', 18)), start)          # every traced pair is swapped
        self.assertLess(stop, ops.index(('state_swap', 0)))            # ended only after the trace stopped
        self.assertFalse(backend.swap_active)
        self.assertEqual((result['read_only'], result['state_swap_requested']), (False, True))
        final = self.report()['state_swap_restored']
        self.assertEqual(final['state_swap_applied'], final['state_swap_restored'])

    def test_plain_trace_sends_no_swap_and_stays_read_only(self):
        backend = Backend(self.profile)
        result = client(backend).lod_inputs(self.output, seconds=3)
        self.assertNotIn('state_swap', [op for op, _ in backend.ops])
        self.assertEqual((result['read_only'], result['state_swap_requested']), (True, False))

    def test_unsupported_backend_is_refused_before_anything_starts(self):
        backend = Backend(self.profile, supported=False)
        with self.assertRaisesRegex(RuntimeError, 'does not support the view-state swap'):
            client(backend).lod_inputs(self.output, seconds=3, state_swap=True)
        self.assertEqual(backend.ops, [])
        self.assertFalse(self.output.exists())

    def test_unconfirmed_restore_fails_the_run(self):
        backend = Backend(self.profile, restore_lags=True)
        with self.assertRaisesRegex(RuntimeError, 'did not apply and restore cleanly'):
            client(backend).lod_inputs(self.output, seconds=3, state_swap=True)
        self.assertFalse(backend.swap_active)
        self.assertIn('did not apply and restore cleanly', self.report()['error'])

    def test_a_failure_mid_trace_still_ends_the_swap(self):
        backend = Backend(self.profile, fail_stop=True)
        with self.assertRaisesRegex(RuntimeError, 'stopped responding'):
            client(backend).lod_inputs(self.output, seconds=3, state_swap=True)
        self.assertEqual([f['seconds'] for op, f in backend.ops if op == 'state_swap'][-1], 0)
        self.assertFalse(backend.swap_active)
        self.assertIn('state_swap_cleanup', self.report())


class StateSwapWindow(unittest.TestCase):
    """`wuwa-test.py state-swap`: opens or ends the window and returns, freeing the lock for a recording."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.backend = Backend(Path(self.tmp.name))

    def tearDown(self):
        self.tmp.cleanup()

    def test_opens_and_ends(self):
        c = client(self.backend)
        self.assertTrue(c.state_swap_window(45)['state_swap_active'])
        self.assertFalse(c.state_swap_window(0)['state_swap_active'])
        self.assertEqual([f['seconds'] for _, f in self.backend.ops], [45, 0])

    def test_refuses_old_backend_busy_window_and_bad_durations(self):
        c = client(Backend(Path(self.tmp.name), supported=False))
        with self.assertRaisesRegex(RuntimeError, 'does not support'):
            c.state_swap_window(30)
        self.backend.swap_active = True
        with self.assertRaisesRegex(RuntimeError, 'not verified and idle'):
            client(self.backend).state_swap_window(30)
        for bad in (-1, 61):
            with self.assertRaises(ValueError):
                client(self.backend).state_swap_window(bad)
        self.assertEqual(self.backend.ops, [])


if __name__ == '__main__':
    unittest.main()
