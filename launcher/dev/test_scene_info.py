"""Scene info files from a recording's motion.jsonl (wuwa_scene.py)."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import wuwa_scene  # noqa: E402

TITLE, STEP, TALK = "PnlInfo/PnlTitle/TxtTitle", "PnlParent/PnlSubTitle/TxtParent", "ScreenSpaceUIRoot/UiView_Plot/TxtTalk"


def row(unix_ms, scene=None, **camera):
    if scene is not None:
        camera["scene"] = scene
    return {"type": "frame", "unix_ms": unix_ms, "camera": camera}


class SceneInfo(unittest.TestCase):
    def folder(self, rows, started=1_000_000):
        d = Path(tempfile.mkdtemp())
        (d / "recording.json").write_text(json.dumps({"started_unix_ms": started}), encoding="utf-8")
        with open(d / "motion.jsonl", "w", encoding="utf-8") as f:
            f.write(json.dumps({"type": "header", "unix_ms": started}) + "\n")
            for r in rows:
                f.write(json.dumps(r) + "\n")
            f.write('{"type": "frame", "unix_ms": 99')  # a crash leaves half a line
        return d

    def test_timeline(self):
        full = {"full": True, "texts": {TITLE: "Simulacrum of the Heart", STEP: "Return to the Heart of Bloom"},
                "map": "WP_3_7_WXSG", "pawn": "BP_Xinyuehu_C"}
        talk = {"set": {TALK: "Leyue: This is the place."}}
        gone = {"gone": [TALK]}
        rows = [row(1_000_000, full), row(1_000_033, full), row(1_000_066, full), row(1_000_500),
                row(1_065_000, talk), row(1_065_040, talk), row(1_065_080, talk),
                row(1_070_000, gone), row(1_070_040, gone),
                row(1_090_000, {"full": True, "texts": {TITLE: "Simulacrum of the Heart", STEP: "Talk to Leyue"},
                                "map": "WP_3_7_WXSG", "pawn": "BP_Xinyuehu_C"})]
        d = self.folder(rows)
        self.assertEqual(wuwa_scene.write(d), 4)
        events = [json.loads(l) for l in (d / "scene.jsonl").read_text(encoding="utf-8").splitlines()]
        self.assertEqual([e["t"] for e in events], [0.0, 65.0, 70.0, 90.0])
        self.assertEqual(events[0]["map"], "WP_3_7_WXSG")
        self.assertEqual(events[2], {"t": 70.0, "gone": [TALK]})
        self.assertEqual(events[3], {"t": 90.0, "set": {STEP: "Talk to Leyue"}})  # a full snapshot adds only changes
        text = (d / "scene.txt").read_text(encoding="utf-8")
        self.assertIn("0:00  Quest      Simulacrum of the Heart", text)
        self.assertIn("0:00  Step       Return to the Heart of Bloom", text)
        self.assertIn("1:05  UiView_Plot/TxtTalk Leyue: This is the place.", text)
        self.assertIn("1:30  Step       Talk to Leyue", text)
        self.assertIn("0:00  Character  BP_Xinyuehu_C", text)

    def test_digits_masked_and_nothing_without_scene(self):
        d = self.folder([row(1_000_000, {"full": True, "texts": {"A/B/Txt": "User ID: 701123532"}})])
        wuwa_scene.write(d)
        self.assertNotIn("701123532", (d / "scene.txt").read_text(encoding="utf-8"))
        self.assertNotIn("701123532", (d / "scene.jsonl").read_text(encoding="utf-8"))
        empty = self.folder([row(1_000_000), row(1_000_033)])
        self.assertEqual(wuwa_scene.write(empty), 0)
        self.assertFalse((empty / "scene.txt").exists())

    def test_missing_files(self):
        d = Path(tempfile.mkdtemp())
        self.assertEqual(wuwa_scene.write(d), 0)


if __name__ == "__main__":
    unittest.main()
