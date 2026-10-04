"""Console comparison readback/cleanup regressions; no game or Windows API calls."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import Mock


spec = importlib.util.spec_from_file_location("wuwa_test_console", Path(__file__).with_name("wuwa-test.py"))
live = importlib.util.module_from_spec(spec)
spec.loader.exec_module(live)


def sample(value, integer=None):
    return {"float": value, "int": int(value) if integer is None else integer}


def begin(value, integer=None):
    return {"after_float": value, "after_int": int(value) if integer is None else integer}


class ConsoleRunner(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.output = Path(self.tmp.name) / "comparison"
        self.captures = []
        self.client = object.__new__(live.LiveTest)
        self.client.pid, self.client.capture_source = 4242, "simulator"
        self.client.wait_frames = Mock()
        self.client.capture = self.capture
        self.client.request = Mock()

    def tearDown(self):
        self.tmp.cleanup()

    def capture(self, output, layer):
        self.captures.append(output.name)
        output.mkdir()

    def run_console(self, responses, value=0):
        self.client.request.side_effect = responses
        return self.client.console("r.Kuro.ToonOutlineDrawDistancePc", value, self.output)

    def report(self):
        return json.loads((self.output / "comparison.json").read_text(encoding="utf-8"))

    def replies(self, value=0, baseline=4000):
        return [sample(baseline), begin(value), sample(value), sample(value),
                {"restore": "restored"}, sample(baseline), sample(baseline), sample(baseline)]

    def assert_cleanup(self):
        requests = self.client.request.call_args_list
        stops = [i for i, call in enumerate(requests)
                 if call.args == ("console_set",) and call.kwargs.get("seconds") == 0]
        self.assertEqual(len(stops), 1)
        self.assertEqual(requests[stops[0] + 1].args, ("console_get",))

    def test_integer_success_has_verified_bracketed_captures_and_restore(self):
        report = self.run_console(self.replies())
        self.assertEqual(report["status"], "captured_and_restored_visual_review_pending")
        for key in ("applied_verified", "changed_capture_valid", "restoration_verified", "restored_capture_valid"):
            self.assertTrue(report[key], key)
        self.assertEqual(self.captures, ["baseline", "changed", "restored"])
        self.assert_cleanup()
        self.assertEqual(self.report(), report)
        start = self.client.request.call_args_list[1]
        self.assertEqual(start.kwargs["seconds"], 30)
        self.assertEqual(start.kwargs["value"], 0.0)

    def test_unchanged_live_bug_rejects_capture_but_restores_lease(self):
        with self.assertRaisesRegex(RuntimeError, "after_float readback differs"):
            self.run_console([sample(4000), begin(4000), {"restore": "restored"}, sample(4000)])
        report = self.report()
        self.assertEqual(report["status"], "failed")
        self.assertFalse(report["applied_verified"])
        self.assertFalse(report["changed_capture_valid"])
        self.assertTrue(report["restoration_verified"])
        self.assertEqual(self.captures, ["baseline"])
        self.assert_cleanup()

    def test_fractional_request_accepts_float32_rounding_and_int_truncation(self):
        rounded = struct.unpack("f", struct.pack("f", 0.1))[0]
        report = self.run_console(self.replies(rounded, baseline=1.25), value=0.1)
        self.assertTrue(report["changed_capture_valid"])
        self.assertEqual(report["restored_value"], sample(1.25))

    def test_fractional_request_rejects_integer_storage_truncation(self):
        with self.assertRaisesRegex(RuntimeError, "after_float readback differs"):
            self.run_console([sample(4), begin(0), {"restore": "restored"}, sample(4)], value=0.5)
        self.assertFalse(self.report()["changed_capture_valid"])
        self.assert_cleanup()

    def test_invalid_requested_values_have_no_side_effects(self):
        for value in (float("nan"), float("inf"), -float("inf"), True, "0", None, 100001, 10 ** 1000):
            with self.subTest(value=repr(value)), self.assertRaises(ValueError):
                self.client.console("r.Test", value, self.output)
        self.client.request.assert_not_called()
        self.assertFalse(self.output.exists())

    def test_zero_requires_zero_not_a_broad_absolute_epsilon(self):
        with self.assertRaisesRegex(RuntimeError, "after_float readback differs"):
            self.run_console([sample(4), begin(1e-8), {"restore": "restored"}, sample(4)])
        self.assert_cleanup()

    def test_integral_request_requires_matching_integer_getter(self):
        with self.assertRaisesRegex(RuntimeError, "after_int readback differs"):
            self.run_console([sample(4000), begin(0, integer=4000), {"restore": "restored"}, sample(4000)])
        self.assertFalse(self.report()["applied_verified"])
        self.assert_cleanup()

    def test_missing_nonfinite_or_boolean_begin_readback_is_rejected(self):
        for value in (None, float("nan"), float("inf"), True):
            with self.subTest(value=value):
                self.output = Path(self.tmp.name) / str(len(self.client.request.call_args_list))
                with self.assertRaisesRegex(RuntimeError, "unavailable or nonfinite"):
                    self.run_console([sample(4000), {"after_float": value}, {"restore": "restored"}, sample(4000)])
                self.assertFalse(self.report()["applied_verified"])
                self.assertTrue(self.report()["restoration_verified"])

    def test_restore_acknowledgement_must_confirm_even_when_value_matches(self):
        for status in ("restore_conflict_value_changed", "restore_failed", "inactive", None):
            with self.subTest(status=status):
                self.output = Path(self.tmp.name) / str(len(self.client.request.call_args_list))
                replies = self.replies()[:6]
                replies[4] = {"restore": status}
                with self.assertRaisesRegex(RuntimeError, "restore was not confirmed"):
                    self.run_console(replies)
                report = self.report()
                self.assertFalse(report["restoration_verified"])
                self.assertFalse(report["restored_capture_valid"])
                self.assertEqual(report["status"], "failed")
                self.assertEqual(report["restored_value"], sample(4000))

    def test_restore_value_must_match_both_baseline_getters(self):
        for restored in (sample(0), sample(4000, integer=0)):
            with self.subTest(restored=restored):
                self.output = Path(self.tmp.name) / str(len(self.client.request.call_args_list))
                replies = self.replies()[:6]
                replies[5] = restored
                with self.assertRaisesRegex(RuntimeError, "readback differs"):
                    self.run_console(replies)
                self.assertFalse(self.report()["restoration_verified"])
                self.assertFalse(self.report()["restored_capture_valid"])

    def test_lost_begin_response_still_restores_and_reads_back(self):
        with self.assertRaisesRegex(TimeoutError, "begin response lost"):
            self.run_console([sample(4000), TimeoutError("begin response lost"),
                              {"restore": "restored"}, sample(4000)])
        self.assert_cleanup()
        self.assertTrue(self.report()["restoration_verified"])
        self.assertEqual(self.captures, ["baseline"])

    def test_primary_and_cleanup_failures_are_both_retained(self):
        with self.assertRaisesRegex(TimeoutError, "begin response lost"):
            self.run_console([sample(4000), TimeoutError("begin response lost"),
                              TimeoutError("restore response lost"), sample(0)])
        report = self.report()
        self.assertEqual(report["error"], "begin response lost")
        self.assertEqual(len(report["restore_errors"]), 2)
        self.assertFalse(report["restoration_verified"])
        self.assert_cleanup()

    def test_capture_failure_still_cleans_up(self):
        def capture(output, layer):
            if output.name == "changed":
                raise RuntimeError("capture failed")
            self.capture(output, layer)
        self.client.capture = capture
        with self.assertRaisesRegex(RuntimeError, "capture failed"):
            self.run_console([sample(4000), begin(0), sample(0), {"restore": "restored"}, sample(4000)])
        self.assert_cleanup()
        self.assertFalse(self.report()["changed_capture_valid"])
        self.assertTrue(self.report()["restoration_verified"])

    def test_change_before_or_during_changed_capture_is_not_valid_evidence(self):
        for during in (False, True):
            with self.subTest(during=during):
                self.output = Path(self.tmp.name) / str(len(self.client.request.call_args_list))
                replies = [sample(4000), begin(0)] + ([sample(0)] if during else [])
                replies += [sample(4000), {"restore": "restored"}, sample(4000)]
                with self.assertRaisesRegex(RuntimeError, "float readback differs"):
                    self.run_console(replies)
                self.assertFalse(self.report()["changed_capture_valid"])
                self.assertTrue(self.report()["restoration_verified"])

    def test_change_before_or_during_restored_capture_invalidates_restoration(self):
        for during in (False, True):
            with self.subTest(during=during):
                self.output = Path(self.tmp.name) / str(len(self.client.request.call_args_list))
                replies = self.replies()[:7 if during else 6] + [sample(0)]
                with self.assertRaisesRegex(RuntimeError, "float readback differs"):
                    self.run_console(replies)
                self.assertFalse(self.report()["restoration_verified"])
                self.assertFalse(self.report()["restored_capture_valid"])

    def test_already_at_value_does_not_create_lease_or_changed_capture(self):
        report = self.run_console([sample(0)])
        self.assertEqual(report["status"], "already_at_test_value")
        self.assertFalse(report["changed_capture_valid"])
        self.assertEqual(self.captures, ["baseline"])
        self.assertEqual(self.client.request.call_count, 1)

    def test_bad_baseline_is_rejected_before_writes(self):
        with self.assertRaisesRegex(RuntimeError, "integer readback is unavailable"):
            self.run_console([{"float": 4000, "int": None}])
        self.assertEqual(self.client.request.call_count, 1)
        self.assertEqual(self.captures, [])
        self.assertEqual(self.report()["status"], "failed")


if __name__ == "__main__":
    unittest.main()
