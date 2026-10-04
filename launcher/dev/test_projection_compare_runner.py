"""Projection comparison contract against a fake backend, no runtime/device calls."""
import contextlib
import copy
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


spec = importlib.util.spec_from_file_location("projection_runner", Path(__file__).with_name("wuwa-projection-compare.py"))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class Clock:
    value = 0
    def __call__(self):
        return self.value
    def sleep(self, seconds):
        self.value += seconds


class Backend:
    def __init__(self, clock):
        self.clock = clock
        self.pid, self.capture_source = 4242, "simulator"
        self.base = dict(runner.EXPECTED, openxr=True, native=True, ready=True, native_fix=True)
        self.options = dict(runner.OPTIONS)
        self.controls = dict(script_fresh=True, script_age_ms=10, strict_game_foreground=False,
                             game_menu=False, native_menu=False, uevr_menu=False, hud_mouse=False)
        self.active, self.owner, self.until, self.sequence = False, "", 0, 10
        self.calls, self.captures = [], []
        self.fail_begin = self.fail_end = self.bad_end = False
        self.mutate_query = self.mutate_capture = None
        self.frozen_sequence = False

    def assert_live(self):
        return {"pid": self.pid, "camera": {"controls": copy.deepcopy(self.controls)},
                "live_options": dict(self.options)}

    def snapshot(self):
        if self.clock() >= self.until:
            self.active = False
        horizontal = 1 if self.active else 0
        if not self.frozen_sequence:
            self.sequence += 1
        matrix = [0.0] * 16
        matrix[0] = 2.0 if self.active else 1.0
        matrix[5], matrix[10], matrix[15] = 1.0, 1.0, 1.0
        applied = {"sequence": self.sequence, "at_tick_ms": int(self.clock() * 1000), "age_ms": 0,
                   "base": dict(self.base), "lease_id": self.owner if self.active else "", "epoch": 1,
                   "key": {"horizontal": horizontal, "vertical": 0, "grow": False, "near_z": 10,
                           "raw_fov": [[-.7, .8, .7, -.7], [-.8, .7, .7, -.7]]},
                   "matrices_column_major": [matrix[:], matrix[:]],
                   "crop_bounds": [[0., 0., 1., 1.], [0., 0., 1., 1.]]}
        return {"active": self.active, "id": self.owner, "reason": "active" if self.active else "ended",
                "remaining_ms": max(0, int((self.until - self.clock()) * 1000)) if self.active else 0,
                "base": dict(self.base), "effective_horizontal": horizontal, "applied": applied,
                "applied_matches_effective": True}

    def request(self, op, **fields):
        assert op == "projection_test"
        self.calls.append(dict(fields))
        self.clock.sleep(.01)
        action = fields["action"]
        if action == "begin":
            assert fields["seconds"] == 30
            assert fields["expected"] == runner.EXPECTED
            self.owner = fields["request_id"]
            self.active, self.until = True, self.clock() + 30
            if self.fail_begin:
                raise TimeoutError("begin response lost")
        elif action == "end":
            assert fields["lease_id"] == self.owner
            if self.fail_end:
                raise TimeoutError("end response lost")
            self.active = False
        status = self.snapshot()
        if action == "query" and self.mutate_query:
            self.mutate_query(status)
        if action == "end" and self.bad_end:
            status["id"] = "other-owner"
        return {"id": self.owner, "projection_test": status}

    def wait_frames(self, seconds):
        self.clock.sleep(seconds)

    def capture_layer(self):
        return "projection" if self.capture_source == "simulator" else "all"

    def capture(self, output, layer):
        assert getattr(self, "require_foreground", False) is False
        self.captures.append((output.name, layer))
        output.mkdir()
        self.clock.sleep(.2)
        if self.mutate_capture:
            self.mutate_capture(output.name)


