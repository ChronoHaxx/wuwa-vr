"""Record clean SteamVR stereo video plus a separate controller/camera replay.

Never captures the desktop, presses controls, or changes a graphics setting.
Requires the game already running through SteamVR and the matching recorder exe.
"""
from __future__ import annotations
import argparse
import bisect
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parent.parent
spec=importlib.util.spec_from_file_location("wuwa_live",Path(__file__).with_name("wuwa-test.py"))
live=importlib.util.module_from_spec(spec)
spec.loader.exec_module(live)


def read_rows(path: Path, limit=12000):
    if not path.is_file(): return []
    if path.stat().st_size>64*1024*1024: raise ValueError("Recording sidecar exceeds size limit")
    rows=[]
    with path.open(encoding="utf-8") as source:
        for line in source:
            try: row=json.loads(line)
            except ValueError: continue  # A crash can leave one incomplete final row.
            if isinstance(row,dict): rows.append(row)
            if len(rows)>limit: raise ValueError("Recording row limit exceeded")
    return rows


def correlate(frames, motion):
    """Nearest timestamp is an estimate; never label stale telemetry current."""
    samples=sorted((r for r in motion if r.get("type")=="frame" and isinstance(r.get("unix_ms"),(int,float))),key=lambda r:r["unix_ms"])
    stamps=[r["unix_ms"] for r in samples]
    output=[]
    for frame in frames:
        at=frame["capture_begin_ms"]
        i=bisect.bisect_left(stamps,at)
        candidates=[j for j in (i-1,i) if 0<=j<len(samples)]
        nearest=min(candidates,key=lambda j:abs(stamps[j]-at)) if candidates else None
        error=stamps[nearest]-at if nearest is not None else None
        output.append({"time":frame["video_seconds"],"capture_span_ms":frame["capture_end_ms"]-at,
            "sample_delta_ms":error,"sample":samples[nearest] if error is not None and abs(error)<=125 else None})
    return output


def make_replay(folder: Path):
    timeline=correlate(read_rows(folder/"frames.jsonl"),read_rows(folder/"motion.jsonl"))
    # Data is embedded as JSON, never interpolated into executable JavaScript.
    data=json.dumps(timeline,separators=(",",":"),ensure_ascii=True).replace("<","\\u003c")
    template=Path(__file__).with_name("recording-replay.html").read_text(encoding="utf-8")
    (folder/"replay.html").write_text(template.replace("__TIMELINE_JSON__",data),encoding="utf-8")
    return {"video_frames":len(timeline),"matched_samples":sum(r["sample"] is not None for r in timeline),
            "alignment":"nearest wall-clock sample within 125 ms; not exact GPU frame synchronization"}


def record(args):
    if args.output.exists(): raise ValueError("Output already exists; preserve it and choose a new folder")
    recorder=args.recorder.resolve()
    if not recorder.is_file(): raise ValueError("wuwa-recorder.exe is missing")
    openvr=args.openvr.resolve()
    if not openvr.is_file(): raise ValueError("openvr_api.dll is missing")
    client=live.LiveTest(profile=args.profile or live.PROFILE)
    if args.pid is not None and args.pid!=client.pid: raise ValueError("Game process changed")
    before=client.assert_live()
    # The native helper independently refuses when SteamVR is absent or when
    # its active scene belongs to another process (including simulator runs).
    recording=None
    with client.exclusive():
        if not args.video_only:
            if before.get("motion_recording",{}).get("active"): raise ValueError("A motion recording is already active")
            reply=client.request("record_motion",seconds=min(300,args.seconds+10))
            recording=reply.get("recording")
            if not recording or not recording.get("id"): raise RuntimeError("Backend did not confirm motion recording")
        error=None
        try:
            completed=subprocess.run([str(recorder),str(openvr),str(client.pid),str(args.output.resolve()),
                str(args.seconds),str(args.fps),str(args.eye_width),str(args.stop_file.resolve())],capture_output=True,text=True,
                creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0),timeout=args.seconds+45)
            if completed.returncode: raise RuntimeError(completed.stderr.strip() or "Video recording failed")
        except (OSError,RuntimeError,subprocess.TimeoutExpired) as exc: error=str(exc)
        finally:
            if recording:
                try: client.request("record_motion",seconds=0,recording_id=recording["id"])
                except (OSError,RuntimeError,TimeoutError): pass  # Game exit closes the file; lease is bounded.
            if args.output.is_dir():
                (args.output/"before.json").write_text(json.dumps(before,indent=2),encoding="utf-8")
                if recording:
                    source=Path(recording["path"]).resolve()
                    expected=(client.profile/"recordings").resolve()
                    if source.parent!=expected or source.suffix!=".jsonl": raise RuntimeError("Unexpected sidecar path")
                    if source.is_file(): shutil.copyfile(source,args.output/"motion.jsonl")
                report=make_replay(args.output)
                report["error"]=error
                (args.output/"replay.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
        if error: raise RuntimeError(error)
    return args.output/"replay.html"


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("output",type=Path)
    p.add_argument("--seconds",type=int,choices=range(1,301),metavar="1..300",default=120)
    p.add_argument("--fps",type=int,choices=(15,30,60),default=30)
    p.add_argument("--eye-width",type=int,choices=(640,960,1280,1920),default=1280)
    p.add_argument("--pid",type=int)
    p.add_argument("--profile",type=Path)
    p.add_argument("--stop-file",type=Path)
    p.add_argument("--recorder",type=Path,default=ROOT/"dev-tools/wuwa-recorder.exe")
    p.add_argument("--openvr",type=Path,default=ROOT/"dev-tools/openvr_api.dll")
    p.add_argument("--video-only",action="store_true",help="Clean video only for an older backend; no camera telemetry claim")
    p.add_argument("--rebuild-replay",action="store_true",help="Read existing video/sidecars without connecting to the game")
    args=p.parse_args(argv)
    if args.stop_file is None: args.stop_file=args.output.with_suffix('.stop')
    if args.rebuild_replay: print(json.dumps(make_replay(args.output))); return 0
    print(record(args))
    return 0


if __name__=="__main__":
    try: sys.exit(main())
    except (OSError,RuntimeError,ValueError) as error: print(f"Recording stopped: {error}",file=sys.stderr); sys.exit(1)
