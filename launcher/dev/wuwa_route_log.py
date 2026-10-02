"""Log your route for a whole play session (for maps, stats and timelapses).

Chains the backend's 5-minute motion recordings back to back while the game runs: player and
camera position 30 times a second, with wall-clock timestamps. Start it any time after the game
is up; Ctrl+C ends it. A session file lists the parts:
%LOCALAPPDATA%\\WuWa VR Launcher\\routes\\route-<date>-<time>.json

    python wuwa_route_log.py
"""
import datetime
import importlib.util
import json
import os
import time
from pathlib import Path

spec = importlib.util.spec_from_file_location("wuwa_live", Path(__file__).with_name("wuwa-test.py"))
live = importlib.util.module_from_spec(spec)
spec.loader.exec_module(live)
ROUTES = Path(os.environ["LOCALAPPDATA"]) / "WuWa VR Launcher" / "routes"


def main():
    ROUTES.mkdir(parents=True, exist_ok=True)
    started = datetime.datetime.now()
    session = ROUTES / f"route-{started:%Y%m%d-%H%M}.json"
    parts, test, current = [], None, None

    def save():
        session.write_text(json.dumps({"started": started.isoformat(timespec="seconds"), "parts": parts},
                                      indent=1), encoding="utf-8")

    print(f"Logging route to {session} (Ctrl+C to stop)", flush=True)
    try:
        while True:
            try:
                if test is None:
                    test = live.LiveTest()
                reply = test.request("record_motion", seconds=300)["recording"]
                current = reply["id"]
                path = Path(reply["path"])
                parts.append({"path": str(path), "id": current, "started_unix_ms": int(time.time() * 1000)})
                save()
                print(f"{datetime.datetime.now():%H:%M:%S} part {len(parts)}: {path.name}", flush=True)
                begun, size, changed = time.time(), -1, time.time()
                while time.time() - begun < 295:
                    time.sleep(2)
                    now_size = path.stat().st_size if path.exists() else 0
                    if now_size != size:
                        size, changed = now_size, time.time()
                    elif time.time() - changed > 10:   # ended early, or the game stopped drawing
                        break
                test.request("record_motion", seconds=0, recording_id=current)
                current = None
            except KeyboardInterrupt:
                raise
            except Exception as error:   # game not up yet, restarted or busy: retry
                print(f"{datetime.datetime.now():%H:%M:%S} waiting for the game ({error})", flush=True)
                test, current = None, None
                time.sleep(5)
    except KeyboardInterrupt:
        if test is not None and current:
            try:
                test.request("record_motion", seconds=0, recording_id=current)
            except Exception:
                pass
        save()
        print(f"Stopped. {len(parts)} part(s) listed in {session}")


if __name__ == "__main__":
    main()
