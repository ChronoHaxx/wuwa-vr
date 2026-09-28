"""Record SteamVR or updated simulator stereo video with a separate motion replay.

Ordinary recording is passive. An explicit --input-plan can add a bounded
controller test sequence on a supporting backend; it is never enabled by default.
Requires the game already running and the matching recorder. Never captures the
desktop, changes graphics settings or switches runtimes.
"""
from __future__ import annotations
import argparse
import bisect
import html
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parent.parent
SIDECAR_BYTE_LIMIT=64*1024*1024
MOTION_OUTPUT_BYTE_LIMIT=60*1024*1024
# Source captures can exceed encoded FPS (and 300 s at 60 FPS is already
# 18,000 rows). This separate safety bound leaves pacing headroom without
# relaxing motion-row or byte limits or claiming encoded-frame identity.
FRAME_ROW_LIMIT=60000
spec=importlib.util.spec_from_file_location("wuwa_live",Path(__file__).with_name("wuwa-test.py"))
live=importlib.util.module_from_spec(spec)
spec.loader.exec_module(live)


def read_rows(path: Path, limit=12000, diagnostics=None):
    if diagnostics is None: diagnostics={}
    diagnostics.update(missing=not path.is_file(),bytes_read=0,invalid_rows=0,invalid_line_numbers=[])
    if diagnostics['missing']: return []
    if path.stat().st_size>SIDECAR_BYTE_LIMIT: raise ValueError("Recording sidecar exceeds size limit")
    rows=[]
    with path.open('rb') as source:
        line_number=0
        while True:
            line=source.readline(SIDECAR_BYTE_LIMIT+1)
            if not line: break
            line_number+=1
            start=diagnostics['bytes_read']
            diagnostics['bytes_read']+=len(line)
            if diagnostics['bytes_read']>SIDECAR_BYTE_LIMIT: raise ValueError("Recording sidecar exceeds size limit")
            try: row=json.loads(line)
            except (ValueError,UnicodeError): row=None
            if not isinstance(row,dict):
                diagnostics['invalid_rows']+=1
                if len(diagnostics['invalid_line_numbers'])<8: diagnostics['invalid_line_numbers'].append(line_number)
                continue  # Keep usable rows, but make the coverage gap explicit.
            rows.append(row)
            diagnostics['last_row_start_byte']=start
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


def compact_timeline(timeline):
    """Store each selected source observation once, without merging equal rows.

    Video timestamps can select the same camera sample several times. Repeating
    its constructor matrices in HTML can multiply the bounded sidecar size.
    References preserve the original nearest-sample selection and its age.
    """
    frames, samples, indices = [], [], {}
    for row in timeline:
        frame = {key: value for key, value in row.items() if key != 'sample'}
        sample = row['sample']
        index = None
        if sample is not None:
            key = id(sample)
            if key not in indices:
                indices[key] = len(samples)
                samples.append(sample)
            index = indices[key]
        frame['sample_index'] = index
        frames.append(frame)
    return {'version': 2, 'frames': frames, 'samples': samples}


