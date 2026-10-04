"""Local developer playtest evidence. Standard library only; never controls hardware.

The immutable session definition and numbered, atomically committed event files
are authoritative. Reports are projections and can be rebuilt after interruption.
Thread/process locks serialize writers; path checks reject links and traversal
but are not a sandbox against another process with write access to the store.
"""

from contextlib import contextmanager
import copy
import hashlib
import html
import json
import os
from pathlib import Path
import re
import stat
import threading
import time
import uuid


STATUSES = ("not_tested", "pass", "fail", "blocked")
MAX_CHECKS = 128
MAX_EVENTS = 2000
MAX_SESSIONS = 1000
MAX_NOTE_CHARS = 16000
MAX_TIME_MS = 366 * 24 * 60 * 60 * 1000
_ID = re.compile(r"[A-Za-z0-9][A-Za-z0-9_-]{0,95}\Z")
_SESSION_ID = re.compile(r"[0-9a-f]{32}\Z")
_EVENT_FILE = re.compile(r"([0-9]{6})\.json\Z")
_LOCKS = {}
_LOCKS_GUARD = threading.Lock()


class PlaytestError(ValueError):
    """Invalid request or unsafe local path."""


class SessionConflict(PlaytestError):
    """A retry changed its payload, or the session no longer accepts an edit."""


class CorruptSessionError(PlaytestError):
    """Committed evidence is invalid; do not silently discard or repair it."""


def _json(value):
    return json.dumps(value, ensure_ascii=False, allow_nan=False,
                      sort_keys=True, separators=(",", ":"))


def _digest(value):
    return hashlib.sha256(_json(value).encode("utf-8")).hexdigest()


def _text(value, name, limit=MAX_NOTE_CHARS, nonempty=False):
    if not isinstance(value, str) or len(value) > limit or (nonempty and not value.strip()):
        raise PlaytestError(f"{name} must be {'nonempty ' if nonempty else ''}text, at most {limit} characters")
    try:
        value.encode("utf-8")
    except UnicodeEncodeError as exc:
        raise PlaytestError(f"{name} contains invalid Unicode") from exc
    return value


def _identifier(value, name="ID"):
    if not isinstance(value, str) or not _ID.fullmatch(value):
        raise PlaytestError(f"{name} must contain 1-96 ASCII letters, digits, underscores or hyphens")
    return value


def _offset(value, name, optional=True):
    if optional and value is None:
        return None
    if type(value) is not int or not 0 <= value <= MAX_TIME_MS:
        raise PlaytestError(f"{name} must be an integer between 0 and {MAX_TIME_MS} milliseconds")
    return value


def _json_copy(value, name, limit):
    def validate(item, depth=0):
        if depth > 24:
            raise PlaytestError(f"{name} is too deeply nested")
        if type(item) in (str, int, float, bool, type(None)):
            return
        if type(item) is list:
            for child in item:
                validate(child, depth + 1)
        elif type(item) is dict and all(type(key) is str for key in item):
            for child in item.values():
                validate(child, depth + 1)
        else:
            raise PlaytestError(f"{name} must use JSON types and string object keys")
    try:
        validate(value)
        encoded = _json(value).encode("utf-8")
        if len(encoded) > limit:
            raise PlaytestError(f"{name} exceeds {limit} bytes")
        # Round-trip rejects exotic objects and disconnects caller-owned values.
        return json.loads(encoded)
    except (TypeError, UnicodeError, ValueError, RecursionError) as exc:
        raise PlaytestError(f"{name} must be bounded, finite JSON data") from exc


def _definition(build, checks, language):
    if not isinstance(build, dict) or not build or any(not isinstance(k, str) for k in build):
        raise PlaytestError("build must be a nonempty JSON object with string keys")
    build = _json_copy(build, "build", 32768)
    if not isinstance(checks, list) or not 1 <= len(checks) <= MAX_CHECKS:
        raise PlaytestError(f"checks must contain 1-{MAX_CHECKS} items")
    checks = _json_copy(checks, "checks", 384 * 1024)
    ids = set()
    for check in checks:
        if not isinstance(check, dict) or set(check) != {"id", "title", "instructions"}:
            raise PlaytestError("Each check requires exactly id, title and instructions")
        item_id = _identifier(check["id"], "item ID")
        if item_id in ids:
            raise PlaytestError("Checklist item IDs must be unique")
        ids.add(item_id)
        for field in ("title", "instructions"):
            localized = check[field]
            if not isinstance(localized, dict) or "en" not in localized or not ({"zh", "zh-Hans"} & localized.keys()):
                raise PlaytestError(f"{field} needs en and zh-Hans (or zh) text")
            if set(localized) - {"en", "zh", "zh-Hans"}:
                raise PlaytestError(f"Unsupported {field} language")
            for value in localized.values():
                _text(value, field, 8000, nonempty=True)
    if language not in ("en", "zh-Hans", "zh"):
        raise PlaytestError("language must be en or zh-Hans")
    return build, checks, "zh-Hans" if language == "zh" else language


