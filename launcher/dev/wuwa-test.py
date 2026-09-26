"""Bounded graphics/input tests with simulator or SteamVR mirror capture.

Does not attach a debugger, send window input, enumerate CVars, or edit config.
"""
from __future__ import annotations

import argparse
import contextlib
import ctypes
from ctypes import wintypes
import hashlib
import html
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
import uuid

PROFILE = Path(os.environ.get("APPDATA", "")) / "UnrealVRMod/Client-Win64-Shipping"
SIMULATOR = Path(os.environ.get("LOCALAPPDATA", "")) / "OpenXR-Simulator"
TOOLS = Path(__file__).resolve().parent.parent / "dev-tools"
REFLECTION_SOURCES = (
    ("planar", "Planar reflections", "r.Kuro.EnablePlanarReflection"),
    ("screen-space", "Screen-space reflections", "r.SSR.Quality"),
    ("environment", "Reflection environment", "r.ReflectionEnvironment"),
)
TRANSLUCENCY_SOURCES = (
    ("full-resolution", "Full-resolution translucent effects", "r.KuroDownsampleTranslucencyFullRes", 1),
    ("without-blur", "Translucency without the separate blur", "r.Kuro.SeparateTranslucencyBlur", 0),
)
STEREO_SOURCES = tuple((folder, title, name, 0) for folder, title, name in REFLECTION_SOURCES) + TRANSLUCENCY_SOURCES


def foreground_pid() -> int:
    user = ctypes.WinDLL("user32", use_last_error=True)
    user.GetForegroundWindow.restype = wintypes.HWND
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    pid = wintypes.DWORD()
    user.GetWindowThreadProcessId(user.GetForegroundWindow(), ctypes.byref(pid))
    return pid.value


def read_json(path: Path) -> dict:
    # Simulator status writes are not atomic. Retry a partially written JSON.
    for attempt in range(4):
        try:
            return json.loads(path.read_text(encoding="utf-8-sig"))
        except (json.JSONDecodeError, PermissionError):
            if attempt == 3:
                raise
            time.sleep(0.05)
    raise AssertionError("unreachable")


def write_json(path: Path, data: dict) -> None:
    temporary = path.with_name(path.name + ".client-tmp")
    temporary.write_text(json.dumps(data, indent=2), encoding="utf-8")
    temporary.replace(path)


def fresh_json(path: Path, max_age: float = 6.0) -> dict:
    age = time.time() - path.stat().st_mtime
    if not -1 <= age <= max_age:
        raise RuntimeError(f"Stale status ({age:.1f}s): {path}")
    return read_json(path)


def verify_process(pid: int) -> float:
    """Read-only Windows process identity/liveness check; returns creation time."""
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    kernel.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD,
                                               wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
    kernel.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
    handle = kernel.OpenProcess(0x100000 | 0x1000, False, pid)
    if not handle:
        raise RuntimeError(f"Cannot verify game PID {pid}: {ctypes.get_last_error()}")
    try:
        if kernel.WaitForSingleObject(handle, 0) != 258:
            raise RuntimeError(f"Game PID {pid} is no longer running")
        image = ctypes.create_unicode_buffer(32768)
        size = wintypes.DWORD(len(image))
        if not kernel.QueryFullProcessImageNameW(handle, 0, image, ctypes.byref(size)):
            raise RuntimeError("Cannot read process identity")
        if Path(image.value).name.lower() != "client-win64-shipping.exe":
            raise RuntimeError("Backend status refers to a different application")
        created, exited, kern, user = [wintypes.FILETIME() for _ in range(4)]
        if not kernel.GetProcessTimes(handle, *[ctypes.byref(x) for x in (created, exited, kern, user)]):
            raise RuntimeError("Cannot read game creation time")
        return ((created.dwHighDateTime << 32) | created.dwLowDateTime) / 10_000_000 - 11644473600
    finally:
        kernel.CloseHandle(handle)


