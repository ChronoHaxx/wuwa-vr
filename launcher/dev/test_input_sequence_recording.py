"""Offline failure-path checks. No game, controller or native recorder is used."""
import argparse
import contextlib
import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(file))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


m = module('sequence_test', 'wuwa_input_sequence.py')
r = module('sequence_recorder_test', 'record-wuwa.py')
PLAN = dict(version=1, segments=[dict(duration_ms=1000, lx=0, ly=0, rx=8192, ry=0)])


class SequenceTests(unittest.TestCase):
    def test_old_axes_plan_keeps_legacy_wire_shape_and_default_context(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'plan.json';path.write_text(json.dumps(PLAN))
            plan=m.load_plan(path,2,30)
            self.assertEqual(plan['context'],'gameplay')
            self.assertEqual(plan['segments'],PLAN['segments'])
            self.assertFalse(m.needs_trigger_axes(plan))
            m.check_backend({'input_sequence':{'protocol_version':1,'active':False}},plan)

    def test_trigger_press_release_preserves_explicit_zero_and_legacy_absence(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'plan.json'
            path.write_text(json.dumps(dict(version=1,segments=[
                dict(duration_ms=100,rt=255),dict(duration_ms=100,rt=0),
                dict(duration_ms=100,lt=1,rt=254),dict(duration_ms=100,lt=0),
                dict(duration_ms=100)])))
            plan=m.load_plan(path,0,30)
            self.assertTrue(m.needs_trigger_axes(plan))
            self.assertEqual([(s.get('lt'),s.get('rt')) for s in plan['segments']],
                             [(None,255),(None,0),(1,254),(0,None),(None,None)])
            self.assertEqual(plan['segments'][-1],dict(duration_ms=100,lx=0,ly=0,rx=0,ry=0))
            client=Mock();lease=m.Lease(client,plan)
            client.request.return_value={'input_sequence':dict(id=lease.id,active=True,phase='lead',
                generated_polls=0,context='gameplay',trigger_axes=True,synthetic_lt=0,synthetic_rt=0)}
            lease.begin()
            sent=client.request.call_args.kwargs['segments']
            self.assertEqual(sent,plan['segments'])
            self.assertIn('rt',sent[1]);self.assertEqual(sent[1]['rt'],0)
            self.assertIn('lt',sent[3]);self.assertEqual(sent[3]['lt'],0)
            self.assertNotIn('lt',sent[0]);self.assertNotIn('rt',sent[-1])

    def test_invalid_trigger_values_cannot_connect_to_game(self):
        with tempfile.TemporaryDirectory() as d:
            folder=Path(d);path=folder/'plan.json'
            args=argparse.Namespace(seconds=30,fps=30,eye_width=720,output=folder/'video',
                input_plan=path,user_index=0,pid=42,stop_file=folder/'stop',video_only=False)
            for axis in m.TRIGGERS:
                for value in [True,False,1.0,0.5,None,'255',-1,256,[],{}]:
                    path.write_text(json.dumps(dict(version=1,segments=[dict(duration_ms=100,**{axis:value})])))
                    with self.subTest(axis=axis,value=value),patch.object(r.live,'LiveTest') as connect:
                        with self.assertRaisesRegex(ValueError,'integer bytes'):r.record(args)
                        connect.assert_not_called()
                path.write_text(json.dumps(dict(version=1,context='game-menu',
                    segments=[dict(duration_ms=100,**{axis:1})])))
                with self.subTest(axis=axis),patch.object(r.live,'LiveTest') as connect:
                    with self.assertRaisesRegex(ValueError,'axes and triggers to be zero'):r.record(args)
                    connect.assert_not_called()
            path.write_text(json.dumps(dict(version=1,context='game-menu',segments=[dict(duration_ms=100,lt=0,rt=0)])))
            plan=m.load_plan(path,0,30)
            self.assertTrue(m.needs_trigger_axes(plan));self.assertTrue(m.needs_button_context(plan))
            self.assertEqual(plan['segments'][0]['lt'],0);self.assertEqual(plan['segments'][0]['rt'],0)

    def test_trigger_capability_required_even_for_explicit_zero_before_capture(self):
        with tempfile.TemporaryDirectory() as d:
            folder=Path(d);path=folder/'plan.json'
            args=argparse.Namespace(seconds=30,fps=30,eye_width=720,output=folder/'video',
                input_plan=path,user_index=0,pid=42,stop_file=folder/'stop',video_only=False,profile=folder/'profile')
            for axis in m.TRIGGERS:
                for value in [0,255]:
                    path.write_text(json.dumps(dict(version=1,segments=[dict(duration_ms=100,**{axis:value})])))
                    plan=m.load_plan(path,0,30)
                    for version in [None,True,False,0,2,'1',1.0]:
                        state=dict(protocol_version=1,active=False,button_context_version=1)
                        if version is not None:state['trigger_axis_version']=version
                        client=Mock(spec=r.live.LiveTest,pid=42)
                        client.assert_live.return_value={'input_sequence':state}
                        with self.subTest(axis=axis,value=value,version=version), \
                                patch.object(r.live,'LiveTest',return_value=client), \
                                patch.object(r,'recorder_command') as command, \
                                patch.object(r.subprocess,'run') as run, \
                                patch.object(r.subprocess,'Popen') as popen:
                            with self.assertRaisesRegex(RuntimeError,'trigger axes'):r.record(args)
                        command.assert_not_called();run.assert_not_called();popen.assert_not_called()
                        client.request.assert_not_called()
                    # Trigger-only gameplay does not silently require the separate
                    # button/menu capability. Each feature advertises its own support.
                    m.check_backend({'input_sequence':dict(protocol_version=1,active=False,trigger_axis_version=1)},plan)

    def test_trigger_context_echo_is_required_and_press_release_metadata_stays_separate(self):
        plan=dict(version=1,user_index=0,context='gameplay',segments=[
            dict(duration_ms=100,lx=0,ly=0,rx=0,ry=0,rt=255),
            dict(duration_ms=100,lx=0,ly=0,rx=0,ry=0,rt=0)])
        for context in [None,'game-menu']:
            lease=m.Lease(Mock(),plan)
            state=dict(id=lease.id,active=False,phase='completed',tail_neutral_observed=True,
                       generated_polls=10,segment_mask=3,trigger_axes=True,synthetic_lt=0,synthetic_rt=0)
            if context is not None:state['context']=context
            with self.subTest(context=context),self.assertRaisesRegex(RuntimeError,'input context'):
                lease.observe('heartbeat',{'input_sequence':state})
            self.assertFalse(lease.done);self.assertTrue(lease.report(None)['input_sent'])
        lease=m.Lease(Mock(),plan)
        for rt,done in [(255,False),(0,True)]:
            poll=dict(raw={'buttons':0,'lt':9,'rt':12},synthetic={'buttons':0,'lt':0,'rt':rt},
                      delivered={'buttons':0,'lt':0,'rt':0},generated=True)
            state=dict(id=lease.id,active=not done,phase='completed' if done else 'running',
                context='gameplay',trigger_axes=True,synthetic_lt=0,synthetic_rt=rt,
                generated_polls=10,segment_mask=3 if done else 1,tail_neutral_observed=done,samples=[poll])
            lease.observe('heartbeat',{'input_sequence':state})
        receipt=lease.report(None)
        self.assertTrue(receipt['completed']);self.assertEqual(receipt['input_context'],'gameplay')
        self.assertEqual([e['state']['synthetic_rt'] for e in receipt['events']],[255,0])
        self.assertEqual(receipt['events'][0]['state']['samples'][0]['raw']['rt'],12)
        self.assertEqual(receipt['events'][0]['state']['samples'][0]['synthetic']['rt'],255)
        self.assertEqual(receipt['events'][0]['state']['samples'][0]['delivered']['rt'],0)
        self.assertIn('not final game consumption',receipt['note'])

    def test_button_press_release_and_neutral_defaults_are_explicit(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'plan.json'
            path.write_text(json.dumps(dict(version=1,context='game-menu',segments=[
                dict(duration_ms=100,buttons=['right_shoulder','a']),
                dict(duration_ms=200,buttons=[]),dict(duration_ms=100,buttons=['dpad_down'])])))
            plan=m.load_plan(path,0,30)
            self.assertEqual(plan['context'],'game-menu')
            self.assertEqual(plan['duration_ms'],400)
            self.assertEqual(plan['segments'][0]['buttons'],['a','right_shoulder'])
            self.assertEqual(plan['segments'][1],dict(duration_ms=200,lx=0,ly=0,rx=0,ry=0))
            self.assertTrue(all(not s[axis] for s in plan['segments'] for axis in m.AXES))
            client=Mock();lease=m.Lease(client,plan)
            client.request.return_value={'input_sequence':dict(id=lease.id,active=True,phase='lead',generated_polls=0,
                                                               context='game-menu',synthetic_buttons=0)}
            lease.begin()
            self.assertEqual(client.request.call_args.kwargs['context'],'game-menu')
            self.assertEqual(client.request.call_args.kwargs['segments'],plan['segments'])
            report=lease.report(None)
            self.assertEqual(report['input_context'],'game-menu')
            self.assertEqual(report['purpose'],'playtest')
            self.assertIsNone(report['input_sent'])

    def test_bad_buttons_context_axes_and_duplicate_json_fields_are_rejected(self):
        invalid=[]
        for buttons in ['a',None,1,['A'],['left_thumb'],['guide'],['rt'],['a','a'],[True],[['a']],['a',{}]]:
            invalid.append(dict(version=1,segments=[dict(duration_ms=100,buttons=buttons)]))
        for context in [None,True,'menu','uevr','GAMEPLAY',{}]:
            invalid.append(dict(PLAN,context=context))
        for axis in m.AXES:
            invalid.append(dict(version=1,context='game-menu',segments=[dict(duration_ms=100,**{axis:1})]))
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'plan.json'
            for plan in invalid:
                path.write_text(json.dumps(plan))
                with self.subTest(plan=plan),self.assertRaises(ValueError):m.load_plan(path,0,30)
            for raw in ['{"version":1,"context":"gameplay","context":"game-menu","segments":[]}',
                        '{"version":1,"segments":[{"duration_ms":100,"buttons":[],"buttons":["a"]}]}']:
                path.write_text(raw)
                with self.assertRaisesRegex(ValueError,'duplicate JSON field'):m.load_plan(path,0,30)

    def test_extended_plan_needs_explicit_backend_capability(self):
        plans=[dict(version=1,context='game-menu',segments=[dict(duration_ms=100)]),
               dict(version=1,context='gameplay',segments=[dict(duration_ms=100,buttons=['a'])])]
        for plan in plans:
            for version in [None,True,0,2,'1']:
                state=dict(protocol_version=1,active=False,button_context_version=version)
                with self.subTest(plan=plan,version=version),self.assertRaisesRegex(RuntimeError,'buttons and menu contexts'):
                    m.check_backend({'input_sequence':state},plan)
            m.check_backend({'input_sequence':dict(protocol_version=1,active=False,button_context_version=1)},plan)

    def test_wrong_context_in_button_lease_response_cannot_complete(self):
        for context in [None,'gameplay']:
            lease=m.Lease(Mock(),dict(version=1,user_index=0,context='game-menu',segments=[dict(duration_ms=100,buttons=['a'])]))
            state=dict(id=lease.id,active=False,phase='completed',tail_neutral_observed=True,
                       generated_polls=10,segment_mask=1)
            if context is not None:state['context']=context
            with self.subTest(context=context),self.assertRaisesRegex(RuntimeError,'input context'):
                lease.observe('heartbeat',{'input_sequence':state})
            self.assertFalse(lease.done)
            self.assertTrue(lease.report(None)['input_sent'])
            self.assertEqual(lease.events[0]['state'],state)

    def test_old_backend_refuses_extended_plan_before_motion_or_video(self):
        with tempfile.TemporaryDirectory() as d:
            folder=Path(d);path=folder/'plan.json'
            path.write_text(json.dumps(dict(version=1,segments=[dict(duration_ms=100,buttons=['a'])])))
            args=argparse.Namespace(seconds=30,fps=30,eye_width=720,output=folder/'video',
                input_plan=path,user_index=0,pid=42,stop_file=folder/'stop',video_only=False,profile=folder/'profile')
            client=Mock(spec=r.live.LiveTest,pid=42)
            client.assert_live.return_value={'input_sequence':{'protocol_version':1,'active':False}}
            with patch.object(r.live,'LiveTest',return_value=client),patch.object(r,'recorder_command') as command, \
                    patch.object(r.subprocess,'run') as run:
                with self.assertRaisesRegex(RuntimeError,'buttons and menu contexts'):r.record(args)
            command.assert_not_called();run.assert_not_called();client.request.assert_not_called()

    def test_passive_recording_has_purpose_but_never_sends_sequence(self):
        for video_only,purpose in [(False,'playtest'),(True,'content')]:
            with self.subTest(video_only=video_only),tempfile.TemporaryDirectory() as d:
                folder=Path(d)
                args=argparse.Namespace(seconds=30,fps=30,eye_width=720,output=folder/'video',
                    input_plan=None,user_index=None,pid=42,stop_file=folder/'stop',video_only=video_only,profile=folder/'profile')
                client=Mock(spec=r.live.LiveTest,pid=42,profile=args.profile)
                client.assert_live.return_value={}
                client.exclusive.return_value=contextlib.nullcontext()
                client.request.return_value={'recording':{'id':'owned','path':str(args.profile/'recordings/owned.jsonl')}}
                def run(*unused,**kw):
                    args.output.mkdir()
                    (args.output/'recording.json').write_text('{"input_sent":false,"status":"recorded"}')
                    return SimpleNamespace(returncode=0)
                with patch.object(r.live,'LiveTest',return_value=client), \
                        patch.object(r,'recorder_command',return_value=('simulator',['fixture'])), \
                        patch.object(r.subprocess,'run',side_effect=run):r.record(args)
                self.assertTrue(all(c.args[0]=='record_motion' for c in client.request.call_args_list))
                if video_only:client.request.assert_not_called()
                client.assert_focus.assert_not_called()
                meta=json.loads((args.output/'recording.json').read_text())
                self.assertFalse(meta['input_sent'])
                self.assertIsNone(meta['input_context'])
                self.assertEqual(meta['purpose'],purpose)
                self.assertFalse((args.output/'input-sequence.json').exists())
                before=json.loads((args.output/'before.json').read_text())
                replay=json.loads((args.output/'replay.json').read_text())
                self.assertEqual(before['recording_purpose'],purpose)
                self.assertEqual(replay['purpose'],purpose)

    def test_invalid_plans_cannot_connect_to_game(self):
        invalid = [dict(PLAN, version=True), dict(PLAN, buttons=1), dict(PLAN, segments=[]),
                   dict(PLAN, segments=PLAN['segments'] * 9)]
        for field, value in [('duration_ms', 0), ('duration_ms', 10001), ('rx', True),
                             ('rx', 1.5), ('rx', -16385), ('button', 1)]:
            invalid.append(dict(PLAN, segments=[dict(PLAN['segments'][0], **{field:value})]))
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / 'plan.json'
            args = argparse.Namespace(seconds=30, fps=30, eye_width=720, output=Path(d)/'video',
                input_plan=path, user_index=0, pid=42, stop_file=Path(d)/'stop', video_only=False)
            for plan in invalid:
                path.write_text(json.dumps(plan))
                with self.subTest(plan=plan), patch.object(r.live, 'LiveTest') as connect:
                    with self.assertRaises(ValueError): r.record(args)
                    connect.assert_not_called()
            path.write_text(json.dumps(PLAN))
            self.assertEqual(m.load_plan(path, 0, 30)['duration_ms'], 1000)
            for slot, seconds, video_only in [(None,30,False),(True,30,False),(4,30,False),
                                             (0,12,False),(0,30,True)]:
                with self.assertRaises(ValueError): m.load_plan(path,slot,seconds,video_only)
            path.write_bytes(b' ' * 4097)
            with self.assertRaisesRegex(ValueError, '4096'): m.load_plan(path,0,30)

    def test_backend_requires_exact_protocol_and_no_active_lease(self):
        for state in [{},{'protocol_version':True},{'protocol_version':2},
                      {'protocol_version':1,'active':True}]:
            with self.assertRaises(RuntimeError): m.check_backend({'input_sequence':state})
        m.check_backend({'input_sequence':{'protocol_version':1,'active':False}})

    def test_begin_timeout_still_stops_exact_owned_id(self):
        client=Mock(); lease=m.Lease(client,dict(PLAN,user_index=2))
        client.request.side_effect=[TimeoutError('lost response'), {'input_sequence':{'id':lease.id}}]
        with self.assertRaises(TimeoutError): lease.begin()
        lease.stop()
        begin,stop=client.request.call_args_list
        self.assertEqual(begin.kwargs['request_id'],lease.id)
        self.assertEqual(stop.kwargs['lease_id'],lease.id)
        self.assertIsNone(lease.report(None)['input_sent'])
        self.assertFalse(lease.report(None)['completed'])

    def test_completed_requires_neutral_tail_and_every_segment_observed(self):
        for change in [{'tail_neutral_observed':False},{'segment_mask':0},{'id':'other'},
                       {'phase':'cancelled','reason':'physical_buttons'}]:
            lease=m.Lease(Mock(),dict(PLAN,user_index=0))
            state=dict(id=lease.id, active=False, phase='completed', tail_neutral_observed=True,
                       segment_mask=1, generated_polls=20)
            state.update(change)
            with self.subTest(change=change),self.assertRaises(RuntimeError):
                lease.observe('heartbeat',{'input_sequence':state})
            self.assertFalse(lease.done)
        lease=m.Lease(Mock(),dict(PLAN,user_index=0))
        lease.observe('heartbeat', {'input_sequence':dict(id=lease.id,active=False,phase='completed',
            tail_neutral_observed=True,segment_mask=1,generated_polls=20)})
        self.assertTrue(lease.done)
        self.assertTrue(lease.report(None)['input_sent'])

    def test_partial_frame_and_stale_lines_cannot_start_input(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d); self.assertIsNone(m.latest_frame(p))
            complete=dict(capture_begin_ms=100,capture_end_ms=101)
            (p/'frames.jsonl').write_text(json.dumps(complete)+'\n{"capture_begin_ms":102')
            self.assertEqual(m.latest_frame(p),complete)
            (p/'frames.jsonl').write_text('{"capture_begin_ms":102')
            self.assertIsNone(m.latest_frame(p))

    def fixture(self,d):
        p=Path(d); output=p/'video';output.mkdir()
        return SimpleNamespace(output=output,stop_file=p/'stop',seconds=30),Mock(spec=r.live.LiveTest)

    def test_video_failure_before_first_frame_never_begins(self):
        with tempfile.TemporaryDirectory() as d:
            args,client=self.fixture(d)
            process=Mock(returncode=1);process.poll.return_value=1
            with patch.object(m.subprocess,'Popen',return_value=process):
                with self.assertRaisesRegex(RuntimeError,'Video recording failed'):
                    m.capture(client,args,['fixture'],dict(PLAN,user_index=0))
            client.request.assert_not_called()
            self.assertFalse(json.loads((args.output/'input-sequence.json').read_text())['input_sent'])

    def test_lost_focus_cancels_owned_lease_and_encoder(self):
        with tempfile.TemporaryDirectory() as d:
            args,client=self.fixture(d)
            process=Mock(returncode=None);process.poll.return_value=None
            client.assert_focus.side_effect=[None,RuntimeError('focus lost')]
            def request(op,**kw):
                return {'input_sequence':dict(id=kw.get('request_id',kw.get('lease_id')),active=True,
                           phase='lead',generated_polls=0)}
            client.request.side_effect=request
            frame=dict(capture_begin_ms=100000,capture_end_ms=100000)
            with patch.object(m.subprocess,'Popen',return_value=process),patch.object(m,'latest_frame',return_value=frame), \
                    patch.object(m.time,'time',return_value=100),patch.object(m.time,'sleep'):
                with self.assertRaisesRegex(RuntimeError,'focus lost'):
                    m.capture(client,args,['fixture'],dict(PLAN,user_index=0))
            self.assertEqual([c.kwargs['action'] for c in client.request.call_args_list],['begin','stop'])
            self.assertTrue(args.stop_file.exists())
            process.wait.assert_called_once_with(timeout=3)
            self.assertIn('focus lost',json.loads((args.output/'input-sequence.json').read_text())['error'])

    def test_recording_metadata_does_not_claim_input_capture_was_passive(self):
        with tempfile.TemporaryDirectory() as d:
            folder=Path(d); native=dict(input_sent=False,status='recorded')
            (folder/'recording.json').write_text(json.dumps(native))
            receipt=dict(input_sent=True,completed=True)
            m.save_report(folder,receipt)
            meta=json.loads((folder/'recording.json').read_text())
            self.assertTrue(meta['input_sent']);self.assertFalse(meta['capture_helper_input_sent'])
            report=r.make_replay(folder)
            self.assertEqual(report['input_sequence'],receipt)
            self.assertIn('automated controller sequence',(folder/'replay.html').read_text())
            self.assertEqual(meta['purpose'],'playtest')
            self.assertEqual(meta['input_context'],'gameplay')

    def test_button_polls_do_not_relabel_synthetic_as_raw_or_game_action(self):
        client=Mock();plan=dict(version=1,user_index=0,context='game-menu',segments=[
            dict(duration_ms=100,lx=0,ly=0,rx=0,ry=0,buttons=['a']),
            dict(duration_ms=100,lx=0,ly=0,rx=0,ry=0)])
        lease=m.Lease(client,plan)
        for buttons,phase,mask,done in [(0x1000,'running',1,False),(0,'completed',3,True)]:
            state=dict(id=lease.id,active=not done,phase=phase,context='game-menu',synthetic_buttons=buttons,
                       generated_polls=10,segment_mask=mask,tail_neutral_observed=done)
            lease.observe('heartbeat',{'input_sequence':state})
        receipt=lease.report(None)
        self.assertTrue(receipt['completed'])
        self.assertEqual([e['state']['synthetic_buttons'] for e in receipt['events']],[0x1000,0])
        sample=dict(type='frame',unix_ms=100,input_sequence=dict(samples=[dict(
            raw={'buttons':0},synthetic={'buttons':0x1000},delivered={'buttons':0},generated=True)]))
        matched=r.correlate([dict(capture_begin_ms=100,capture_end_ms=101,video_seconds=0)],[sample])
        self.assertIs(matched[0]['sample'],sample)
        self.assertEqual(matched[0]['sample']['input_sequence']['samples'][0]['raw']['buttons'],0)
        self.assertEqual(matched[0]['sample']['input_sequence']['samples'][0]['synthetic']['buttons'],0x1000)
        self.assertEqual(matched[0]['sample']['input_sequence']['samples'][0]['delivered']['buttons'],0)
        self.assertIn('not final game consumption',receipt['note'])

    def test_normal_completion_heartbeats_until_tail_and_preserves_video(self):
        with tempfile.TemporaryDirectory() as d:
            args,client=self.fixture(d)
            clock=SimpleNamespace(now=100.0,started=None,lease=None)
            def sleep(seconds): clock.now+=seconds
            def poll():
                if clock.now<104: return None
                (args.output/'recording.json').write_text('{"input_sent":false}')
                return 0
            process=Mock(returncode=0);process.poll.side_effect=poll
            def request(op,**kw):
                if kw['action']=='begin':
                    clock.started=clock.now;clock.lease=kw['request_id']
                else: self.assertEqual(kw['lease_id'],clock.lease)
                done=clock.now-clock.started>=1.75
                return {'input_sequence':dict(id=clock.lease,active=not done,
                    phase='completed' if done else 'running',segment_mask=1,
                    tail_neutral_observed=done,generated_polls=10)}
            client.request.side_effect=request
            def frame(folder):
                return dict(capture_begin_ms=int(clock.now*1000),capture_end_ms=int(clock.now*1000))
            with patch.object(m.subprocess,'Popen',return_value=process),patch.object(m,'latest_frame',side_effect=frame), \
                    patch.object(m.time,'time',side_effect=lambda:clock.now), \
                    patch.object(m.time,'monotonic',side_effect=lambda:clock.now),patch.object(m.time,'sleep',side_effect=sleep):
                m.capture(client,args,['fixture'],dict(PLAN,user_index=0))
            report=json.loads((args.output/'input-sequence.json').read_text())
            self.assertTrue(report['completed']); self.assertTrue(report['input_sent'])
            self.assertFalse(args.stop_file.exists())
            process.terminate.assert_not_called()
            actions=[c.kwargs['action'] for c in client.request.call_args_list]
            self.assertEqual(actions[0],'begin');self.assertEqual(actions[-1],'stop')
            self.assertGreaterEqual(actions.count('heartbeat'),2)

    def test_known_request_identity_is_written_and_invalid_id_refused(self):
        with tempfile.TemporaryDirectory() as d:
            client=r.live.LiveTest.__new__(r.live.LiveTest)
            client.profile=Path(d);client.pid=42;client.assert_live=Mock()
            captured=[]
            def write(path,value): captured.append(value)
            with patch.object(r.live,'write_json',side_effect=write):
                with self.assertRaises(TimeoutError):
                    client.request('input_sequence',timeout=0,request_id='owned_test',action='begin')
            self.assertEqual(captured[0]['id'],'owned_test')
            for identity in ['',None,True,'bad space','x'*65]:
                if identity is None: continue  # None intentionally means generate a normal ID.
                with self.assertRaises(ValueError): client.request('input_sequence',request_id=identity)


if __name__=='__main__': unittest.main()