def _reject_link(path):
    try:
        info = path.lstat()
    except FileNotFoundError:
        return
    if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400):
        raise PlaytestError("Symlinks and reparse points are not allowed in the playtest store")
    if stat.S_ISREG(info.st_mode) and info.st_nlink > 1:
        raise PlaytestError("Hard-linked files are not allowed in the playtest store")


def _check_chain(path):
    for part in reversed((path, *path.parents)):
        _reject_link(part)


class SessionStore:
    """A bounded local store. All public mutation methods return a fresh snapshot.

    Supply stable event_id values for HTTP retries. Reusing an ID with different
    input raises SessionConflict. Timestamps are approximate, in milliseconds;
    video_ms requires a previously linked recording_id. Attachment IDs are opaque
    references only: this module neither opens nor transcribes their contents.
    """

    def __init__(self, root, clock_ms=None):
        self.root = Path(os.path.abspath(os.fspath(root)))
        _check_chain(self.root)
        self.root.mkdir(parents=True, exist_ok=True)
        _check_chain(self.root)
        if not self.root.is_dir():
            raise PlaytestError("Playtest root must be a directory")
        self.clock_ms = clock_ms or (lambda: time.time_ns() // 1_000_000)
        with _LOCKS_GUARD:
            self._thread_lock = _LOCKS.setdefault(os.path.normcase(str(self.root)), threading.RLock())

    def _path(self, *parts):
        path = self.root.joinpath(*parts)
        _check_chain(path)
        if not path.resolve().is_relative_to(self.root.resolve()):
            raise PlaytestError("Path escapes playtest root")
        return path

    @contextmanager
    def _locked(self):
        with self._thread_lock:
            path = self._path(".store.lock")
            flags = os.O_RDWR | os.O_CREAT | getattr(os, "O_BINARY", 0) | getattr(os, "O_NOFOLLOW", 0)
            fd = os.open(path, flags, 0o600)
            acquired = False
            try:
                if os.fstat(fd).st_size == 0:
                    os.write(fd, b"\0")
                    os.fsync(fd)
                os.lseek(fd, 0, os.SEEK_SET)
                if os.name == "nt":
                    import msvcrt
                    deadline = time.monotonic() + 10
                    while True:
                        try:
                            msvcrt.locking(fd, msvcrt.LK_NBLCK, 1)
                            break
                        except OSError:
                            if time.monotonic() >= deadline:
                                raise SessionConflict("Playtest store is busy; retry this request")
                            time.sleep(0.02)
                else:
                    import fcntl
                    fcntl.flock(fd, fcntl.LOCK_EX)
                acquired = True
                yield
            finally:
                if acquired:
                    if os.name == "nt":
                        import msvcrt
                        os.lseek(fd, 0, os.SEEK_SET)
                        msvcrt.locking(fd, msvcrt.LK_UNLCK, 1)
                    else:
                        import fcntl
                        fcntl.flock(fd, fcntl.LOCK_UN)
                os.close(fd)

    def _now(self):
        value = self.clock_ms()
        if type(value) is not int or value < 0:
            raise PlaytestError("clock_ms must return a nonnegative integer")
        return value

    def _atomic(self, path, text, exclusive=False):
        self._path(*path.relative_to(self.root).parts)
        if exclusive and path.exists():
            raise SessionConflict("Evidence file already exists")
        temporary = path.with_name(".tmp-" + uuid.uuid4().hex)
        flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_BINARY", 0) | getattr(os, "O_NOFOLLOW", 0)
        try:
            with os.fdopen(os.open(temporary, flags, 0o600), "wb") as stream:
                stream.write(text.encode("utf-8"))
                stream.flush()
                os.fsync(stream.fileno())
            self._path(*path.relative_to(self.root).parts)
            os.replace(temporary, path)
            # Windows has no standard-library directory fsync. File contents are
            # synced before replacement; POSIX also syncs the directory entry.
            if os.name != "nt":
                fd = os.open(path.parent, os.O_RDONLY)
                try:
                    os.fsync(fd)
                finally:
                    os.close(fd)
        finally:
            if temporary.exists():
                _reject_link(temporary)
                temporary.unlink()

    def _read_json(self, path, limit):
        self._path(*path.relative_to(self.root).parts)
        try:
            with path.open("rb") as stream:
                raw = stream.read(limit + 1)
            if len(raw) > limit:
                raise ValueError("Oversized evidence")
            return json.loads(raw, parse_constant=lambda value: (_ for _ in ()).throw(ValueError(value)))
        except (ValueError, UnicodeError, OSError, RecursionError) as exc:
            raise CorruptSessionError(f"Cannot read committed playtest evidence: {path.name}") from exc

    def _session_path(self, session_id):
        if not isinstance(session_id, str) or not _SESSION_ID.fullmatch(session_id):
            raise PlaytestError("Invalid session ID")
        path = self._path(session_id)
        if not path.is_dir() or not self._path(session_id, "session.json").is_file():
            raise PlaytestError("Unknown session ID")
        return path

    def _manifests(self):
        folders = []
        for path in self.root.iterdir():
            if _SESSION_ID.fullmatch(path.name):
                self._path(path.name)
                if path.is_dir() and self._path(path.name, "session.json").is_file():
                    folders.append(path.name)
        if len(folders) > MAX_SESSIONS:
            raise PlaytestError("Playtest session limit reached")
        return folders

    def start(self, build, checks, language="en", *, event_id=None):
        build, checks, language = _definition(build, checks, language)
        event_id = _identifier(uuid.uuid4().hex if event_id is None else event_id, "event ID")
        fingerprint = _digest(dict(build=build, checks=checks, language=language))
        with self._locked():
            sessions = self._manifests()
            active = False
            for session_id in sessions:
                manifest, state = self._load(session_id)
                if manifest["start_event_id"] == event_id:
                    if manifest["start_request_sha256"] != fingerprint:
                        raise SessionConflict("Start event ID was already used for different input")
                    return self._snapshot(state)
                active = active or state["state"] == "active"
            if active:
                raise SessionConflict("An active playtest already exists; resume or finish it first")
            if len(sessions) >= MAX_SESSIONS:
                raise PlaytestError("Playtest session limit reached")
            session_id = uuid.uuid4().hex
            folder = self._path(session_id)
            folder.mkdir()
            self._path(session_id, "events").mkdir()
            manifest = dict(schema_version=1, session_id=session_id,
                            created_at_ms=self._now(), language=language,
                            build=build, checks=checks, start_event_id=event_id,
                            start_request_sha256=fingerprint)
            self._atomic(folder / "session.json", _json(manifest), exclusive=True)
            return self._snapshot(self._load(session_id)[1])

    def _command(self, kind, *, item_id=None, status=None, note="", session_ms=None,
                 recording_id=None, video_ms=None, attachment_id=None, label=""):
        if kind not in ("result", "note", "recording", "finish"):
            raise PlaytestError("Invalid event type")
        if kind in ("result", "note"):
            _identifier(item_id, "item ID")
        elif item_id is not None:
            raise PlaytestError("Unexpected item ID")
        if (kind == "result" and status not in STATUSES) or (kind != "result" and status is not None):
            raise PlaytestError("Invalid explicit result status")
        _text(note, "note")
        _text(label, "recording label", 512)
        _offset(session_ms, "session_ms")
        _offset(video_ms, "video_ms")
        if recording_id is not None:
            _identifier(recording_id, "recording ID")
        if kind == "recording" and recording_id is None:
            raise PlaytestError("A recording ID is required")
        if video_ms is not None and recording_id is None:
            raise PlaytestError("video_ms requires recording_id")
        if attachment_id is not None:
            _identifier(attachment_id, "attachment ID")
            if kind != "note":
                raise PlaytestError("Attachments belong to note events")
        if kind == "note" and not note and attachment_id is None:
            raise PlaytestError("A note or attachment reference is required")
        return dict(type=kind, item_id=item_id, status=status, note=note,
                    session_ms=session_ms, recording_id=recording_id,
                    video_ms=video_ms, attachment_id=attachment_id, label=label)

    def _apply(self, state, event):
        if state["state"] != "active":
            raise SessionConflict("Finished sessions cannot be edited")
        items = {item["id"]: item for item in state["checks"]}
        recordings = {recording["id"] for recording in state["recordings"]}
        kind = event["type"]
        if kind in ("result", "note") and event["item_id"] not in items:
            raise PlaytestError("Unknown checklist item ID")
        if kind == "recording":
            if event["recording_id"] in recordings:
                raise SessionConflict("Recording ID is already linked")
            state["recordings"].append(dict(id=event["recording_id"], label=event["label"],
                                             session_ms=event["session_ms"], event_id=event["event_id"]))
        elif event["recording_id"] is not None and event["recording_id"] not in recordings:
            raise PlaytestError("Recording ID is not linked to this session")
        if kind == "result":
            items[event["item_id"]]["status"] = event["status"]
        if kind in ("result", "note") and (event["note"] or event["attachment_id"]):
            items[event["item_id"]]["notes"].append({key: event[key] for key in
                ("event_id", "note", "session_ms", "recording_id", "video_ms", "attachment_id")})
        if kind == "finish":
            state["state"] = "finished"
            state["finished_at_ms"] = event["at_ms"]
        state["events"].append(event)

    def _load(self, session_id):
        folder = self._session_path(session_id)
        manifest = self._read_json(folder / "session.json", 512 * 1024)
        try:
            if not isinstance(manifest, dict) or set(manifest) != {
                "schema_version", "session_id", "created_at_ms", "language", "build",
                "checks", "start_event_id", "start_request_sha256"}:
                raise ValueError("Invalid session definition")
            if type(manifest["schema_version"]) is not int or manifest["schema_version"] != 1 or manifest["session_id"] != session_id:
                raise ValueError("Session identity mismatch")
            build, checks, language = _definition(manifest["build"], manifest["checks"], manifest["language"])
            _identifier(manifest["start_event_id"], "start event ID")
            if type(manifest["created_at_ms"]) is not int or manifest["created_at_ms"] < 0:
                raise ValueError("Invalid creation time")
            if _digest(dict(build=build, checks=checks, language=language)) != manifest["start_request_sha256"]:
                raise ValueError("Session definition checksum mismatch")
            state = {key: copy.deepcopy(manifest[key]) for key in
                     ("schema_version", "session_id", "created_at_ms", "language", "build", "checks")}
            state.update(state="active", finished_at_ms=None, recordings=[], events=[])
            for check in state["checks"]:
                check.update(status="not_tested", notes=[])
            paths = []
            for path in self._path(session_id, "events").iterdir():
                if _EVENT_FILE.fullmatch(path.name):
                    paths.append(path)
                elif not re.fullmatch(r"\.tmp-[0-9a-f]{32}", path.name):
                    raise ValueError("Unexpected evidence file")
            if len(paths) > MAX_EVENTS:
                raise ValueError("Event limit exceeded")
            seen = {manifest["start_event_id"]}
            for sequence, path in enumerate(sorted(paths), 1):
                if path.name != f"{sequence:06d}.json":
                    raise ValueError("Missing event in committed journal")
                event = self._read_json(path, 128 * 1024)
                command_keys = {"type", "item_id", "status", "note", "session_ms", "recording_id", "video_ms", "attachment_id", "label"}
                if not isinstance(event, dict) or set(event) != command_keys | {"sequence", "event_id", "at_ms", "session_id", "request_sha256"}:
                    raise ValueError("Invalid event fields")
                self._command(event["type"], **{key: event[key] for key in command_keys - {"type"}})
                _offset(event["session_ms"], "session_ms", optional=False)
                _identifier(event["event_id"], "event ID")
                if event["event_id"] in seen or type(event["sequence"]) is not int or event["sequence"] != sequence or event["session_id"] != session_id:
                    raise ValueError("Event identity mismatch")
                if type(event["at_ms"]) is not int or event["at_ms"] < 0 or not isinstance(event["request_sha256"], str) or not re.fullmatch(r"[0-9a-f]{64}", event["request_sha256"]):
                    raise ValueError("Invalid event metadata")
                seen.add(event["event_id"])
                self._apply(state, event)
            return manifest, state
        except (ValueError, TypeError, KeyError, OSError, RecursionError) as exc:
            raise CorruptSessionError(f"Invalid committed evidence for session {session_id}") from exc

    def _snapshot(self, state):
        result = copy.deepcopy(state)
        result["counts"] = {status: sum(check["status"] == status for check in result["checks"]) for status in STATUSES}
        result["reports"] = ({"json": "report.json", "markdown": "report.md", "html": "report.html"}
                             if result["state"] == "finished" else None)
        for event in result["events"]:
            event.pop("request_sha256", None)
        return result

    def snapshot(self, session_id):
        with self._locked():
            snapshot = self._snapshot(self._load(session_id)[1])
            if snapshot["state"] == "finished":
                self._write_reports(snapshot)
            return snapshot

    def current(self):
        with self._locked():
            states = [self._load(session_id)[1] for session_id in self._manifests()]
            active = [state for state in states if state["state"] == "active"]
            if len(active) > 1:
                raise CorruptSessionError("Multiple active sessions require manual review")
            return self._snapshot(active[0]) if active else None

    def list_sessions(self, limit=50):
        if type(limit) is not int or not 1 <= limit <= MAX_SESSIONS:
            raise PlaytestError("limit must be an integer between 1 and 1000")
        with self._locked():
            states = [self._snapshot(self._load(session_id)[1]) for session_id in self._manifests()]
            states.sort(key=lambda state: (state["created_at_ms"], state["session_id"]), reverse=True)
            return [{key: state[key] for key in ("session_id", "state", "created_at_ms", "finished_at_ms", "language", "build", "counts")} for state in states[:limit]]

    def _record(self, session_id, command, event_id):
        event_id = _identifier(uuid.uuid4().hex if event_id is None else event_id, "event ID")
        fingerprint = _digest(command)
        with self._locked():
            manifest, state = self._load(session_id)
            if manifest["start_event_id"] == event_id:
                raise SessionConflict("Event ID was already used to start this session")
            for previous in state["events"]:
                if previous["event_id"] == event_id:
                    if previous["request_sha256"] != fingerprint:
                        raise SessionConflict("Event ID was already used for different input")
                    snapshot = self._snapshot(state)
                    if snapshot["state"] == "finished":
                        self._write_reports(snapshot)
                    return snapshot
            if command["type"] == "finish" and state["state"] == "finished":
                raise SessionConflict("Session is already finished; retry with the original event ID")
            # Reserve the final journal slot for finish so a bounded session can
            # always be closed and the next one started.
            if len(state["events"]) >= MAX_EVENTS - (0 if command["type"] == "finish" else 1):
                raise PlaytestError("Session event limit reached")
            now = self._now()
            event = dict(command, event_id=event_id, sequence=len(state["events"]) + 1,
                         session_id=session_id, at_ms=now, request_sha256=fingerprint)
            if event["session_ms"] is None:
                event["session_ms"] = min(MAX_TIME_MS, max(0, now - state["created_at_ms"]))
            self._apply(state, event)
            self._atomic(self._path(session_id, "events", f"{event['sequence']:06d}.json"), _json(event), exclusive=True)
            snapshot = self._snapshot(state)
            if snapshot["state"] == "finished":
                self._write_reports(snapshot)
            return snapshot

    def record_result(self, session_id, item_id, status, note="", *, event_id=None,
                      session_ms=None, recording_id=None, video_ms=None):
        return self._record(session_id, self._command("result", item_id=item_id, status=status,
            note=note, session_ms=session_ms, recording_id=recording_id, video_ms=video_ms), event_id)

    def add_note(self, session_id, item_id, note, *, event_id=None, session_ms=None,
                 recording_id=None, video_ms=None, attachment_id=None):
        return self._record(session_id, self._command("note", item_id=item_id, note=note,
            session_ms=session_ms, recording_id=recording_id, video_ms=video_ms,
            attachment_id=attachment_id), event_id)

    def link_recording(self, session_id, recording_id, *, label="", session_ms=None, event_id=None):
        return self._record(session_id, self._command("recording", recording_id=recording_id,
            label=label, session_ms=session_ms), event_id)

    def finish(self, session_id, note="", *, event_id=None):
        return self._record(session_id, self._command("finish", note=note), event_id)

    def _write_reports(self, snapshot):
        for extension, content in self._reports(snapshot).items():
            path = self._path(snapshot["session_id"], "report." + extension)
            # Avoid rewriting identical reports on every refresh.
            if path.exists():
                with path.open("rb") as stream:
                    existing = stream.read(len(content.encode("utf-8")) + 1)
                if existing == content.encode("utf-8"):
                    continue
            self._atomic(path, content)

    def report(self, session_id, format="html"):
        extension = {"html": "html", "json": "json", "markdown": "md", "md": "md"}.get(format)
        if extension is None:
            raise PlaytestError("Report format must be html, json or markdown")
        with self._locked():
            snapshot = self._snapshot(self._load(session_id)[1])
            if snapshot["state"] == "finished":
                self._write_reports(snapshot)
            return self._reports(snapshot)[extension]

    @staticmethod
    def _reports(snapshot):
        # Keep all external text in escaped HTML text nodes and dynamically sized
        # Markdown code fences. No path, URL, raw HTML or script is interpolated.
        def esc(value):
            return html.escape(str(value), quote=True)

        def fence(value):
            value = str(value)
            longest = max((len(match.group()) for match in re.finditer(r"`+", value)), default=0)
            delimiter = "`" * max(3, longest + 1)
            return f"{delimiter}\n{value}\n{delimiter}\n"

        summary = ", ".join(f"{key}: {value}" for key, value in snapshot["counts"].items())
        intro = "Results are explicit tester selections. Not tested and blocked items remain unaccepted. Times are approximate; local recording and attachment IDs are references only."
        sections = ["<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">",
                    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">",
                    "<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; style-src 'unsafe-inline'; base-uri 'none'; form-action 'none'\">",
                    "<title>WuWa VR playtest report</title><style>body{font:16px system-ui;max-width:960px;margin:2rem auto;padding:0 1rem}pre{white-space:pre-wrap;overflow-wrap:anywhere;background:#f3f4f6;padding:1rem}section{border-top:1px solid #bbb;margin-top:2rem}code{overflow-wrap:anywhere}</style>",
                    "</head><body><h1>WuWa VR playtest report</h1>",
                    f"<p>Session <code>{esc(snapshot['session_id'])}</code> — {esc(snapshot['state'])}</p>",
                    f"<p>{esc(summary)}</p><p>{esc(intro)}</p>",
                    f"<h2>Exact build identity</h2><pre>{esc(json.dumps(snapshot['build'], ensure_ascii=False, indent=2))}</pre>"]
        markdown = ["# WuWa VR playtest report\n", fence(snapshot["session_id"] + " — " + snapshot["state"]),
                    summary + "\n", intro + "\n", "## Exact build identity\n", fence(json.dumps(snapshot["build"], ensure_ascii=False, indent=2))]
        for check in snapshot["checks"]:
            sections.append(f"<section><h2>{esc(check['id'])} — {esc(check['status'])}</h2>")
            markdown.extend(["## Checklist item\n", fence(check["id"] + " — " + check["status"])])
            for language in ("en", "zh-Hans", "zh"):
                if language in check["title"]:
                    title = check["title"][language]
                    instructions = check["instructions"].get(language, check["instructions"].get("zh-Hans", check["instructions"].get("zh", "")))
                    sections.append(f"<h3>{esc(language)}: {esc(title)}</h3><pre>{esc(instructions)}</pre>")
                    markdown.extend([f"### {language}\n", fence(title), fence(instructions)])
            sections.append("</section>")
        sections.append("<section><h2>Recording references</h2>")
        markdown.append("## Recording references\n")
        for recording in snapshot["recordings"]:
            text = f"{recording['id']} | session ~{recording['session_ms']} ms\n{recording['label']}"
            sections.append(f"<pre>{esc(text)}</pre>")
            markdown.append(fence(text))
        sections.append("</section><section><h2>Event evidence and exact feedback</h2>")
        markdown.append("## Event evidence and exact feedback\n")
        for event in snapshot["events"]:
            description = f"#{event['sequence']} {event['type']} | {event['event_id']} | session ~{event['session_ms']} ms"
            if event["item_id"]:
                description += f" | item {event['item_id']}"
            if event["status"]:
                description += f" | {event['status']}"
            if event["recording_id"]:
                description += f" | recording {event['recording_id']}"
            if event["video_ms"] is not None:
                description += f" | video ~{event['video_ms']} ms"
            if event["attachment_id"]:
                description += f" | attachment {event['attachment_id']} (reference only; transcription not implied)"
            sections.extend([f"<p>{esc(description)}</p>", f"<pre>{esc(event['note'])}</pre>"])
            markdown.extend([fence(description), fence(event["note"])])
        sections.append("</section></body></html>")
        return {"json": json.dumps(snapshot, ensure_ascii=False, allow_nan=False, indent=2) + "\n",
                "html": "\n".join(sections), "md": "\n".join(markdown)}
