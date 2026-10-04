"""Developer-only playtest controller. No game, microphone or model starts on import."""
from pathlib import Path
import html
import importlib.util
import json
import re
import sys
import threading
import time


def _module(name):
    key = '_wuwa_developer_' + name
    if key not in sys.modules:
        spec = importlib.util.spec_from_file_location(key, Path(__file__).with_name(name + '.py'))
        module = importlib.util.module_from_spec(spec)
        sys.modules[key] = module
        try:
            spec.loader.exec_module(module)
        except Exception:
            sys.modules.pop(key, None)
            raise
    return sys.modules[key]


CHECKS = [
    {'id': 'launch', 'title': {'en': 'Launch and exit', 'zh-Hans': '启动与退出'},
     'instructions': {'en': 'Confirm the intended build, actual VR in both eyes and normal exit. Launcher success alone is not a pass.',
                      'zh-Hans': '确认目标版本、双眼实际 VR 画面及正常退出。启动器提示成功不等于通过。'}},
    {'id': 'stereo', 'title': {'en': 'Stereo, distant objects and lighting', 'zh-Hans': '双眼、远处物体与光照'},
     'instructions': {'en': 'Compare the same tree/prop near and far in both eyes, then NPC/enemy lighting. Note exact location and what differs.',
                      'zh-Hans': '在近处和远处对比同一树木或物体的双眼画面，再检查 NPC/敌人光照。记录位置及差异。'}},
    {'id': 'menus', 'title': {'en': 'Menus and cinematic return', 'zh-Hans': '菜单与过场返回'},
     'instructions': {'en': 'Check ESC, map, Resonators, Weapon/Echo submenus and a character-trial ultimate returning to play. Separate known reflection issues from new regressions.',
                      'zh-Hans': '检查 ESC、地图、共鸣者、武器/声骸子菜单和角色试用大招返回。区分已知反射问题与新回归。'}},
    {'id': 'portal', 'title': {'en': 'Portal shortcut and normal controls', 'zh-Hans': '空间窗口快捷键与普通操作'},
     'instructions': {'en': 'With UEVR settings closed, test F7 and L3+LT, hold/release, focus changes and saved portal settings. Confirm normal movement and menus afterwards.',
                      'zh-Hans': '关闭 UEVR 设置，测试 F7、L3+LT、长按/松开、切换焦点及设置保存，然后检查普通移动与菜单。'}},
    {'id': 'recording', 'title': {'en': 'Recording and recovery', 'zh-Hans': '录制与恢复'},
     'instructions': {'en': 'Review a short stereo recording and its replay. Test recovery only with the game/injector closed. Untested items remain Not tested.',
                      'zh-Hans': '检查一段短双眼录像及回放。仅在游戏/注入器关闭后测试恢复。未验证项保持“未测试”。'}},
    {'id': 'hands', 'title': {'en': 'Optional bare-hand demo', 'zh-Hans': '可选裸手演示'},
     'instructions': {'en': 'Only when deliberately enabled on a supported OpenXR runtime: check both hands and fingers in both eyes, move your head, hide/reveal hands, and disable again. Confirm Xbox controls remain normal. This is an unoccluded skeleton display, without grabbing or gesture gameplay. Otherwise leave Not tested.',
                      'zh-Hans': '仅在支持的 OpenXR 运行时中主动启用后：检查双眼中的双手和手指，移动头部，遮住/露出手，再关闭演示。确认 Xbox 操作正常。这里只显示不受场景遮挡的骨架，不含抓取或手势玩法；未验证时保持“未测试”。'}},
]