def motion_coverage(rows, diagnostics):
    warnings=[]
    headers=[r for r in rows if r.get('type')=='header']
    endings=[r for r in rows if r.get('type')=='recording_end']
    ending=endings[0] if len(endings)==1 else None
    finalized=False
    if diagnostics.get('missing'):
        warnings.append('Motion sidecar is unavailable; camera/input coverage cannot be confirmed.')
    elif not endings:
        warnings.append('Motion recording has no recording_end marker; it may be an older recording or have stopped before finalization. Coverage is unconfirmed.')
    elif len(endings)!=1 or rows[-1] is not ending:
        warnings.append('Motion recording has multiple or nonterminal recording_end markers; coverage is unconfirmed.')
    elif (len(headers)!=1 or not isinstance(headers[0].get('id'),str) or not headers[0]['id']
          or headers[0]['id']!=ending.get('id')):
        warnings.append('Motion recording header/end identities do not match; coverage is unconfirmed.')
    else:
        counts={name:sum(r.get('type')==kind for r in rows) for name,kind in (('rows','frame'),('render_rows','render_stage0'))}
        fields_ok=(type(ending.get('version')) is int and ending['version']==1
                   and all(type(ending.get(name)) is int and ending[name]>=0 for name in (*counts,'bytes_before_footer','byte_limit'))
                   and 0<ending['byte_limit']<=MOTION_OUTPUT_BYTE_LIMIT
                   and type(ending.get('truncated')) is bool and type(ending.get('render_stage0_truncated')) is bool
                   and isinstance(ending.get('reason'),str) and isinstance(ending.get('error'),str))
        if not fields_ok:
            warnings.append('Motion recording end marker is invalid; coverage is unconfirmed.')
        elif (any(ending[name]!=count for name,count in counts.items())
              or ending['bytes_before_footer']!=diagnostics.get('last_row_start_byte')
              or diagnostics['bytes_read']>ending['byte_limit']):
            warnings.append('Motion recording end counts or byte accounting do not match the sidecar; coverage is unconfirmed.')
        else:
            finalized=True
    if diagnostics.get('invalid_rows'):
        warnings.append(f"Motion sidecar contains {diagnostics['invalid_rows']} unreadable row(s); usable observations were retained and coverage is partial.")
    truncated=ending.get('truncated') if ending and type(ending.get('truncated')) is bool else None
    if truncated:
        warnings.append('Motion recording was truncated ('+str(ending.get('reason','unknown'))+'); later camera/input or render observations may be missing.')
    if ending and ending.get('render_stage0_truncated') is True:
        warnings.append('Render-stage diagnostics reached their row cap; later render-stage observations are missing.')
    if ending and ending.get('error'):
        warnings.append('Motion writer reported: '+str(ending['error']))
    return dict(status='unavailable' if diagnostics.get('missing') else 'partial' if warnings else 'finalized',
                writer_finalized=finalized,truncated=truncated,end=ending,reader=diagnostics,warnings=warnings)