def validate_capture(status: dict, *, layer: str, frame_before: int,
                     status_mtime: float, image_mtime: float, requested_at: float,
                     client_id: str | None = None, pid: int | None = None) -> None:
    if status.get("ok") is False:
        raise RuntimeError(f"Simulator rejected capture: {status.get('error', 'unknown error')}")
    if client_id is not None and "client_id" in status and status["client_id"] != client_id:
        raise RuntimeError("Capture belongs to another request")
    if pid is not None and "runtimePid" in status and status["runtimePid"] != pid:
        raise RuntimeError("Capture belongs to another runtime process")
    if status_mtime < requested_at or image_mtime < requested_at:
        raise RuntimeError("Capture files predate this request")
    if status.get("layer") != layer or status.get("eye") != "both":
        raise RuntimeError("Capture layer/eyes do not match the request")
    kind = status.get("captureKind")
    if layer != "all" and (kind != "layer-isolated-preview" or
                           not status.get("client_id") or not status.get("runtimePid")):
        raise RuntimeError("Simulator does not verify isolated-layer captures; use the patched runtime")
    if layer == "all" and kind not in (None, "composited-preview"):
        raise RuntimeError("Capture is not the combined preview")
    # Legacy composite captures can lag by two readback slots. Patched captures
    # bind the request to its recording slot and may never borrow an older frame.
    if status.get("requestFrame", -1) <= frame_before or status.get("capturedFrame", -1) <= frame_before:
        raise RuntimeError("Capture belongs to an older frame")
    allowed_lag = 0 if kind else 2
    if status.get("capturedFrame", -1) < status.get("requestFrame", 0) - allowed_lag:
        raise RuntimeError("Capture predates its recording request" if kind else
                           "Capture is more than two frames behind its request")
    if status.get("width", 0) < 2 or status.get("height", 0) < 1:
        raise RuntimeError("Capture has invalid dimensions")


def png_dimensions(path: Path) -> tuple[int, int]:
    """Check the native encoder's PNG envelope without requiring Pillow."""
    with path.open("rb") as file:
        header = file.read(33)
        file.seek(-12, 2)
        end = file.read()
    if (len(header) != 33 or header[:16] != b"\x89PNG\r\n\x1a\n\0\0\0\rIHDR"
            or end != b"\0\0\0\0IEND\xaeB`\x82"):
        raise RuntimeError("Native mirror capture is not a complete PNG")
    return struct.unpack(">II", header[16:24])


