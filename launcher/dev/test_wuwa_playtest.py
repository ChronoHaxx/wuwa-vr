"""Offline playtest persistence/report checks. Uses tempdirs; no game or devices."""

from concurrent.futures import ThreadPoolExecutor
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import wuwa_playtest as p


BUILD = {"id": "private-test", "sha256": "a" * 64,
         "rendering": {"payload": "R2", "settings": ["preserved", 1, False]}}
CHECKS = [
    {"id": "startup", "title": {"en": "Startup", "zh-Hans": "启动"},
     "instructions": {"en": "Launch explicitly, then inspect stereo.", "zh-Hans": "手动启动，然后检查立体画面。"}},
    {"id": "portal", "title": {"en": "Portal", "zh-Hans": "窗口"},
     "instructions": {"en": "Toggle and retry.", "zh-Hans": "切换并重试。"}},
]


class PlaytestTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "store"
        self.now = 1_000_000
        self.store = p.SessionStore(self.root, clock_ms=lambda: self.now)

    def start(self, **kwargs):
        return self.store.start(copy.deepcopy(BUILD), copy.deepcopy(CHECKS), **kwargs)["session_id"]

    def test_restart_preserves_exact_build_definition_results_and_notes(self):
        build, checks = copy.deepcopy(BUILD), copy.deepcopy(CHECKS)
        initial = self.store.start(build, checks, "zh-Hans", event_id="start-1")
        sid = initial["session_id"]
        build["sha256"] = "changed"
        checks[0]["title"]["en"] = "changed"
        initial["build"]["rendering"]["payload"] = "changed"
        self.store.record_result(sid, "startup", "pass", event_id="result-1")
        note = "  First line\n第二行: camera jitters 😵\n\tkeep exact spacing  "
        self.now += 1234
        self.store.add_note(sid, "portal", note, event_id="note-1")
        resumed = p.SessionStore(self.root).current()
        self.assertEqual(resumed["session_id"], sid)
        self.assertEqual(resumed["build"], BUILD)
        self.assertEqual(resumed["checks"][0]["title"], CHECKS[0]["title"])
        self.assertEqual(resumed["checks"][0]["status"], "pass")
        self.assertEqual(resumed["checks"][1]["status"], "not_tested")
        self.assertEqual(resumed["checks"][1]["notes"][0]["note"], note)
        self.assertEqual(resumed["checks"][1]["notes"][0]["session_ms"], 1234)

    def test_retry_is_idempotent_even_when_clock_changes_and_after_finish(self):
        sid = self.start(event_id="start-1")
        self.assertEqual(self.start(event_id="start-1"), sid)
        self.store.record_result(sid, "startup", "fail", "exact", event_id="result-1")
        self.now += 9000
        retry = self.store.record_result(sid, "startup", "fail", "exact", event_id="result-1")
        self.assertEqual(len(retry["events"]), 1)
        self.assertEqual(retry["events"][0]["session_ms"], 0)
        final = self.store.finish(sid, "still not done", event_id="finish-1")
        self.now += 9000
        self.assertEqual(self.store.finish(sid, "still not done", event_id="finish-1"), final)
        self.assertEqual(self.store.record_result(sid, "startup", "fail", "exact", event_id="result-1"), final)
        self.assertIsNone(self.store.current())
        for action in (
            lambda: self.store.record_result(sid, "startup", "pass", "exact", event_id="result-1"),
            lambda: self.store.add_note(sid, "portal", "new"),
            lambda: self.store.finish(sid, "changed", event_id="finish-1"),
            lambda: self.store.finish(sid),
        ):
            with self.assertRaises(p.SessionConflict):
                action()

    def test_start_does_not_replace_active_session_or_change_identity_on_retry(self):
        sid = self.start(event_id="start-1")
        with self.assertRaises(p.SessionConflict):
            self.start()
        with self.assertRaises(p.SessionConflict):
            self.store.start({"id": "different"}, CHECKS, event_id="start-1")
        self.assertEqual(self.store.current()["session_id"], sid)
        self.store.finish(sid)
        self.assertNotEqual(self.start(), sid)
        self.assertEqual(len(self.store.list_sessions()), 2)

    def test_explicit_status_history_does_not_infer_pass_from_notes_or_recording(self):
        sid = self.start()
        self.store.link_recording(sid, "video-1", label="A local clip", event_id="video-event")
        self.store.add_note(sid, "startup", "looks perfect", recording_id="video-1", video_ms=200,
                            session_ms=700, event_id="note")
        for index, status in enumerate(("fail", "pass", "blocked", "not_tested")):
            snapshot = self.store.record_result(sid, "portal", status, event_id=f"result-{index}")
        self.assertEqual(snapshot["counts"], {"not_tested": 2, "pass": 0, "fail": 0, "blocked": 0})
        self.assertEqual([event["status"] for event in snapshot["events"] if event["type"] == "result"],
                         ["fail", "pass", "blocked", "not_tested"])
        self.assertEqual(snapshot["checks"][0]["notes"][0]["video_ms"], 200)

    def test_partial_finish_reports_exact_feedback_and_keeps_unaccepted_items(self):
        sid = self.start()
        hostile = ' <script>alert("x")</script>\n```\n<img src=x onerror=alert(1)>\n[click](javascript:evil)\n~~~\n中文 & \x00  '
        self.store.record_result(sid, "startup", "blocked", hostile, event_id="result-1")
        self.store.add_note(sid, "portal", "Audio pending", attachment_id="voice-123", event_id="voice")
        result = self.store.finish(sid, "  final\nfeedback  ", event_id="finished")
        folder = self.root / sid
        report = json.loads((folder / "report.json").read_text(encoding="utf-8"))
        self.assertEqual(report, result)
        self.assertEqual(report["checks"][0]["notes"][0]["note"], hostile)
        self.assertEqual(report["counts"], {"not_tested": 1, "pass": 0, "fail": 0, "blocked": 1})
        self.assertEqual(report["events"][-1]["note"], "  final\nfeedback  ")
        output = (folder / "report.html").read_text(encoding="utf-8")
        self.assertNotIn("<script>", output)
        self.assertNotIn("<img src=", output)
        self.assertIn("&lt;script&gt;", output)
        self.assertIn("Content-Security-Policy", output)
        self.assertIn("transcription not implied", output)
        markdown = self.store.report(sid, "markdown")
        self.assertIn("````\n" + hostile + "\n````", markdown)
        self.assertEqual(self.store.report(sid, "json"), (folder / "report.json").read_text(encoding="utf-8"))

    def test_committed_finish_recovers_all_reports_after_interrupted_generation(self):
        sid = self.start()
        self.store.record_result(sid, "startup", "pass", event_id="result")
        with patch.object(self.store, "_write_reports", side_effect=OSError("power interrupted")):
            with self.assertRaises(OSError):
                self.store.finish(sid, "saved first", event_id="finish")
        self.assertFalse((self.root / sid / "report.json").exists())
        restarted = p.SessionStore(self.root)
        snapshot = restarted.snapshot(sid)
        self.assertEqual(snapshot["state"], "finished")
        self.assertEqual(snapshot["events"][-1]["note"], "saved first")
        self.assertEqual(restarted.finish(sid, "saved first", event_id="finish"), snapshot)
        for extension in ("json", "md", "html"):
            self.assertTrue((self.root / sid / ("report." + extension)).is_file())

    def test_uncommitted_event_and_orphan_session_are_ignored_after_interruption(self):
        sid = self.start()
        original = p.os.replace

        def interrupt_event(source, destination):
            if Path(destination).parent.name == "events":
                raise OSError("interrupted before commit")
            return original(source, destination)

        with patch.object(p.os, "replace", side_effect=interrupt_event):
            with self.assertRaises(OSError):
                self.store.record_result(sid, "startup", "pass", event_id="result")
        events = self.root / sid / "events"
        (events / (".tmp-" + "a" * 32)).write_text('{"unfinished":', encoding="utf-8")
        (self.root / ("a" * 32)).mkdir()
        restarted = p.SessionStore(self.root)
        self.assertEqual(restarted.current()["counts"]["not_tested"], 2)
        self.assertEqual(len(restarted.list_sessions()), 1)
        self.assertEqual(len(restarted.record_result(sid, "startup", "pass", event_id="result")["events"]), 1)

    def test_committed_corruption_or_missing_event_fails_closed(self):
        for mutation in ("truncate", "gap", "wrong-session", "changed-build"):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as directory:
                store = p.SessionStore(directory)
                sid = store.start(BUILD, CHECKS)["session_id"]
                store.record_result(sid, "startup", "pass", event_id="result")
                store.add_note(sid, "portal", "note", event_id="note")
                event_path = Path(directory) / sid / "events" / "000001.json"
                if mutation == "truncate":
                    event_path.write_text('{"partial":', encoding="utf-8")
                elif mutation == "gap":
                    event_path.unlink()
                elif mutation == "wrong-session":
                    event = json.loads(event_path.read_text(encoding="utf-8"))
                    event["session_id"] = "b" * 32
                    event_path.write_text(json.dumps(event), encoding="utf-8")
                else:
                    manifest_path = Path(directory) / sid / "session.json"
                    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
                    manifest["build"]["id"] = "silently-swapped"
                    manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
                with self.assertRaises(p.CorruptSessionError):
                    store.snapshot(sid)

    def test_wrong_ids_paths_offsets_and_statuses_never_append_evidence(self):
        sid = self.start()
        invalid = [
            lambda: self.store.snapshot("../outside"),
            lambda: self.store.snapshot("b" * 32),
            lambda: self.store.record_result(sid, "unknown", "pass"),
            lambda: self.store.record_result(sid, "startup", "PASS"),
            lambda: self.store.record_result(sid, "startup", True),
            lambda: self.store.record_result(sid, "startup", "pass", event_id=""),
            lambda: self.store.record_result(sid, "startup", "pass", event_id=False),
            lambda: self.store.add_note(sid, "startup", "x", recording_id="not-linked"),
            lambda: self.store.add_note(sid, "startup", "x", video_ms=42),
            lambda: self.store.add_note(sid, "startup", "x", session_ms=True),
            lambda: self.store.add_note(sid, "startup", "x", session_ms=-1),
            lambda: self.store.add_note(sid, "startup", "x", session_ms=p.MAX_TIME_MS + 1),
            lambda: self.store.add_note(sid, "startup", "x", attachment_id="../../audio.wav"),
            lambda: self.store.add_note(sid, "startup", "x", attachment_id="https://host/audio"),
            lambda: self.store.add_note(sid, "startup", "x" * (p.MAX_NOTE_CHARS + 1)),
            lambda: self.store.add_note(sid, "startup", "\ud800"),
            lambda: self.store.link_recording(sid, "C:\\private\\video.mp4"),
            lambda: self.store.link_recording(sid, "video.mp4"),
        ]
        for action in invalid:
            with self.subTest(action=action), self.assertRaises(p.PlaytestError):
                action()
        self.assertEqual(self.store.snapshot(sid)["events"], [])

    def test_invalid_definitions_do_not_create_sessions(self):
        invalid_checks = [[], [CHECKS[0], CHECKS[0]], [{"id": "a", "title": {"en": "Only English"}, "instructions": {"en": "x"}}]]
        for checks in invalid_checks:
            with self.assertRaises(p.PlaytestError):
                self.store.start(BUILD, checks)
        for build in ({}, {"bad": float("nan")}, {"nested": {1: "coerced"}}, {"tuple": (1, 2)}, {"too_big": "x" * 32768}):
            with self.assertRaises(p.PlaytestError):
                self.store.start(build, CHECKS)
        self.assertEqual(self.store.list_sessions(), [])

    def test_multiple_instances_serialize_concurrent_appends_and_shared_retries(self):
        sid = self.start()

        def append(index):
            store = p.SessionStore(self.root, clock_ms=lambda: self.now)
            store.add_note(sid, "startup", f"note-{index % 12}", event_id=f"event-{index % 12}")

        with ThreadPoolExecutor(max_workers=6) as executor:
            list(executor.map(append, range(48)))
        events = self.store.snapshot(sid)["events"]
        self.assertEqual(len(events), 12)
        self.assertEqual([event["sequence"] for event in events], list(range(1, 13)))
        self.assertEqual({event["note"] for event in events}, {f"note-{i}" for i in range(12)})

    def test_event_limit_does_not_prevent_retry_of_committed_event(self):
        sid = self.start()
        with patch.object(p, "MAX_EVENTS", 2):
            self.store.add_note(sid, "startup", "first", event_id="note")
            self.assertEqual(len(self.store.add_note(sid, "startup", "first", event_id="note")["events"]), 1)
            with self.assertRaises(p.PlaytestError):
                self.store.add_note(sid, "startup", "second")
            self.assertEqual(self.store.finish(sid, event_id="finish")["state"], "finished")

    def test_processes_share_one_commit_sequence(self):
        sid = self.start()
        script = """import sys
from wuwa_playtest import SessionStore
store = SessionStore(sys.argv[1])
for index in range(6):
    identity = 'process-' + sys.argv[3] + '-' + str(index)
    store.add_note(sys.argv[2], 'startup', identity, event_id=identity)
"""
        processes = [subprocess.Popen(
            [sys.executable, "-c", script, str(self.root), sid, str(index)],
            cwd=Path(__file__).parent, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        ) for index in range(4)]
        try:
            for process in processes:
                stdout, stderr = process.communicate(timeout=20)
                self.assertEqual(process.returncode, 0, (stdout, stderr))
        finally:
            for process in processes:
                if process.poll() is None:
                    process.kill()
                    process.wait()
        events = self.store.snapshot(sid)["events"]
        self.assertEqual(len(events), 24)
        self.assertEqual([event["sequence"] for event in events], list(range(1, 25)))
        self.assertEqual(len({event["event_id"] for event in events}), 24)

    def test_symlink_and_hardlink_guards_prevent_outside_report_writes(self):
        sid = self.start()
        outside = Path(self.temp.name) / "outside.txt"
        outside.write_text("untouched", encoding="utf-8")
        report = self.root / sid / "report.html"
        try:
            report.hardlink_to(outside)
        except OSError as exc:
            self.skipTest(f"Filesystem cannot create a hardlink: {exc}")
        with self.assertRaises(p.PlaytestError):
            self.store.finish(sid, event_id="finish")
        self.assertEqual(outside.read_text(encoding="utf-8"), "untouched")
        report.unlink()
        self.assertEqual(self.store.snapshot(sid)["state"], "finished")
        link = Path(self.temp.name) / "linked-store"
        try:
            link.symlink_to(self.root, target_is_directory=True)
        except OSError:
            return  # Windows commonly restricts symlink creation; hardlink guard ran.
        with self.assertRaises(p.PlaytestError):
            p.SessionStore(link)

    def test_active_report_is_preview_and_does_not_finish_or_write_files(self):
        sid = self.start()
        preview = json.loads(self.store.report(sid, "json"))
        self.assertEqual(preview["state"], "active")
        self.assertIsNone(preview["reports"])
        self.assertFalse((self.root / sid / "report.json").exists())
        self.assertEqual(self.store.current()["session_id"], sid)


if __name__ == "__main__":
    unittest.main()
