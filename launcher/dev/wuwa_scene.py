"""Scene info of a recording (1.2.6): what the game showed, from the "scene" parts of motion.jsonl.

The plugin's Lua (mod/lua/wuwa_scene_log.lua) puts what changed on the screen's game text on the recording rows:
{"full": true, "texts": {key: text}, "map", "pawn"} now and then, {"set": {key: text}, "gone": [keys], "map",
"pawn"} in between, each repeated on a few rows. write(folder) replays them in order and writes, next to the video:
  scene.jsonl  one row per real change: {"t": video seconds, "set", "gone", "map", "pawn"}
  scene.txt    the same as a readable timeline ("1:23  Quest  Simulacrum of the Heart")
Times are video seconds: (unix_ms - recording.json started_unix_ms) / 1000. Keys are the widget's last three names
(PnlInfo/PnlTitle/TxtTitle); a few known ones get a label. The plugin never reads the UID widget and masks runs of
7+ digits; this file masks them again.
"""
import json
import re
from pathlib import Path

LABELS = (
    ("PnlTitle/TxtTitle", "Quest"),
    ("PnlSubTitle/TxtParent", "Step"),
    ("TxtMissionUpdate", "Banner"),
)
DIGITS = re.compile(r"\d{7,}")


def label(key):
    for tail, name in LABELS:
        if key.endswith(tail):
            return name
    return "/".join(key.split("/")[-2:])


def clock(t):
    t = max(0, int(t))
    return f"{t // 3600}:{t % 3600 // 60:02d}:{t % 60:02d}" if t >= 3600 else f"{t // 60}:{t % 60:02d}"


def mask(text):
    return DIGITS.sub(lambda m: "#" * len(m.group(0)), text) if isinstance(text, str) else None


def events(motion_path, started_ms):
    """Real changes, in order: (t, set, gone, map, pawn); repeats and unchanged full snapshots drop out."""
    state, where = {}, {"map": None, "pawn": None}
    out = []
    with open(motion_path, "rb") as f:
        for line in f:
            try:
                row = json.loads(line)
            except ValueError:
                continue  # a crash can leave half a line at the end
            scene = (row.get("camera") or {}).get("scene") if isinstance(row, dict) else None
            if not isinstance(scene, dict) or not isinstance(row.get("unix_ms"), (int, float)):
                continue
            t = (row["unix_ms"] - started_ms) / 1000.0
            if scene.get("full"):
                texts = {k: mask(v) for k, v in (scene.get("texts") or {}).items() if isinstance(v, str)}
                set_ = {k: v for k, v in texts.items() if state.get(k) != v}
                gone = sorted(k for k in state if k not in texts)
            else:
                set_ = {k: mask(v) for k, v in (scene.get("set") or {}).items()
                        if isinstance(v, str) and state.get(k) != mask(v)}
                gone = sorted(k for k in (scene.get("gone") or []) if k in state)
            moved = {}
            for key in ("map", "pawn"):
                if scene.get(key) and scene[key] != where[key]:
                    where[key] = moved[key] = scene[key]
            for k in gone:
                state.pop(k, None)
            state.update(set_)
            if set_ or gone or moved:
                out.append({"t": round(t, 3), **({"set": set_} if set_ else {}), **({"gone": gone} if gone else {}), **moved})
    return out


def write(folder):
    """Writes scene.jsonl and scene.txt into a recording folder; returns the number of changes (0: nothing written)."""
    folder = Path(folder)
    motion, meta = folder / "motion.jsonl", folder / "recording.json"
    if not motion.is_file() or not meta.is_file():
        return 0
    started = json.loads(meta.read_text(encoding="utf-8")).get("started_unix_ms")
    if not isinstance(started, (int, float)):
        return 0
    rows = events(motion, started)
    if not rows:
        return 0
    with open(folder / "scene.jsonl", "w", encoding="utf-8", newline="\n") as f:
        for r in rows:
            f.write(json.dumps(r, ensure_ascii=False) + "\n")
    lines = [f"Scene info for {folder.name}: what the game showed, at video time. Your UID is never saved.", ""]
    for r in rows:
        at = clock(r["t"])
        if "map" in r:
            lines.append(f"{at}  Map        {r['map']}")
        if "pawn" in r:
            lines.append(f"{at}  Character  {r['pawn']}")
        for key, text in sorted((r.get("set") or {}).items(), key=lambda kv: (label(kv[0]) not in ("Quest", "Step"), kv[0])):
            lines.append(f"{at}  {label(key):<10} {text}")
    (folder / "scene.txt").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    return len(rows)