def validate_mirrors(output: Path, *, pid: int, token: str, requested_at: float) -> dict:
    receipt = output / "mirror.json"
    status = read_json(receipt)
    if status.get("version") != 1 or status.get("pid") != pid or status.get("client_id") != token:
        raise RuntimeError("SteamVR capture belongs to another process or request")
    if (status.get("captureKind") != "steamvr-composited-mirrors" or status.get("layer") != "all"
            or status.get("eye") != "both" or status.get("same_instant") is not False
            or status.get("source_frame_binding") != "unavailable_public_mirror_api"
            or status.get("settings_changed") is not False or status.get("input_sent") is not False):
        raise RuntimeError("Unexpected SteamVR mirror capture contract")
    frames = [status.get(key) for key in ("frame_initial", "frame_before", "frame_after")]
    if any(type(v) is not int or not 0 <= v <= 0xffffffff for v in frames):
        raise RuntimeError("Missing compositor timing evidence")
    initial, before, after = frames
    if not 2 <= (before - initial) % 2**32 < 2**31 or (after - before) % 2**32 >= 2**31:
        raise RuntimeError("Compositor did not progress or restarted during capture")
    started, copied = status.get("started_unix_ms", 0), status.get("copied_unix_ms", 0)
    if (type(started) is not int or type(copied) is not int
            or not requested_at - .002 <= started / 1000 <= copied / 1000 <= time.time() + 1):
        raise RuntimeError("SteamVR capture predates this request or has invalid timing")
    width, height = status.get("width"), status.get("height")
    if (type(width) is not int or type(height) is not int or width % 2 or
            not 2 <= width <= 32768 or not 1 <= height <= 16384 or width * height * 4 > 256 * 1024**2):
        raise RuntimeError("Invalid SteamVR stereo dimensions")
    for name, expected in (("image.png", (width, height)), ("left.png", (width // 2, height)),
                           ("right.png", (width // 2, height))):
        file = output / name
        if file.stat().st_mtime < requested_at - .002 or png_dimensions(file) != expected:
            raise RuntimeError("SteamVR image dimensions or timestamp differ from receipt")
    if receipt.stat().st_mtime < requested_at - .002:
        raise RuntimeError("SteamVR receipt predates this request")
    return status


class LiveTest:
    def __init__(self, profile: Path = PROFILE, simulator: Path = SIMULATOR, *,
                 capture_source: str = "simulator"):
        if capture_source not in ("simulator", "steamvr"):
            raise ValueError("Choose simulator or steamvr capture")
        self.capture_source = capture_source
        self.profile, self.simulator = profile, simulator
        self.status_path = profile / "wuwa-test.status.json"
        state = fresh_json(self.status_path)
        self.pid = int(state["pid"])
        self.created = verify_process(self.pid)
        self.assert_live()

    def assert_live(self) -> dict:
        state = fresh_json(self.status_path)
        if state.get("version") != 1 or state.get("pid") != self.pid:
            raise RuntimeError("Backend process changed during test")
        if state.get("unix_ms", 0) / 1000 < self.created:
            raise RuntimeError("Backend status predates the current game process")
        if verify_process(self.pid) != self.created:
            raise RuntimeError("Game restarted during test")
        return state

    @contextlib.contextmanager
    def exclusive(self):
        import msvcrt
        # OS lock is released on client exit/crash. The empty file may remain.
        with (self.profile / "wuwa-test.client.lock").open("a+b") as handle:
            handle.seek(0)
            msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
            try:
                yield
            finally:
                handle.seek(0)
                msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)

    def request(self, op: str, timeout: float = 15, **fields) -> dict:
        self.assert_live()
        request_path = self.profile / "wuwa-test.request.json"
        if request_path.exists():
            raise RuntimeError("Another backend test request is pending")
        request = dict(version=1, pid=self.pid, id=uuid.uuid4().hex, op=op,
                       expires_unix_ms=int((time.time() + timeout + 3) * 1000), **fields)
        write_json(request_path, request)
        deadline = time.monotonic() + timeout
        try:
            while time.monotonic() < deadline:
                response_path = self.profile / "wuwa-test.response.json"
                if response_path.exists():
                    response = read_json(response_path)
                    if response.get("id") == request["id"] and response.get("pid") == self.pid:
                        if response.get("status") != "ok":
                            raise RuntimeError(f"Backend refused {op}: {response.get('error')}")
                        return response
                # A targeted resolver may take a moment; process identity still
                # must remain unchanged even while its heartbeat pauses.
                if verify_process(self.pid) != self.created:
                    raise RuntimeError("Game restarted during request")
                time.sleep(0.1)
            raise TimeoutError(f"No matching backend response for {op}")
        finally:
            try:
                if read_json(request_path).get("id") == request["id"]:
                    request_path.unlink(missing_ok=True)
            except FileNotFoundError:
                pass  # Backend may consume/delete the request during cleanup.

    def wait_frames(self, seconds: float = 2, lease: dict | None = None) -> None:
        steamvr = getattr(self, "capture_source", "simulator") == "steamvr"
        initial = None if steamvr else fresh_json(self.simulator / "runtime_status.json")
        started = time.monotonic()
        deadline = started + seconds + 6
        while time.monotonic() < deadline:
            state = self.assert_live()
            runtime = None if steamvr else fresh_json(self.simulator / "runtime_status.json")
            if lease:
                if state.get("unix_ms", 0) < lease["unix_ms"]:
                    time.sleep(0.1)
                    continue  # Response and heartbeat are separate atomic files.
                if lease.get("op") == "shadow_pass":
                    shadow = state.get("shadow", {})
                    expected_enabled = lease.get("shadow", {}).get("enabled", True)
                    if (shadow.get("enabled") != expected_enabled or not shadow.get("ready")
                            or shadow.get("faulted") or not shadow.get("test_active", True)):
                        raise RuntimeError("Shadow pass test is no longer active and healthy")
                elif (not state.get("active") or state.get("id") != lease["id"]
                      or state.get("actual") != lease["actual"] or state.get("render") != lease["actual"]):
                    raise RuntimeError("Graphics test no longer has the requested value")
            # With SteamVR this waits for the lease to settle only. The native
            # capture independently checks scene PID and compositor progress.
            # A backend heartbeat does not prove that an image was rendered.
            if time.monotonic() - started >= seconds and (steamvr or runtime["frame_count"] >= initial["frame_count"] + 10):
                return
            time.sleep(0.15)
        raise TimeoutError("Backend lease did not settle" if steamvr else "Simulator did not advance enough frames")

    def capture_layer(self, layer: str | None = None) -> str:
        steamvr = getattr(self, "capture_source", "simulator") == "steamvr"
        result = layer or ("all" if steamvr else "projection")
        if steamvr and result != "all":
            raise ValueError("SteamVR public mirrors are composited; projection/quad isolation needs the simulator")
        return result

    def capture_steamvr(self, output: Path) -> dict:
        self.assert_live()
        if getattr(self, "require_foreground", False) and foreground_pid() != self.pid:
            raise RuntimeError("WuWa lost foreground focus; comparison stopped")
        helper, openvr = TOOLS / "steamvr-capture.exe", TOOLS / "openvr_api.dll"
        if not helper.is_file() or not openvr.is_file():
            raise RuntimeError("This package does not include SteamVR mirror capture")
        if output.exists():
            raise FileExistsError("Output already exists; preserve prior capture")
        token = uuid.uuid4().hex
        requested_at = time.time()
        completed = subprocess.run([str(helper), str(openvr), str(self.pid), str(output.resolve()), token],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
            encoding="utf-8", errors="replace", timeout=15, creationflags=subprocess.CREATE_NO_WINDOW)
        if completed.returncode:
            raise RuntimeError("SteamVR capture stopped: " + completed.stdout.strip())
        status = validate_mirrors(output, pid=self.pid, token=token, requested_at=requested_at)
        backend = self.assert_live()
        if getattr(self, "require_foreground", False) and foreground_pid() != self.pid:
            raise RuntimeError("WuWa lost foreground focus during capture; comparison stopped")
        manifest = dict(pid=self.pid, process_created=self.created, requested_at=requested_at,
            request={"layer": "all", "eye": "both", "client_id": token, "source": "steamvr"},
            acknowledgement=status, backend=backend,
            image_sha256=hashlib.sha256((output / "image.png").read_bytes()).hexdigest(),
            limitation="Composited mirrors; no exact source-frame binding or atomic cross-eye capture")
        write_json(output / "capture.json", manifest)
        for name in ("config.txt", "cvars_data.txt", "cvars_standard.txt", "user_script.txt"):
            if (self.profile / name).exists():
                shutil.copy2(self.profile / name, output / name)
        return manifest

    def capture(self, output: Path, layer: str = "all") -> dict:
        self.capture_layer(layer)
        if getattr(self, "capture_source", "simulator") == "steamvr":
            return self.capture_steamvr(output)
        from PIL import Image
        self.assert_live()
        output.mkdir(parents=True, exist_ok=False)
        runtime_path = self.simulator / "runtime_status.json"
        before = fresh_json(runtime_path)
        request_path = self.simulator / "screenshot_request.json"
        if request_path.exists():
            raise RuntimeError("Another simulator capture request is pending")
        self.wait_frames(0.1)  # Do not request the same frame as the initial snapshot.
        token = uuid.uuid4().hex
        request = {"eye": "both", "layer": layer, "client_id": token}
        requested_at = time.time()
        write_json(request_path, request)
        screenshot_path = self.simulator / ("screenshot_quad.bmp" if layer == "quad" else "screenshot.bmp")
        status_path = self.simulator / "screenshot_status.json"
        deadline = time.monotonic() + 12
        last_error = "no capture acknowledgement"
        try:
            while time.monotonic() < deadline:
                self.assert_live()
                try:
                    status = read_json(status_path)
                    if status.get("client_id") == token and status.get("ok") is False:
                        raise RuntimeError(f"Simulator rejected capture: {status.get('error')}")
                    validate_capture(status, layer=layer, frame_before=before["frame_count"],
                                     status_mtime=status_path.stat().st_mtime,
                                     image_mtime=screenshot_path.stat().st_mtime, requested_at=requested_at,
                                     client_id=token, pid=self.pid)
                    with Image.open(screenshot_path) as image:
                        if image.size != (status["width"], status["height"]):
                            raise RuntimeError("Image dimensions differ from acknowledgement")
                        image.load()
                        # Older simulators lack request/PID identity. Keep the
                        # exclusive request and mid-read check for both versions.
                        if read_json(status_path) != status:
                            raise RuntimeError("Capture changed while being read")
                        runtime = fresh_json(runtime_path)
                        if runtime["frame_count"] < before["frame_count"]:
                            raise RuntimeError("Simulator frame counter restarted")
                        backend = self.assert_live()
                        image.save(output / "image.png")
                    manifest = dict(pid=self.pid, process_created=self.created,
                                    requested_at=requested_at, request=request, acknowledgement=status,
                                    runtime_before=before, runtime_after=runtime,
                                    runtime_pid_binding=("verified_capture_ack" if status.get("runtimePid") == self.pid
                                                         else "unavailable_legacy_runtime"),
                                    backend=backend, image_sha256=hashlib.sha256((output / "image.png").read_bytes()).hexdigest())
                    write_json(output / "capture.json", manifest)
                    for name in ("config.txt", "cvars_data.txt", "cvars_standard.txt", "user_script.txt"):
                        if (self.profile / name).exists():
                            shutil.copy2(self.profile / name, output / name)
                    return manifest
                except (OSError, ValueError, RuntimeError) as error:
                    if isinstance(error, RuntimeError) and str(error).startswith("Simulator rejected capture:"):
                        raise
                    last_error = str(error)
                time.sleep(0.1)
            raise TimeoutError(f"No fresh simulator capture: {last_error}")
        finally:
            # Remove only our still-pending request; leave other clients alone.
            if request_path.exists() and read_json(request_path).get("client_id") == token:
                request_path.unlink()

    def graphics(self, name: str, value: int, output: Path, layer: str | None = None) -> dict:
        layer = self.capture_layer(layer)
        output.mkdir(parents=True, exist_ok=False)
        report = {"pid": self.pid, "name": name, "test_value": value, "status": "incomplete",
                  "capture_source": getattr(self, "capture_source", "simulator"), "layer": layer}
        begin = None
        try:
            baseline = self.request("query", name=name)
            report["baseline"] = baseline
            self.wait_frames()
            self.capture(output / "baseline", layer)
            if baseline["actual"] == value:
                report["status"] = "already_at_test_value"
                return report
            try:
                begin = self.request("begin", name=name, value=value, expected=baseline["actual"], seconds=45)
                report["begin"] = begin
                self.wait_frames(2, begin)
                self.capture(output / "changed", layer)
                state = self.assert_live()
                if state.get("id") != begin["id"] or state.get("actual") != value or state.get("render") != value:
                    raise RuntimeError("CVar changed during capture")
            finally:
                if begin is not None:
                    report["restore"] = self.request("restore", lease_id=begin["id"])
                    if report["restore"].get("restore") != "restored":
                        raise RuntimeError(f"Restore was not confirmed: {report['restore']}")
            restored = self.request("query", name=name)
            report["restored_value"] = restored
            if restored["actual"] != baseline["actual"] or restored["render"] != baseline["render"]:
                raise RuntimeError("Baseline restoration readback differs")
            self.wait_frames()
            self.capture(output / "restored", layer)
            report["status"] = "captured_and_restored_visual_review_pending"
            return report
        except BaseException as error:
            report["error"] = str(error)
            # If begin was consumed but its response was lost, the backend's
            # 45-second lease is the fallback. Never pretend restoration passed.
            raise
        finally:
            write_json(output / "comparison.json", report)


    def reflection_values(self) -> dict:
        """Read the three supported values, without beginning a graphics lease."""
        state = self.assert_live()
        supported = state.get("graphics_test_cvars", [])
        if not isinstance(supported, list) or any(name not in supported for _, _, name in REFLECTION_SOURCES):
            raise RuntimeError("This running backend does not support reflection queries")
        return {"pid": self.pid, "status": "queried_values_not_render_validation", "settings_changed": False,
                "values": [{"title": title, "name": name, "sample": self.request("query", name=name)}
                           for _, title, name in REFLECTION_SOURCES]}

    def reflections(self, output: Path) -> dict:
        """Capture each source off, restoring it before testing the next one."""
        return self.graphics_batch(output, tuple((folder, title, name, 0)
                                   for folder, title, name in REFLECTION_SOURCES), "reflection", "reflections.json")

    def translucency(self, output: Path) -> dict:
        """Compare two native material paths, restoring each before proceeding."""
        return self.graphics_batch(output, TRANSLUCENCY_SOURCES, "translucency", "translucency.json")

    def stereo(self, output: Path) -> dict:
        return self.graphics_batch(output, STEREO_SOURCES, "stereo", "stereo.json")

    def graphics_batch(self, output: Path, sources: tuple, kind: str, receipt: str) -> dict:
        layer = self.capture_layer()
        state = self.assert_live()
        supported = state.get("graphics_test_cvars", [])
        if not isinstance(supported, list) or any(name not in supported for _, _, name, _ in sources):
            raise RuntimeError(f"This running backend does not support the {kind} comparison; install the prepared update first")
        output.mkdir(parents=True, exist_ok=False)
        report = {"pid": self.pid, "status": "incomplete", "sources": [],
                  "capture_source": getattr(self, "capture_source", "simulator"), "layer": layer,
                  "render_effect": "unverified; no visible change is inconclusive"}
        try:
            for folder, title, name, value in sources:
                result = self.graphics(name, value, output / folder, layer)
                report["sources"].append({"folder": folder, "title": title, **result})
                # graphics() has confirmed restoration (or made no change) at
                # this point. Never proceed after a missing restore receipt.
                if result["status"] not in ("captured_and_restored_visual_review_pending", "already_at_test_value"):
                    raise RuntimeError(f"{kind.capitalize()} comparison incomplete: {name}")
            report["status"] = "comparisons_finished_visual_review_pending"
            return report
        except BaseException as error:
            report["error"] = str(error)
            raise
        finally:
            write_json(output / receipt, report)
            # Native SteamVR SBS places the unchanged per-eye RGB side by side.
            # No resampling, registration or comparison heatmap hides a defect.
            parts = [f'<!doctype html><meta charset="utf-8"><title>WuWa {kind} comparison</title>',
                     '<style>body{background:#111820;color:#eee;font:17px system-ui;margin:24px} img{width:100%;max-width:1100px} section{margin:32px 0} a{color:#bfe59c}</style>',
                     f'<h1>WuWa {kind} comparison</h1><p>Before, changed, then restored. Compare both eyes. Captures do not establish a fix.</p>',
                     '<p>Setting values are checked; their visible effect is unverified. No visible change is inconclusive: the game may not react to a live value change.</p>']
            parts.append('<p>' + ('SteamVR composited mirrors include the HUD. Exact source-frame pairing is unavailable.'
                         if report["capture_source"] == "steamvr" else 'Simulator capture: isolated projection layer.') + '</p>')
            if "error" in report:
                parts.append('<p>Stopped: ' + html.escape(report["error"]) + '</p>')
            for folder, title, _, value in sources:
                detail_path = output / folder / "comparison.json"
                if not detail_path.is_file():
                    continue
                detail = read_json(detail_path)
                parts.append('<section><h2>' + html.escape(title) + '</h2><p>' + html.escape(detail["status"]) + '</p>')
                for stage, label in (("baseline", "Before"), ("changed", f"Changed to {value}"), ("restored", "Restored")):
                    relative = f"{folder}/{stage}/image.png"
                    if (output / relative).is_file():
                        parts.append(f'<h3>{label}</h3><a href="{relative}"><img src="{relative}" alt="{label}"></a>')
                parts.append('</section>')
            (output / "index.html").write_text('\n'.join(parts), encoding="utf-8")

    def shadows(self, output: Path) -> dict:
        """Compare the opposite pass state, then return to the original setting."""
        output.mkdir(parents=True, exist_ok=False)
        report = {"pid": self.pid, "test": "second_eye_primary_pass", "status": "incomplete"}
        begin = None
        try:
            baseline = self.request("shadow_query")["shadow"]
            report["baseline"] = baseline
            configurable = "configured_enabled" in baseline
            if (not baseline.get("ready") or baseline.get("faulted") or baseline.get("test_active")
                    or (baseline.get("enabled") and not configurable)):
                raise RuntimeError("Shadow view pair is not verified and idle")
            changed_enabled = not baseline["enabled"]
            report["changed_enabled"] = changed_enabled
            self.wait_frames()
            self.capture(output / "baseline", "all")
            try:
                fields = {"enabled": changed_enabled} if configurable else {}
                begin = self.request("shadow_pass", seconds=45, **fields)
                report["begin"] = begin
                if begin["shadow"]["enabled"] != changed_enabled:
                    raise RuntimeError("Backend did not select the requested shadow state")
                self.wait_frames(2, begin)
                self.capture(output / "changed", "all")
                active = self.request("shadow_query")["shadow"]
                report["active"] = active
                applied_before = begin["shadow"]["applied"]
                counter_ok = (active["applied"] > applied_before if changed_enabled
                              else active["applied"] == applied_before)
                if (active.get("enabled") != changed_enabled or active.get("faulted") or
                        not active.get("test_active", True) or not counter_ok
                        or active["restored"] != active["applied"]):
                    raise RuntimeError("Shadow override did not apply and restore cleanly")
            finally:
                if begin is not None:
                    report["disable"] = self.request("shadow_pass", seconds=0)
            restored = self.request("shadow_query")["shadow"]
            report["restored"] = restored
            if (restored.get("enabled") != baseline["enabled"] or restored.get("faulted")
                    or restored.get("test_active") or restored["applied"] != restored["restored"]
                    or restored.get("configured_enabled") != baseline.get("configured_enabled")):
                raise RuntimeError("Shadow override restoration was not confirmed")
            self.wait_frames()
            self.capture(output / "restored", "all")
            report["status"] = "captured_and_restored_visual_review_pending"
            return report
        except BaseException as error:
            report["error"] = str(error)
            raise
        finally:
            write_json(output / "comparison.json", report)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("status")
    capture = commands.add_parser("capture")
    capture.add_argument("output", type=Path)
    capture.add_argument("--layer", choices=("all", "projection", "quad"), default="all")
    graphics = commands.add_parser("graphics")
    graphics.add_argument("name")
    graphics.add_argument("--value", type=int, default=0)
    graphics.add_argument("--output", type=Path, required=True)
    graphics.add_argument("--layer", choices=("all", "projection"), help="Default: projection for simulator, all for SteamVR")
    reflections = commands.add_parser("reflections", help="Capture three reflection sources off one at a time, restoring each")
    reflections.add_argument("--output", type=Path, required=True)
    reflections.add_argument("--pid", type=int, help="Refuse a different game process")
    translucency = commands.add_parser("translucency", help="Capture full-resolution and blur material comparisons with restoration")
    translucency.add_argument("--output", type=Path, required=True)
    translucency.add_argument("--pid", type=int, help="Refuse a different game process")
    stereo = commands.add_parser("stereo", help="Compare reflection and translucency paths in one restored batch")
    stereo.add_argument("--output", type=Path, required=True)
    stereo.add_argument("--pid", type=int, help="Refuse a different game process")
    values = commands.add_parser("reflection-values", help="Read the three reflection values without changing them")
    values.add_argument("--pid", type=int, help="Refuse a different game process")
    values.add_argument("--output", type=Path, help="Optional JSON receipt path")
    shadows = commands.add_parser("shadows")
    shadows.add_argument("--output", type=Path, required=True)
    for command in (capture, graphics, reflections, translucency, stereo, shadows):
        command.add_argument("--capture-source", choices=("simulator", "steamvr"), default="simulator",
                             help="Select an already running runtime; never switches or starts one")
    shadow_pass = commands.add_parser("shadow-pass")
    shadow_pass.add_argument("--seconds", type=int, default=0)
    trace = commands.add_parser("trace")
    trace.add_argument("--seconds", type=int, default=90)
    trace.add_argument("--until-stopped", action="store_true", help="Record until stopped, game exit or 1000 changes")
    record = commands.add_parser("record-motion", help="Record bounded local controller/camera sidecar; never generates input")
    record.add_argument("--seconds", type=int, choices=range(0, 301), metavar="0..300", default=120)
    record.add_argument("--recording-id", default="")
    motion = commands.add_parser("motion-input")
    motion.add_argument("--mute-seconds", type=int, default=90)
    focus = commands.add_parser("input-focus", help="Temporary Windows/XR focus candidate; does not edit config")
    focus.add_argument("--policy", choices=("windows", "xr", "none"), required=True)
    focus.add_argument("--seconds", type=int, choices=range(0, 121), metavar="0..120", default=60)
    restore = commands.add_parser("restore")
    restore.add_argument("lease_id")
    args = parser.parse_args()
    if getattr(args, "capture_source", None) == "steamvr" and getattr(args, "layer", None) not in (None, "all"):
        parser.error("SteamVR mirrors cannot isolate projection/quad layers; use --layer all")
    client = LiveTest()
    client.capture_source = getattr(args, "capture_source", "simulator")
    if args.command in ("reflections", "reflection-values", "translucency", "stereo") and args.pid is not None and client.pid != args.pid:
        raise RuntimeError("Game process changed before graphics comparison")
    with client.exclusive():
        if args.command == "status":
            result = client.assert_live()
        elif args.command == "capture":
            result = client.capture(args.output, args.layer)
        elif args.command == "graphics":
            result = client.graphics(args.name, args.value, args.output, args.layer)
        elif args.command == "reflections":
            result = client.reflections(args.output)
        elif args.command == "translucency":
            result = client.translucency(args.output)
        elif args.command == "stereo":
            result = client.stereo(args.output)
        elif args.command == "reflection-values":
            result = client.reflection_values()
            if args.output is not None:
                args.output.parent.mkdir(parents=True, exist_ok=True)
                write_json(args.output, result)
        elif args.command == "shadows":
            result = client.shadows(args.output)
        elif args.command == "shadow-pass":
            result = client.request("shadow_pass", seconds=args.seconds)
        elif args.command == "trace":
            result = client.request("trace", seconds=0 if args.until_stopped else args.seconds, until_stopped=args.until_stopped)
        elif args.command == "record-motion":
            result = client.request("record_motion", seconds=args.seconds, recording_id=args.recording_id)
        elif args.command == "motion-input":
            result = client.request("motion_input", mute_seconds=args.mute_seconds)
        elif args.command == "input-focus":
            result = client.request("input_focus", policy=args.policy, seconds=args.seconds)
        else:
            result = client.request("restore", lease_id=args.lease_id)
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, KeyError, subprocess.SubprocessError) as exc:
        print(f"Test stopped: {exc}", file=sys.stderr)
        sys.exit(1)