def make_replay(folder: Path):
    frame_reads,motion_reads={},{}
    frames=read_rows(folder/"frames.jsonl",limit=FRAME_ROW_LIMIT,diagnostics=frame_reads)
    motion=read_rows(folder/"motion.jsonl",diagnostics=motion_reads)
    timeline=correlate(frames,motion)
    coverage=motion_coverage(motion,motion_reads)
    warnings=list(coverage['warnings'])
    input_report = None
    if (folder/'input-sequence.json').is_file():
        input_report = json.loads((folder/'input-sequence.json').read_text(encoding='utf-8'))
        warnings.append('This recording includes a requested automated controller sequence. '
            'See input-sequence.json and the separate input_sequence observations in motion.jsonl. '
            'Compare raw, synthetic and delivered inputs; hook delivery does not prove a game action or menu change.')
    if frame_reads['missing']: warnings.append('Video frame timing sidecar is unavailable; timestamp matching is unavailable.')
    if frame_reads['invalid_rows']: warnings.append(f"Video frame timing sidecar contains {frame_reads['invalid_rows']} unreadable row(s); alignment coverage is partial.")
    # Data is embedded as JSON, never interpolated into executable JavaScript.
    payload=compact_timeline(timeline)
    data=json.dumps(payload,separators=(",",":"),ensure_ascii=True).replace("<","\\u003c")
    template=Path(__file__).with_name("recording-replay.html").read_text(encoding="utf-8")
    if warnings:
        notice='<aside id="recording-coverage" role="status"><strong>Recording coverage warning</strong><p>'+'</p><p>'.join(html.escape(warning) for warning in warnings)+'</p></aside>'
        template=template.replace('<video id="video"',notice+'<video id="video"',1)
    (folder/"replay.html").write_text(template.replace("__TIMELINE_JSON__",data),encoding="utf-8")
    report={"video_frames":len(timeline),"source_samples":len(timeline),
            "frame_count_note":"Counts submitted source captures; the encoder may repeat/drop frames at the target rate.",
            "matched_samples":sum(r["sample"] is not None for r in timeline),
            "embedded_unique_samples":len(payload['samples']),
            "embedded_data_bytes":len(data.encode('utf-8')),
            "alignment":"nearest wall-clock sample within 125 ms; not exact GPU frame synchronization",
            "motion_coverage":coverage,"warnings":warnings,"input_sequence":input_report}
    metadata_path = folder/'recording.json'
    if metadata_path.is_file():
        metadata = json.loads(metadata_path.read_text(encoding='utf-8'))
        report['purpose'] = metadata.get('purpose')
        report['input_context'] = metadata.get('input_context')
    (folder/"replay.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
    return report


def recorder_command(args, client):
    source=getattr(args,'source','auto')
    if source not in ('auto','steamvr','simulator'): raise ValueError('Choose auto, steamvr or simulator recording')
    runtime=None
    legacy_simulator=False
    if source != 'steamvr':
        path=client.simulator/'runtime_status.json'
        try:
            runtime=live.fresh_json(path)
            # Pre-video simulator builds publish a fresh preview status without
            # PID identity. Do not treat that as SteamVR, or capture an unbound
            # process. Give an actionable upgrade message before starting any
            # motion lease or encoder. Explicit SteamVR remains available when
            # another app owns this legacy preview.
            legacy_simulator=('runtime_pid' not in runtime and
                all(key in runtime for key in ('frame_count','preview_width','preview_height','session_state')) and
                path.stat().st_mtime >= client.created)
            if runtime.get('runtime_pid') != client.pid or path.stat().st_mtime < client.created:
                runtime=None
        except (OSError,ValueError,RuntimeError): runtime=None
    if legacy_simulator:
        raise RuntimeError('An older OpenXR Simulator is running without video capture. Close WuWa and its injector, '
            'choose Use this package\'s simulator in the launcher, then relaunch. '
            'Updating the WuWa backend alone does not update the active simulator. No runtime was changed.')
    if source=='simulator' or (source=='auto' and runtime is not None):
        if runtime is None: raise RuntimeError('No current simulator status for this game process')
        if runtime.get('video_stream_version') != 1:
            raise RuntimeError('This simulator needs the recording update; no runtime was changed')
        exe=Path(getattr(args,'simulator_recorder',ROOT/'dev-tools/wuwa-simulator-recorder.exe')).resolve()
        if not exe.is_file(): raise ValueError('wuwa-simulator-recorder.exe is missing')
        prefix=[str(exe)]; source='simulator'
    else:
        exe=args.recorder.resolve(); openvr=args.openvr.resolve()
        if not exe.is_file(): raise ValueError('wuwa-recorder.exe is missing')
        if not openvr.is_file(): raise ValueError('openvr_api.dll is missing')
        prefix=[str(exe),str(openvr)]; source='steamvr'
    return source, prefix+[str(client.pid),str(args.output.resolve()),str(args.seconds),
        str(args.fps),str(args.eye_width),str(args.stop_file.resolve())]


def record(args):
    if type(args.seconds) is not int or not 1<=args.seconds<=300: raise ValueError('Choose 1..300 seconds')
    if type(args.fps) is not int or args.fps not in (30,45,60): raise ValueError('Choose 30, 45 or 60 fps')
    if type(args.eye_width) is not int or args.eye_width not in (720,1024,1280): raise ValueError('Choose 720, 1024 or 1280 pixels per eye')
    if args.output.exists(): raise ValueError("Output already exists; preserve it and choose a new folder")
    sequence_module = plan = None
    if getattr(args, 'input_plan', None) is not None:
        if args.pid is None: raise ValueError('Input plans require an explicit --pid')
        if args.stop_file.exists(): raise ValueError('Stop file already exists; no input was requested')
        sequence_spec = importlib.util.spec_from_file_location('wuwa_input_sequence', Path(__file__).with_name('wuwa_input_sequence.py'))
        sequence_module = importlib.util.module_from_spec(sequence_spec)
        sequence_spec.loader.exec_module(sequence_module)
        plan = sequence_module.load_plan(args.input_plan, getattr(args, 'user_index', None), args.seconds, args.video_only)
    elif getattr(args, 'user_index', None) is not None:
        raise ValueError('--user-index requires --input-plan')
    client=live.LiveTest(profile=args.profile or live.PROFILE)
    if args.pid is not None and args.pid!=client.pid: raise ValueError("Game process changed")
    before=client.assert_live()
    if plan:
        sequence_module.check_backend(before, plan)
        client.require_foreground = True
        client.assert_focus()
    # Native helpers also bind to the live process. SteamVR refuses an absent
    # server or a different scene process; the simulator requires its own lease.
    recording=None
    with client.exclusive():
        source, command=recorder_command(args,client)
        purpose = 'content' if args.video_only else 'playtest'
        input_context = plan['context'] if plan else None
        before={**before,'recording_source':source,'recording_purpose':purpose,'input_context':input_context}
        if not args.video_only:
            if before.get("motion_recording",{}).get("active"): raise ValueError("A motion recording is already active")
            reply=client.request("record_motion",seconds=min(300,args.seconds+10))
            recording=reply.get("recording")
            if not recording or not recording.get("id"): raise RuntimeError("Backend did not confirm motion recording")
        error=None
        try:
            if plan:
                sequence_module.capture(client,args,command,plan)
            else:
                completed=subprocess.run(command,capture_output=True,text=True,
                    creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0),timeout=args.seconds+45)
                if completed.returncode: raise RuntimeError(completed.stderr.strip() or "Video recording failed")
        except (OSError,RuntimeError,subprocess.TimeoutExpired) as exc: error=str(exc)
        finally:
            if recording:
                try: client.request("record_motion",seconds=0,recording_id=recording["id"])
                except (OSError,RuntimeError,TimeoutError): pass  # Game exit closes the file; lease is bounded.
            if args.output.is_dir():
                (args.output/"before.json").write_text(json.dumps(before,indent=2),encoding="utf-8")
                metadata_path = args.output/'recording.json'
                if metadata_path.is_file():
                    metadata = json.loads(metadata_path.read_text(encoding='utf-8'))
                    metadata.update(purpose=purpose, input_context=input_context)
                    metadata_path.write_text(json.dumps(metadata,indent=2),encoding='utf-8')
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
    p.add_argument("--fps",type=int,choices=(30,45,60),default=30)
    p.add_argument("--eye-width",type=int,choices=(720,1024,1280),default=1024)
    p.add_argument("--source",choices=('auto','steamvr','simulator'),default='auto')
    p.add_argument("--pid",type=int)
    p.add_argument("--profile",type=Path)
    p.add_argument("--stop-file",type=Path)
    p.add_argument("--recorder",type=Path,default=ROOT/"dev-tools/wuwa-recorder.exe")
    p.add_argument("--simulator-recorder",type=Path,default=ROOT/"dev-tools/wuwa-simulator-recorder.exe")
    p.add_argument("--openvr",type=Path,default=ROOT/"dev-tools/openvr_api.dll")
    p.add_argument("--video-only",action="store_true",help="Clean video only for an older backend; no camera telemetry claim")
    p.add_argument('--input-plan',type=Path,help='Developer-only: bounded controller plan JSON with gameplay or game-menu context; requires live-control handover')
    p.add_argument('--user-index',type=int,choices=range(4),help='Connected physical Xbox slot for --input-plan; never inferred')
    p.add_argument("--rebuild-replay",action="store_true",help="Read existing video/sidecars without connecting to the game")
    args=p.parse_args(argv)
    if args.stop_file is None: args.stop_file=args.output.with_suffix('.stop')
    if args.rebuild_replay:
        if args.input_plan is not None or args.user_index is not None: raise ValueError('Replay rebuilding cannot request input')
        print(json.dumps(make_replay(args.output))); return 0
    print(record(args))
    return 0


if __name__=="__main__":
    try: sys.exit(main())
    except (OSError,RuntimeError,ValueError) as error: print(f"Recording stopped: {error}",file=sys.stderr); sys.exit(1)
