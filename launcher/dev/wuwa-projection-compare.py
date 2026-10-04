"""Capture Raw / temporary horizontal symmetry / restored Raw for visual review.

Dry-run by default. Never launches a game, sends input, switches runtime or saves
settings. CPU projection checks do not prove a submitted frame or an NPC fix.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import math
from pathlib import Path
import sys
import time
import uuid


_spec = importlib.util.spec_from_file_location("wuwa_projection_live", Path(__file__).with_name("wuwa-test.py"))
live = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(live)
EXPECTED = {"horizontal": 0, "vertical": 0, "grow": False, "screen": False}
OPTIONS = {"VR_HorizontalProjectionOverride": "0", "VR_VerticalProjectionOverride": "0",
           "VR_GrowRectangleForProjectionCropping": "false", "VR_2DScreenMode": "false"}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def integer(value):
    return type(value) is int and value >= 0


def finite_array(value, rows, columns):
    return (isinstance(value, list) and len(value) == rows and
            all(isinstance(row, list) and len(row) == columns and
                all(type(v) in (int, float) and math.isfinite(v) for v in row) for row in value))


class Comparison:
    def __init__(self, client, pid, output, *, clock=time.monotonic, sleep=time.sleep):
        self.client, self.pid, self.output = client, pid, Path(output)
        self.clock, self.sleep = clock, sleep
        self.owner = uuid.uuid4().hex
        self.options = self.base = self.baseline = None
        self.latest_sequence = 0
        self.report = {"pid": pid, "source": client.capture_source, "status": "incomplete",
                       "lease_id": self.owner, "lease_seconds": 30, "settings_saved": False,
                       "input_sent": False, "baseline_verified": False, "changed_capture_valid": False,
                       "restoration_verified": False, "restored_capture_valid": False,
                       "interpretation": "CPU matrix/component checks only; visual review pending, no NPC fix claim",
                       "limitations": ["Menu/settings guards are sampled; brief changes between samples may be missed.",
                                       "CPU matrices are not bound to captured GPU frames; settling is a diagnostic precaution.",
                                       "SteamVR mirror eyes are not captured atomically."]}

    def environment(self):
        require(self.client.pid == self.pid, "Game PID differs from the requested process")
        state = self.client.assert_live()  # Also checks process creation identity.
        require(state.get("pid") == self.pid, "Backend process changed")
        controls = state.get("camera", {}).get("controls", {})
        require(controls.get("script_fresh") is True and integer(controls.get("script_age_ms")) and
                controls["script_age_ms"] <= 1000, "Fresh script/menu status is unavailable")
        self.report["last_focus_context"] = {"strict_game_foreground": controls.get("strict_game_foreground"),
                                             "script_age_ms": controls.get("script_age_ms")}
        for field in ("game_menu", "native_menu", "uevr_menu", "hud_mouse"):
            require(controls.get(field) is False, f"Close menus and HUD adjustment before comparison ({field})")
        options = state.get("live_options", {})
        for key, expected in OPTIONS.items():
            require(str(options.get(key)).lower() == expected, "Comparison requires live Raw/Raw, growth off and 2D off")
        if self.options is None:
            self.options = dict(options)
        require(options == self.options, "Live settings changed during comparison")
        return state

    def query(self, *, guarded=True, timeout=1):
        if guarded:
            self.environment()
        reply = self.client.request("projection_test", action="query", timeout=timeout)
        status = reply.get("projection_test")
        require(isinstance(status, dict), "Backend has no projection comparison status")
        self.report["last_query"] = status
        applied = status.get("applied") or {}
        if integer(applied.get("sequence")):
            self.latest_sequence = max(self.latest_sequence, applied["sequence"])
        return status

    def validate_base(self, status):
        base = status.get("base")
        require(isinstance(base, dict), "Projection baseline is unavailable")
        for key, expected in EXPECTED.items():
            require(type(base.get(key)) is type(expected) and base[key] == expected,
                    "Projection baseline changed or is not Raw/Raw with growth/2D off")
        for field in ("openxr", "native", "ready"):
            require(base.get(field) is True, "Comparison requires ready OpenXR native stereo")
        require(type(base.get("native_fix")) is bool, "Native stereo fix status is unavailable")
        if self.base is None:
            self.base = dict(base)
        require(base == self.base, "Projection runtime/base settings changed")

    def matrix_ready(self, status, horizontal, after_sequence, *, restoring=False):
        self.validate_base(status)
        require(status.get("active") is (horizontal == 1), "Projection lease expired, cancelled or unexpectedly active")
        if horizontal == 1:
            require(status.get("id") == self.owner, "Projection lease owner changed")
            require(integer(status.get("remaining_ms")) and 0 < status["remaining_ms"] <= 30000,
                    "Projection lease has no valid remaining lifetime")
        require(type(status.get("effective_horizontal")) is int and status["effective_horizontal"] == horizontal,
                "Effective projection differs from the requested mode")
        a = status.get("applied")
        if not isinstance(a, dict):
            return False
        require(a.get("base") == self.base, "Applied projection belongs to another base")
        if self.baseline is not None:
            require(a.get("epoch") == self.baseline["epoch"], "Projection runtime reset during comparison")
        key = a.get("key") or {}
        if (status.get("applied_matches_effective") is not True or
                a.get("lease_id") != (self.owner if horizontal == 1 else "") or
                type(key.get("horizontal")) is not int or key["horizontal"] != horizontal):
            return False  # A prior update can still be in flight immediately after begin/end.
        require(type(key.get("vertical")) is int and key["vertical"] == 0 and key.get("grow") is False,
                "Applied vertical projection or growth changed")
        require(type(key.get("near_z")) in (int, float) and math.isfinite(key["near_z"]) and
                finite_array(key.get("raw_fov"), 2, 4), "Projection key contains unavailable/nonfinite inputs")
        require(integer(a.get("epoch")) and finite_array(a.get("matrices_column_major"), 2, 16) and
                finite_array(a.get("crop_bounds"), 2, 4), "Projection matrices/crop snapshot is unavailable or nonfinite")
        if self.baseline is not None:
            old_key = dict(self.baseline["key"])
            old_key["horizontal"] = horizontal
            require(key == old_key, "Projection near plane/raw FOV changed during comparison")
            if restoring:
                require(a["matrices_column_major"] == self.baseline["matrices_column_major"] and
                        a["crop_bounds"] == self.baseline["crop_bounds"], "Raw matrices/crop differ from baseline")
        return (integer(a.get("sequence")) and a["sequence"] > after_sequence and
                integer(a.get("age_ms")) and a["age_ms"] <= 2000)

    def wait_applied(self, horizontal, after_sequence, *, guarded=True, restoring=False):
        deadline = self.clock() + 5
        while self.clock() < deadline:
            status = self.query(guarded=guarded, timeout=min(1, deadline - self.clock()))
            if self.matrix_ready(status, horizontal, after_sequence, restoring=restoring):
                return status
            remaining = deadline - self.clock()
            if remaining > 0:
                self.sleep(min(.1, remaining))
        raise TimeoutError("No fresh matching projection matrix update within 5 seconds")

    def capture(self, name, horizontal, after_sequence, *, restoring=False):
        before = self.query()
        require(self.matrix_ready(before, horizontal, after_sequence, restoring=restoring),
                f"Projection snapshot is stale or mismatched before {name} capture")
        if horizontal == 1:
            require(before["remaining_ms"] >= 16000, "Too little lease time remains for a bounded capture")
        self.report[name + "_before"] = before
        self.client.capture(self.output / name, self.client.capture_layer())
        after = self.query()
        require(self.matrix_ready(after, horizontal, before["applied"]["sequence"] - 1, restoring=restoring),
                f"Projection snapshot is stale or mismatched after {name} capture")
        self.report[name + "_after"] = after
        return after

    def run(self):
        self.output.mkdir(parents=True, exist_ok=False)
        previous_focus = getattr(self.client, "require_foreground", False)
        self.client.require_foreground = False  # Read-only captures may run while the user chats.
        try:
            self.environment()
            baseline = self.wait_applied(0, -1)
            self.baseline = baseline["applied"]
            self.report["baseline_projection"] = baseline
            self.report["baseline_live_options"] = self.options
            self.client.wait_frames(.5)
            baseline = self.capture("baseline", 0, self.baseline["sequence"] - 1, restoring=True)
            self.report["baseline_verified"] = True
            baseline_sequence = baseline["applied"]["sequence"]
            failure = None
            try:
                # The ID is known before dispatch: a lost reply can still be
                # followed by an owner-checked end. Server TTL is the fallback.
                self.report["begin"] = self.client.request("projection_test", action="begin", seconds=30,
                    expected=dict(EXPECTED), request_id=self.owner, timeout=3)
                require(self.report["begin"].get("id") == self.owner, "Begin acknowledgement identity differs")
                self.report["changed_settled"] = self.wait_applied(1, baseline_sequence)
                self.client.wait_frames(.5)
                self.capture("changed", 1, baseline_sequence)
                self.report["changed_capture_valid"] = True
            except BaseException as error:
                failure = error
            finally:
                errors = []
                restore_after = self.latest_sequence
                try:
                    self.report["end"] = self.client.request("projection_test", action="end", lease_id=self.owner, timeout=3)
                    ended = self.report["end"].get("projection_test", {})
                    require(ended.get("active") is False and ended.get("id") == self.owner,
                            "Owned projection end was not confirmed")
                except BaseException as error:
                    errors.append(str(error))
                    if failure is None:
                        failure = error
                try:
                    self.report["restored_projection"] = self.wait_applied(0, restore_after, guarded=False, restoring=True)
                except BaseException as error:
                    errors.append(str(error))
                    if failure is None:
                        failure = error
                self.report["restoration_verified"] = not errors
                if errors:
                    self.report["cleanup_errors"] = errors
                    self.report["fallback"] = "Owned lease expires within 30 seconds of begin; no restoration claim without readback"
            if failure is not None:
                raise failure
            self.client.wait_frames(.5)
            try:
                self.capture("restored", 0, restore_after, restoring=True)
            except BaseException:
                self.report["restoration_verified"] = False
                raise
            self.report["restored_capture_valid"] = True
            self.report["status"] = "captured_and_restored_visual_review_pending"
            return self.report
        except BaseException as error:
            self.report["status"] = "failed"
            self.report["error"] = str(error)
            raise
        finally:
            self.client.require_foreground = previous_focus
            live.write_json(self.output / "comparison.json", self.report)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, epilog="Keep a stationary gameplay scene with menus closed. No image proves an NPC fix automatically.")
    parser.add_argument("--run", action="store_true", help="Explicitly run the temporary 30-second comparison; otherwise print the plan only")
    parser.add_argument("--pid", type=int, help="Required with --run; refuse another game process")
    parser.add_argument("--output", type=Path, help="Required with --run; a new capture directory only")
    parser.add_argument("--capture-source", choices=("simulator", "steamvr"), default="simulator",
                        help="An already running capture source; never starts or switches runtime")
    parser.add_argument("--tools", type=Path, help="Explicit directory containing SteamVR capture helpers, if packaged elsewhere")
    args = parser.parse_args(argv)
    if not args.run:
        print(json.dumps({"status": "dry_run", "source": args.capture_source, "baseline": EXPECTED,
                          "temporary_horizontal": 1, "lease_seconds": 30,
                          "steps": ["verify gameplay and raw matrices", "capture baseline", "begin owned symmetry lease",
                                    "verify fresh matrices and capture", "end own lease", "verify raw restoration and capture"],
                          "live_calls": False, "visual_review_required": True}, indent=2))
        return 0
    if args.pid is None or args.pid <= 0 or args.output is None:
        parser.error("--run requires a positive --pid and --output")
    if args.output.exists():
        parser.error("--output already exists; choose a new directory")
    if args.tools is not None:
        live.TOOLS = args.tools.resolve()
    client = live.LiveTest(capture_source=args.capture_source)
    require(client.pid == args.pid, "Game process differs from --pid")
    with client.exclusive():
        report = Comparison(client, args.pid, args.output).run()
    print(json.dumps({"status": report["status"], "report": str(args.output / "comparison.json")}, indent=2))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, KeyError) as exc:
        print(f"Projection comparison stopped: {exc}", file=sys.stderr)
        sys.exit(1)