class Service:
    def __init__(self, data, inspect_status, recording_snapshot, *, store=None, voice=None, clock_ms=None):
        self.root = Path(data) / 'playtests'
        self.inspect_status = inspect_status
        self.recording_snapshot = recording_snapshot
        self.clock_ms = clock_ms or (lambda: int(time.time() * 1000))
        self.lock = threading.RLock()
        self.store = store if store is not None else _module('wuwa_playtest').SessionStore(self.root / 'sessions')
        self.voice = voice if voice is not None else _module('wuwa_voice_notes').VoiceNoteManager(
            self.root / 'voice', activity_probe=self._transcription_activity)
        self.settings_path = self.root / 'transcription.json'

    def _game_idle(self):
        state = self.inspect_status()
        # Unknown activity is not permission to start heavy transcription.
        return (state.get('gameRunning') is False and state.get('injectorRunning') is False
                and state.get('job', {}).get('running') is False
                and state.get('recording', {}).get('running') is False)

    def _transcription_activity(self, session_id):
        return {'playtest_stopped': self.store.snapshot(session_id)['state'] == 'finished',
                'gameplay_idle': self._game_idle()}

    def _settings(self):
        try:
            value = json.loads(self.settings_path.read_text(encoding='utf-8'))
            if not isinstance(value, dict):
                return {}
            return {k: v for k, v in value.items() if k in ('whisper_cli', 'model_path') and isinstance(v, str)}
        except (OSError, ValueError):
            return {}

    def state(self, session_id=None):
        with self.lock:
            session = self.store.snapshot(session_id) if session_id else self.store.current()
            return {'session': session, 'history': self.store.list_sessions(limit=20),
                    'voice': self.voice.snapshot(),
                    'notes': self.voice.list_notes(session['session_id']) if session else [],
                    'transcription': self._settings()}

    @staticmethod
    def _text(body, key, maximum=4000, optional=False):
        value = body.get(key, '' if optional else None)
        if not isinstance(value, str) or len(value) > maximum or (not optional and not value):
            raise ValueError('Invalid ' + key)
        return value

    def _session(self, body, active=False):
        session = self.store.snapshot(self._text(body, 'session_id', 128))
        if active and session['state'] != 'active':
            raise ValueError('This playtest is finished. Start a new session for new results.')
        return session

    def _item(self, session, body):
        item = self._text(body, 'item_id', 128)
        if item not in {check['id'] for check in session['checks']}:
            raise ValueError('Choose a checklist item in this session.')
        return item

    def _stamp(self, session):
        return max(0, self.clock_ms() - session['created_at_ms'])

    def _ensure_voice_idle(self):
        state = self.voice.snapshot()
        if state.get('active') or state.get('transcribing'):
            raise ValueError('Finish the voice note or transcription before changing this session.')

    def action(self, action, body):
        with self.lock:
            if action == 'start':
                request = {'event_id': body.get('event_id'), 'build_id': body.get('build_id') or None,
                           'language': body.get('language', 'en')}
                # A lost HTTP response must return the original selection, even
                # if the user has since switched runtime/build in another tab.
                if request['event_id']:
                    for entry in self.store.list_sessions(limit=1000):
                        prior = entry['build'].get('start_request', {})
                        if prior.get('event_id') == request['event_id']:
                            if prior != request:
                                raise ValueError('Event ID already used for different start input.')
                            return self.state(entry['session_id'])
                self._ensure_voice_idle()
                state = self.inspect_status()
                selected = body.get('build_id') or state.get('selected') or state.get('package', {}).get('defaultBuild')
                candidates = [b for b in state.get('builds', []) if b.get('id') == selected]
                if len(candidates) != 1:
                    raise ValueError('Select a known package build before starting a playtest.')
                build = {'id': candidates[0]['id'], 'name': candidates[0].get('name', selected),
                         'backend_sha256': candidates[0].get('sha256', ''),
                         'package_id': state.get('package', {}).get('id', ''),
                         'runtime': state.get('openxr', {}).get('name', ''),
                         'applied_at_start': state.get('selected') == selected,
                         'injector_selection_matches_at_start': state.get('selectionMatches') is True,
                         'start_request': request,
                         'identity_note': 'Package selection captured at session start; not proof of a loaded DLL or correct VR.'}
                session = self.store.start(build, CHECKS, language=body.get('language', 'en'), event_id=body.get('event_id'))
            elif action in ('result', 'note'):
                session = self._session(body)
                item = self._item(session, body)
                note = self._text(body, 'note', optional=True)
                # Let the store stamp first receipt so retries keep the same payload.
                args = dict(event_id=body.get('event_id'), recording_id=body.get('recording_id'), video_ms=body.get('video_ms'))
                if action == 'result':
                    session = self.store.record_result(session['session_id'], item, body.get('status'), note, **args)
                else:
                    session = self.store.add_note(session['session_id'], item, note, **args)
            elif action == 'link-recording':
                session = self._session(body)
                previous = next((event for event in session['events'] if event['event_id'] == body.get('event_id')), None)
                if previous:
                    if previous['type'] != 'recording':
                        raise ValueError('Event ID already used for a different action.')
                    return self.state(session['session_id'])
                recording = self.recording_snapshot()
                if not recording.get('recording_id'):
                    raise ValueError('Start or finish a recording in this launcher before linking it.')
                session = self.store.link_recording(session['session_id'], recording['recording_id'],
                    label='Launcher recording; synchronization is approximate', event_id=body.get('event_id'))
            elif action == 'finish':
                self._ensure_voice_idle()
                session = self._session(body)
                session = self.store.finish(session['session_id'], self._text(body, 'note', optional=True), event_id=body.get('event_id'))
            elif action == 'devices':
                return {'devices': self.voice.devices()}
            elif action == 'voice-start':
                session = self._session(body, active=True)
                item = self._item(session, body)
                event_id = self._text(body, 'event_id', 96)
                if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_-]{0,95}', event_id):
                    raise ValueError('Invalid event_id')
                if body.get('consent') is not True:
                    raise ValueError('Confirm microphone recording for each new voice note.')
                message = 'Voice note requested on microphone ' + str(body.get('device_id')) + '; no result inferred.'
                previous = next((event for event in session['events'] if event['event_id'] == event_id), None)
                if previous:
                    if previous['type'] != 'note' or previous['item_id'] != item or previous['note'] != message or not previous.get('attachment_id'):
                        raise ValueError('Event ID already used for different input.')
                    return self.state(session['session_id'])
                self._ensure_voice_idle()
                note = self.voice.start(session['session_id'], body.get('device_id'), consent=body.get('consent') is True)
                try:
                    session = self.store.add_note(session['session_id'], item, message,
                        attachment_id=note['id'], event_id=event_id)
                except Exception:
                    self.voice.cancel(note['id'])
                    raise
            elif action in ('voice-stop', 'voice-cancel'):
                session = self._session(body)
                note_id = self._text(body, 'note_id', 128)
                self.voice.note(session['session_id'], note_id)
                (self.voice.stop if action == 'voice-stop' else self.voice.cancel)(note_id)
            elif action == 'transcription-settings':
                self._ensure_voice_idle()
                values = {key: self._text(body, key, 1024) for key in ('whisper_cli', 'model_path')}
                # These are explicit local user choices, never taken from transcript text.
                for value in values.values():
                    if not Path(value).is_absolute() or not Path(value).is_file():
                        raise ValueError('Choose existing absolute local paths for whisper-cli and its model.')
                self.root.mkdir(parents=True, exist_ok=True)
                temporary = self.settings_path.with_suffix('.tmp')
                temporary.write_text(json.dumps(values), encoding='utf-8')
                temporary.replace(self.settings_path)
                return self.state(body.get('session_id'))
            elif action == 'transcribe':
                session = self._session(body)
                settings = self._settings()
                if set(settings) != {'whisper_cli', 'model_path'}:
                    raise ValueError('Configure a local whisper-cli executable and model first. Nothing is downloaded automatically.')
                self.voice.transcribe(session['session_id'], self._text(body, 'note_id', 128),
                    **settings, playtest_stopped=session['state'] == 'finished', gameplay_idle=self._game_idle(),
                    language='zh' if session['language'] == 'zh-Hans' else 'en')
            else:
                raise ValueError('Unknown playtest action')
            return self.state(session['session_id'])

    def report(self, session_id):
        # The core report contains exact observations; voice remains explicitly unverified.
        with self.lock:
            document = self.store.report(session_id, format='html')
            notes = self.voice.list_notes(session_id)
            extra = ['<section><h2>Voice notes / 语音备注</h2><p>Local transcription is unverified. No checklist result is inferred from speech.</p>']
            for note in notes:
                extra += ['<article><h3>' + html.escape(note['id']) + '</h3><p>' + html.escape(note['status']) + '</p>']
                if note.get('transcript'):
                    extra += ['<blockquote>' + html.escape(note['transcript'].get('text', '')) + '</blockquote>']
                if note.get('error'):
                    extra += ['<p>' + html.escape(note['error']) + '</p>']
                extra += ['</article>']
            extra += ['</section>']
            return document.replace('</body>', ''.join(extra) + '</body>')

    def close(self):
        self.voice.close()
