import contextlib
import copy
import unittest
from pathlib import Path
import wuwa_graphics as g


def snapshot():
    return {'graphics': {'schema': 1, 'settings_changed': False, 'values': {
        'sg.ShadowQuality': {'available': True, 'int': 3, 'float': 3.0},
        'r.ShadowQuality': {'available': True, 'int': 5, 'float': 5.0},
        'r.ReflectionMethod': {'available': False, 'error': 'not found'}}}}


class FakeClient:
    def __init__(self):
        self.pid = 123
        self.calls = []
        self.error = None
        self.response = snapshot()
        self.reads = 0
        self.restart = False
        self.busy = False
    def assert_live(self):
        self.reads += 1
        return {'pid': 456 if self.restart and self.reads > 1 else self.pid}
    @contextlib.contextmanager
    def exclusive(self):
        if self.busy: raise RuntimeError('Another reader owns the bridge')
        yield
    def request(self, op, **kwargs):
        self.calls.append((op, kwargs))
        if self.error: raise self.error
        return self.response


class GraphicsReadTests(unittest.TestCase):
    def run_reader(self, client):
        return g.read_live_graphics(Path('fixture-profile'), Path('unused'), client_factory=lambda **kw: client)
    def test_fixed_read_only_operation_and_unavailable_never_becomes_zero(self):
        client = FakeClient()
        result = self.run_reader(client)
        self.assertEqual(client.calls, [('graphics_snapshot', {'timeout': 8})])
        self.assertFalse(result['settings_changed'])
        self.assertEqual(result['pid'], 123)
        self.assertEqual(result['values']['r.ShadowQuality']['float'], 5)
        self.assertNotIn('float', result['values']['r.ReflectionMethod'])
    def test_rejects_malformed_or_non_readonly_payload(self):
        bad = [None, {}, {'graphics': {}}, {'graphics': {'schema': 1, 'settings_changed': True}}]
        for value in bad:
            with self.subTest(value=value), self.assertRaises(ValueError): g.validate_snapshot(value)
    def test_rejects_bad_names_sizes_and_numbers(self):
        for values in ({}, {f'r.test{i}': {'available': False} for i in range(49)},
                       {'r.ShadowQuality 0': {'available': False}},
                       {'r.test': {'available': True, 'int': 0, 'float': float('nan')}},
                       {'r.test': {'available': True, 'int': True, 'float': 0}},
                       {'r.test': {'int': 0, 'float': 0}}):
            reply = snapshot(); reply['graphics']['values'] = values
            with self.subTest(values=values), self.assertRaises(ValueError): g.validate_snapshot(reply)
    def test_old_backend_is_an_explicit_upgrade_message(self):
        client = FakeClient(); client.error = RuntimeError('Backend refused: unknown operation')
        with self.assertRaisesRegex(RuntimeError, 'updated graphics package'): self.run_reader(client)
        self.assertEqual(len(client.calls), 1)
    def test_timeout_and_lock_failure_never_fall_back_to_mutation(self):
        client = FakeClient(); client.error = TimeoutError('No response')
        with self.assertRaises(TimeoutError): self.run_reader(client)
        client = FakeClient(); client.busy = True
        with self.assertRaisesRegex(RuntimeError, 'Another reader'): self.run_reader(client)
        self.assertFalse(client.calls)
    def test_restarted_process_cannot_return_a_valid_snapshot(self):
        client = FakeClient(); client.restart = True
        with self.assertRaisesRegex(RuntimeError, 'process changed'): self.run_reader(client)


if __name__ == '__main__': unittest.main()
