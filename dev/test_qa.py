"""dev/qa.py: matrix validation, session-log invariants, checklist and report. Offline; no game."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("qa", Path(__file__).with_name("qa.py"))
qa = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qa)

MATRIX = {
    "states": {"full169": "Full VR", "stereo": "Stereo screen", "load": "Load"},
    "dimensions": {"copy": ["steam", "launcher"], "input": ["controller", "mouse"]},
    "known_off": [{"match": "\\[WuWaWorldLabels\\]", "why": "pinned"}],
    "features": [
        {"id": "eye-camera", "area": "Stereo", "name": "Shared camera", "states": ["full169", "stereo"], "smoke": True,
         "how": "look", "checks": ["invariant:camera-shared"], "code": ["mod/hook.cpp"]},
        {"id": "ui-cursor", "area": "UI", "name": "Cursor", "states": ["stereo"], "dims": {"input": ["controller", "mouse"]},
         "how": "click", "checks": ["manual"], "code": ["mod/lua/*.lua"]},
        {"id": "eye-ao", "area": "Stereo", "name": "AO", "states": ["full169"], "how": "look", "checks": ["manual"]},
    ],
}


def line(clock, level, text):
    return f"[2026-10-15 {clock}.100] [UnrealVR] [{level}] {text}"


LOG = "\n".join([
    line("10:00:00", "info", "UnrealVR entry"),
    line("10:00:01", "info", "[OpenXR] OpenXR system Name: SteamVR/OpenXR : lighthouse"),
    line("10:00:02", "info", "[WuWaStereoCode] route=Same-draw stereo game camera verified_ranges=2 image_size=ad1c000 timestamp=6aa96d2d"),
    line("10:00:02", "warning", "[WuWaWorldLabels] unavailable; existing HUD/menu route retained: World-label route code mismatch"),
    line("10:00:03", "error", "[WuWaLguiViewport] refused: code mismatch rva=c66881"),
    line("10:00:04", "info", "[WuWaStereoBase] action=reused reason=none caller_rva=1234 verified_site=true source_call=1"),
    line("10:00:09", "info", "[WuWaStereoBase] action=reused reason=none caller_rva=1234 verified_site=true source_call=1"),
    line("10:00:10", "info", "[Plugin] WuWaHud: Menu laid out for the screen again (screen check)"),
    line("10:00:11", "info", "[Plugin] WuWaCinema: Waiting for a verified cinematic signal. movie asset not loaded [detection 0 ms]"),
    line("10:00:12", "info", "[Plugin] WuWaCinema: Automatic mono theatre active. story video [detection 1 ms]"),
    line("10:00:13", "info", "[Plugin] WuWaCinema: Automatic mono theatre active. story video [detection 2 ms]"),
    line("10:00:14", "error", "[VR] xrEndFrame failed: XR_ERROR_TIME_INVALID"),
    line("10:00:15", "error", "Failed to find true vfunc at 7ffba1a2bd30, reason 3"),
])


class QaTests(unittest.TestCase):
    def test_the_real_matrix_is_valid(self):
        self.assertEqual(qa.validate(qa.load_matrix()), [])

    def test_validation_names_each_problem(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "mod").mkdir()
            (root / "mod/hook.cpp").write_text("")
            bad = json.loads(json.dumps(MATRIX))
            bad["features"].append(dict(bad["features"][2]))
            bad["features"][0]["states"].append("orbit")
            bad["features"][0]["checks"].append("unit:dev/test_missing.lua")
            bad["features"][1]["dims"] = {"input": ["keyboard"]}
            problems = qa.validate(bad, root)
        text = "\n".join(problems)
        for expected in ("duplicated: 'eye-ao'", "unknown state orbit", "test_missing.lua does not exist",
                         "unknown dimension input", "mod/lua/*.lua matches no file"):
            self.assertIn(expected, text)

    def test_cells_are_every_state_times_every_dimension(self):
        self.assertEqual(len(qa.cells(MATRIX["features"][1])), 2)
        self.assertEqual(len(qa.cells(MATRIX["features"][0])), 2)

    def test_session_turns_the_log_into_invariants(self):
        s = qa.session(LOG, MATRIX["known_off"])
        self.assertEqual((s["runtime"], s["copy"], s["game_build"]), ("headset", "steam", "6aa96d2d"))
        self.assertEqual(len(s["known_off"]), 1)
        self.assertEqual(len(s["refused"]), 1)
        self.assertEqual([h for _, h in s["cinema"]],
                         ["Waiting for a verified cinematic signal", "Automatic mono theatre active"])
        inv = {name: (result, detail) for name, result, detail in qa.invariants(s)}
        self.assertEqual(inv["code-checks"][0], "fail")
        self.assertEqual(inv["camera-shared"], ("pass", "reused/none 2"))
        self.assertEqual(inv["layout-resyncs"][0], "pass")
        self.assertIn("1 resyncs", inv["layout-resyncs"][1])
        self.assertEqual(inv["cinema-events"][0], "pass")
        self.assertEqual(inv["errors"][0], "unknown")

    def test_the_mouse_ray_invariant_needs_the_taller_layout_and_a_rewrite(self):
        quiet = line("10:03:00", "info", "[WuWaLguiViewport] installed=true size=1920x1080 rewrites=50 pointer=0x0 pointer_rewrites=0 sites=3f callers=1ff by_caller=1,2,3,4,5,6,7,8,9")
        inv = {n: (r, d) for n, r, d in qa.invariants(qa.session(LOG + "\n" + quiet))}
        self.assertEqual(inv["mouse-ray"], ("unknown", "taller layout not used in this session"))
        tall = quiet.replace("size=1920x1080", "size=0x0").replace("pointer=0x0 pointer_rewrites=0", "pointer=1920x1080 pointer_rewrites=12")
        inv = {n: (r, d) for n, r, d in qa.invariants(qa.session(LOG + "\n" + tall))}
        self.assertEqual(inv["mouse-ray"][0], "pass")
        self.assertIn("12 mouse rays", inv["mouse-ray"][1])

    def test_a_layout_loop_or_an_unshared_camera_fails(self):
        log = LOG + "\n" + line("10:01:00", "info", "[Plugin] WuWaHud: Layout keeps changing; stopped checking") + "\n" + \
            line("10:01:01", "info", "[WuWaStereoBase] action=ignored reason=disabled caller_rva=0 verified_site=false source_call=0")
        inv = {name: result for name, result, _ in qa.invariants(qa.session(log, MATRIX["known_off"]))}
        self.assertEqual((inv["layout-resyncs"], inv["camera-shared"]), ("fail", "fail"))

    def test_baseline_shows_new_error_kinds_and_lost_routes(self):
        baseline = qa.session(LOG.replace("7ffba1a2bd30", "7ffbbbbbbb10"), MATRIX["known_off"])
        now = qa.session("\n".join(l for l in LOG.splitlines() if "route=" not in l) + "\n" +
                         line("10:02:00", "error", "[VR] something new 42"), MATRIX["known_off"])
        inv = {name: (result, detail) for name, result, detail in qa.invariants(now, baseline)}
        self.assertEqual(inv["errors"][0], "fail")
        self.assertEqual(now["new_errors"], ["[VR] something new #"])
        self.assertIn("Same-draw stereo game camera", inv["code-checks"][1])

    def test_plan_lists_changed_smoke_and_unchecked_but_not_passed_on_this_build(self):
        results = [
            dict(feature="eye-camera", state="full169", dims={}, result="pass", build="17k"),
            dict(feature="eye-ao", state="full169", dims={}, result="pass", build="17j"),
        ]
        text = qa.plan(MATRIX, results, "17k", ["mod/lua/wuwa_hud_refresh.lua"])
        self.assertIn("Stereo screen (never checked)", text)       # eye-camera smoke, the cell not yet passed
        self.assertNotIn("- [ ] Full VR (", text.split("## UI")[0].split("`eye-camera`")[1].split("##")[0])
        self.assertIn("changed: mod/lua/wuwa_hud_refresh.lua", text)
        self.assertIn("input=mouse", text)
        self.assertNotIn("`eye-ao`", text)                          # passed before, untouched, not smoke

    def test_report_shows_the_worst_result_of_a_cell(self):
        results = [
            dict(feature="ui-cursor", state="stereo", dims={"input": "controller"}, result="pass", build="17k", date="2026-10-15"),
            dict(feature="ui-cursor", state="stereo", dims={"input": "mouse"}, result="fail", build="17k", date="2026-10-15"),
            dict(feature="eye-ao", state="full169", dims={}, result="pass", build="steam-20261015-1658", date="2026-10-15"),
        ]
        text = qa.report(MATRIX, results)
        self.assertIn("| Cursor (`ui-cursor`) | ✗ |", text)
        self.assertIn("✓ 1015-1658 10-15", text)
        self.assertIn("| Shared camera (`eye-camera`) | ? | ? | ? |", text)
        self.assertIn("| AO (`eye-ao`) | ✓ 1015-1658 10-15 |  |  |", text)

    def test_session_results_fill_the_log_column(self):
        s = qa.session(LOG, MATRIX["known_off"])
        rows = qa.session_results(MATRIX, s, qa.invariants(s), "17k", "2026-10-15")
        self.assertEqual([(r["feature"], r["state"], r["result"], r["dims"]) for r in rows],
                         [("eye-camera", "log", "pass", {"copy": "steam", "runtime": "headset"})])
        self.assertIn("| Shared camera (`eye-camera`) | ? | ? | ✓ 17k 10-15 |", qa.report(MATRIX, rows))


if __name__ == "__main__":
    unittest.main()