class ProjectionRunner(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.output = Path(self.tmp.name) / "new"
        self.clock = Clock()
        self.backend = Backend(self.clock)
        self.comparison = runner.Comparison(self.backend, 4242, self.output, clock=self.clock, sleep=self.clock.sleep)

    def tearDown(self):
        self.tmp.cleanup()

    def report(self):
        return json.loads((self.output / "comparison.json").read_text(encoding="utf-8"))

    def assert_end(self):
        ends = [c for c in self.backend.calls if c["action"] == "end"]
        self.assertEqual(len(ends), 1)
        self.assertEqual(ends[0]["lease_id"], self.comparison.owner)

    def test_background_success_checks_matrices_and_restores_without_saved_writes(self):
        report = self.comparison.run()
        self.assertEqual(report["status"], "captured_and_restored_visual_review_pending")
        for key in ("baseline_verified", "changed_capture_valid", "restoration_verified", "restored_capture_valid"):
            self.assertTrue(report[key])
        self.assertEqual(self.backend.captures, [("baseline", "projection"), ("changed", "projection"), ("restored", "projection")])
        self.assertFalse(report["last_focus_context"]["strict_game_foreground"])
        self.assertEqual(self.backend.options, runner.OPTIONS)
        self.assert_end()
        self.assertFalse(self.backend.active)
        self.assertEqual(self.report(), report)

    def test_steamvr_uses_composited_layer(self):
        self.backend.capture_source = "steamvr"
        self.comparison.run()
        self.assertEqual({layer for _, layer in self.backend.captures}, {"all"})

    def test_dry_run_has_no_client_or_output_side_effects(self):
        with patch.object(runner.live, "LiveTest", side_effect=AssertionError("live call")), contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(runner.main(["--pid", "4242", "--output", str(self.output)]), 0)
        self.assertEqual(json.loads(output.getvalue())["status"], "dry_run")
        self.assertFalse(self.output.exists())

    def test_run_requires_pid_and_output_before_client(self):
        with patch.object(runner.live, "LiveTest", side_effect=AssertionError("live call")), contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                runner.main(["--run"])

    def test_existing_output_is_preserved(self):
        self.output.mkdir()
        marker = self.output / "keep.txt"
        marker.write_text("existing", encoding="utf-8")
        with self.assertRaises(FileExistsError):
            self.comparison.run()
        self.assertEqual(marker.read_text(encoding="utf-8"), "existing")
        self.assertEqual(self.backend.calls, [])

    def test_bad_baseline_settings_refused_before_begin(self):
        self.backend.options["VR_2DScreenMode"] = "true"
        with self.assertRaisesRegex(RuntimeError, "Raw/Raw"):
            self.comparison.run()
        self.assertEqual(self.backend.calls, [])
        self.assertEqual(self.report()["status"], "failed")

    def test_game_menu_refused_without_lease(self):
        self.backend.controls["native_menu"] = True
        with self.assertRaisesRegex(RuntimeError, "Close menus"):
            self.comparison.run()
        self.assertEqual(self.backend.calls, [])

    def test_wrong_process_refused_before_capture(self):
        self.backend.pid = 99
        with self.assertRaisesRegex(RuntimeError, "PID differs"):
            self.comparison.run()
        self.assertEqual(self.backend.captures, [])

    def test_non_native_runtime_refused_before_begin(self):
        self.backend.base["native"] = False
        with self.assertRaisesRegex(RuntimeError, "native stereo"):
            self.comparison.run()
        self.assertFalse(any(c["action"] == "begin" for c in self.backend.calls))

    def test_failed_begin_response_still_ends_known_owner_and_verifies_raw(self):
        self.backend.fail_begin = True
        with self.assertRaisesRegex(TimeoutError, "begin response lost"):
            self.comparison.run()
        self.assert_end()
        report = self.report()
        self.assertFalse(report["changed_capture_valid"])
        self.assertTrue(report["restoration_verified"])

    def test_stale_active_snapshot_times_out_in_five_seconds_and_restores(self):
        self.backend.mutate_query = lambda s: s["applied"].update(age_ms=2001) if s["active"] else None
        with self.assertRaisesRegex(TimeoutError, "within 5 seconds"):
            self.comparison.run()
        self.assertLess(self.clock(), 6.5)
        self.assert_end()
        self.assertTrue(self.report()["restoration_verified"])

    def test_mismatched_applied_key_does_not_become_valid_evidence(self):
        self.backend.mutate_query = lambda s: s["applied"]["key"].update(horizontal=0) if s["active"] else None
        with self.assertRaises(TimeoutError):
            self.comparison.run()
        self.assertFalse(self.report()["changed_capture_valid"])
        self.assert_end()

    def test_no_new_sequence_rejected(self):
        self.backend.frozen_sequence = True
        with self.assertRaises(TimeoutError):
            self.comparison.run()
        self.assertFalse(self.report()["changed_capture_valid"])
        self.assertFalse(self.report()["restoration_verified"])
        self.assert_end()

    def test_pending_old_snapshot_is_allowed_to_settle(self):
        remaining = [2]
        def mutate(status):
            if status["active"] and remaining[0]:
                remaining[0] -= 1
                status["applied"]["lease_id"] = ""
                status["applied"]["key"]["horizontal"] = 0
                status["applied_matches_effective"] = False
        self.backend.mutate_query = mutate
        self.assertTrue(self.comparison.run()["changed_capture_valid"])

    def test_expiry_during_capture_invalidates_it_and_cleanup_still_runs(self):
        self.backend.mutate_capture = lambda name: self.clock.sleep(31) if name == "changed" else None
        with self.assertRaisesRegex(RuntimeError, "expired"):
            self.comparison.run()
        self.assertFalse(self.report()["changed_capture_valid"])
        self.assertTrue(self.report()["restoration_verified"])
        self.assert_end()

    def test_settings_change_during_capture_stops_but_does_not_skip_owned_end(self):
        self.backend.mutate_capture = lambda name: self.backend.options.update(UI_Size="3") if name == "changed" else None
        with self.assertRaisesRegex(RuntimeError, "settings changed"):
            self.comparison.run()
        self.assert_end()
        self.assertFalse(self.report()["changed_capture_valid"])

    def test_wrong_restore_owner_ack_is_not_a_success(self):
        self.backend.bad_end = True
        with self.assertRaisesRegex(RuntimeError, "end was not confirmed"):
            self.comparison.run()
        self.assertFalse(self.report()["restoration_verified"])
        self.assertFalse(self.report()["restored_capture_valid"])

    def test_end_failure_records_fallback_and_never_claims_restore(self):
        self.backend.fail_end = True
        with self.assertRaisesRegex(TimeoutError, "end response lost"):
            self.comparison.run()
        report = self.report()
        self.assertFalse(report["restoration_verified"])
        self.assertIn("30 seconds", report["fallback"])
        self.assertEqual(len(report["cleanup_errors"]), 2)

    def test_raw_matrix_restoration_must_match_original(self):
        def mutate(status):
            if self.backend.owner and not status["active"]:
                status["applied"]["matrices_column_major"][0][0] = 9
        self.backend.mutate_query = mutate
        with self.assertRaisesRegex(RuntimeError, "Raw matrices/crop differ"):
            self.comparison.run()
        self.assertFalse(self.report()["restoration_verified"])

    def test_runtime_reset_aborts_even_with_fresh_matrices(self):
        self.backend.mutate_query = lambda s: s["applied"].update(epoch=2) if s["active"] else None
        with self.assertRaisesRegex(RuntimeError, "runtime reset"):
            self.comparison.run()
        self.assert_end()
        self.assertFalse(self.report()["changed_capture_valid"])


if __name__ == "__main__":
    unittest.main()
