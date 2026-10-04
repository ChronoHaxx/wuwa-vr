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
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time
import uuid

PROFILE = Path(os.environ.get("APPDATA", "")) / "UnrealVRMod/Client-Win64-Shipping"
SIMULATOR = Path(os.environ.get("LOCALAPPDATA", "")) / "OpenXR-Simulator"
STATE_SWAP_MODES = ("exchange", "first_for_both", "second_for_both")
TOOLS = Path(__file__).resolve().parent.parent / "dev-tools"
REFLECTION_SOURCES = (
    ("planar", "Planar reflection updates", "r.Kuro.EnablePlanarReflection"),
    ("screen-space", "Screen-space reflections", "r.SSR.Quality"),
    ("environment", "Reflection environment", "r.ReflectionEnvironment"),
)
GRAPHICS_INTERPRETATION = {
    "r.MeshDrawCommands.UseCachedCommands": (
        "Temporarily generates mesh draw commands each frame instead of using cached commands. "
        "Rendering cost may increase; geometry and wind are not intentionally hidden. "
        "An improvement implicates a cached-command path but does not by itself identify a stale "
        "view buffer. A zero baseline means no comparison. Saved or frozen settings stay unchanged."
    ),
    "r.Kuro.EnablePlanarReflection": (
        "The inspected game path gates planar reflection updates. A cached reflection may remain visible "
        "when this is zero; an unchanged still image cannot rule out planar reflections. "
        "This comparison does not clear reflection textures or establish which path draws the defect."
    ),
    "vr.RoundRobinOcclusion": (
        "Tests alternating-eye occlusion scheduling. Already zero means no change was made. "
        "An improvement would implicate scheduling; it would not prove the stereo cameras are correct."
    ),
    "r.AllowOcclusionQueries": (
        "Temporarily disables hardware occlusion queries. It may increase rendering cost. "
        "This does not disable every custom visibility path or repair per-eye visibility."
    ),
    "foliage.ForceLOD": (
        "Temporarily fixes foliage at the requested LOD to separate LOD selection from missing geometry. "
        "This changes visible detail and may increase rendering cost; it is not a final rendering fix."
    ),
    "foliage.DisableCull": (
        "Temporarily disables the standard foliage frustum culling path. Custom imposters and "
        "distance culling may still apply. Rendering cost may increase; no persistent setting is saved."
    ),
    "r.EyeAdaptationQuality": (
        "Temporarily disables automatic exposure to isolate an exposure mismatch from shadows. "
        "Scene brightness may change; fixed exposure is not the final lighting fix."
    ),
    "r.ImposterVer2.ForceMode": (
        "Compares this game's impostor system: 2 requests source meshes, 1 requests impostors. "
        "Confirm that the target object actually changes before interpreting the result. "
        "No change can mean an unexercised or cached path, not proof that impostors are innocent. "
        "Each comparison restores the original mode; this is not a shipped quality override."
    ),
}
TRANSLUCENCY_SOURCES = (
    ("full-resolution", "Full-resolution translucent effects", "r.KuroDownsampleTranslucencyFullRes", 1),
    ("without-blur", "Translucency without the separate blur", "r.Kuro.SeparateTranslucencyBlur", 0),
)
STEREO_SOURCES = tuple((folder, title, name, 0) for folder, title, name in REFLECTION_SOURCES) + TRANSLUCENCY_SOURCES
VISIBILITY_SOURCES = (
    ("round-robin", "Alternating-eye occlusion off", "vr.RoundRobinOcclusion", 0),
    ("occlusion", "Hardware occlusion queries off", "r.AllowOcclusionQueries", 0),
    ("fixed-foliage-lod", "Foliage LOD 1", "foliage.ForceLOD", 1),
    ("foliage-frustum", "Foliage frustum culling off", "foliage.DisableCull", 1),
    ("exposure", "Automatic exposure off", "r.EyeAdaptationQuality", 0),
)
IMPOSTOR_SOURCES = (
    ("source-meshes", "Request source meshes", "r.ImposterVer2.ForceMode", 2),
    ("impostors", "Request impostors", "r.ImposterVer2.ForceMode", 1),
)
STEREO_CANDIDATES = (
    ("baseline", "All three candidates off", (False,False,False)),
    ("planar", "Per-eye reflection parameters", (True,False,False)),
    ("translucency", "Full-resolution stereo translucency", (False,True,False)),
    ("combined", "Both corrections", (True,True,False)),
    ("kuro-fallback", "Hide custom reflections (workaround)", (False,False,True)),
)


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

    def assert_focus(self) -> None:
        if getattr(self, "require_foreground", False) and foreground_pid() != self.pid:
            raise RuntimeError("WuWa lost foreground focus; comparison stopped")

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

    def request(self, op: str, timeout: float = 15, *, request_id: str | None = None, **fields) -> dict:
        if request_id is not None and (not isinstance(request_id, str) or not re.fullmatch(r"[a-zA-Z0-9_-]{1,64}", request_id)):
            raise ValueError("Invalid backend request identity")
        self.assert_live()
        request_path = self.profile / "wuwa-test.request.json"
        if request_path.exists():
            raise RuntimeError("Another backend test request is pending")
        request = dict(version=1, pid=self.pid, id=request_id or uuid.uuid4().hex, op=op,
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
            self.assert_focus()
            runtime = None if steamvr else fresh_json(self.simulator / "runtime_status.json")
            if lease:
                if state.get("unix_ms", 0) < lease["unix_ms"]:
                    time.sleep(0.1)
                    continue  # Response and heartbeat are separate atomic files.
                if lease.get("op") == "shadow_pass":
                    shadow = state.get("shadow", {})
                    expected_enabled = lease.get("shadow", {}).get("enabled", True)
                    if (shadow.get("enabled") != expected_enabled or not shadow.get("ready")
                            or shadow.get("faulted") or not shadow.get("test_active", True)
                            or shadow.get("full_view_enabled", False) != lease.get("shadow", {}).get("full_view_enabled", False)):
                        raise RuntimeError("Shadow pass test is no longer active and healthy")
                elif lease.get("op") == "stereo_candidates":
                    candidate=state.get("stereo_candidate_test",{})
                    if (not candidate.get("active") or candidate.get("id")!=lease["id"] or
                            candidate.get("values")!=lease["stereo_candidate_test"]["values"]):
                        raise RuntimeError("Stereo candidate comparison expired or changed")
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
        self.assert_focus()
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
                self.assert_focus()
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
        if name in GRAPHICS_INTERPRETATION:
            report["interpretation"] = GRAPHICS_INTERPRETATION[name]
        begin = None
        try:
            self.assert_focus()
            baseline = self.request("query", name=name)
            report["baseline"] = baseline
            self.wait_frames()
            self.capture(output / "baseline", layer)
            if baseline["actual"] == value:
                report["status"] = "already_at_test_value"
                return report
            try:
                self.assert_focus()
                begin = self.request("begin", name=name, value=value, expected=baseline["actual"], seconds=45)
                report["begin"] = begin
                self.wait_frames(4 if name == "r.ImposterVer2.ForceMode" else 2, begin)
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


    def console(self, name: str, value: float, output: Path, layer: str | None = None) -> dict:
        """Set one r.* console variable for a bounded window through the console manager; capture, restore, capture."""
        if type(value) not in (int, float) or abs(value) > 100000 or not math.isfinite(value):
            raise ValueError("Console test value must be finite and within -100000..100000")
        value = float(value)

        def number(sample, key):
            result = sample.get(key)
            if type(result) not in (int, float) or not math.isfinite(result):
                raise RuntimeError(f"Console readback {key} is unavailable or nonfinite")
            return result

        def matches(actual, expected):
            # The SDK reports a 32-bit float. Allow its rounding, not a broad
            # fixed epsilon which could accept a failed zero/small-value write.
            rounded = struct.unpack("f", struct.pack("f", expected))[0]
            return math.isclose(actual, rounded, rel_tol=2 ** -23, abs_tol=0)

        def verify(sample, expected, float_key="float", int_key="int", expected_int=None):
            if not matches(number(sample, float_key), expected):
                raise RuntimeError(f"Console {float_key} readback differs from requested value {expected}")
            # Integral requests must survive both getter paths. Fractional
            # requests may legitimately truncate through GetInt(). Restoration
            # always compares the independently captured baseline integer too.
            integer = expected_int if expected_int is not None else (int(expected) if expected.is_integer() else None)
            if integer is not None and (type(sample.get(int_key)) is not int or sample[int_key] != integer):
                raise RuntimeError(f"Console {int_key} readback differs from expected value {integer}")

        layer = self.capture_layer(layer)
        output.mkdir(parents=True, exist_ok=False)
        report = {"pid": self.pid, "name": name, "test_value": value, "status": "incomplete", "layer": layer,
                  "applied_verified": False, "changed_capture_valid": False, "restoration_verified": False,
                  "restored_capture_valid": False}
        try:
            report["baseline"] = self.request("console_get", name=name)
            baseline = float(number(report["baseline"], "float"))
            baseline_int = report["baseline"].get("int")
            if type(baseline_int) is not int:
                raise RuntimeError("Baseline console integer readback is unavailable")
            self.wait_frames()
            self.capture(output / "baseline", layer)
            if matches(baseline, value) and (not value.is_integer() or baseline_int == int(value)):
                report["status"] = "already_at_test_value"
                return report
            failure = None
            try:
                report["begin"] = self.request("console_set", name=name, value=value, seconds=30)
                verify(report["begin"], value, "after_float", "after_int")
                report["applied_verified"] = True
                self.wait_frames()
                report["before_changed_capture"] = self.request("console_get", name=name)
                verify(report["before_changed_capture"], value)
                self.capture(output / "changed", layer)
                report["after_changed_capture"] = self.request("console_get", name=name)
                verify(report["after_changed_capture"], value)
                report["changed_capture_valid"] = True
            except BaseException as error:
                failure = error
                report["error"] = str(error)
            finally:
                cleanup_errors = []
                # Even a rejected readback or lost begin response may leave a
                # live lease. Attempt cleanup, then read the value independently
                # even if the restore acknowledgement itself failed.
                try:
                    report["restore"] = self.request("console_set", name=name, seconds=0)
                    if report["restore"].get("restore") != "restored":
                        raise RuntimeError(f"Console restore was not confirmed: {report['restore']}")
                except BaseException as error:
                    cleanup_errors.append(str(error))
                    if failure is None:
                        failure = error
                try:
                    report["restored_value"] = self.request("console_get", name=name)
                    verify(report["restored_value"], baseline, expected_int=baseline_int)
                except BaseException as error:
                    cleanup_errors.append(str(error))
                    if failure is None:
                        failure = error
                report["restoration_verified"] = not cleanup_errors
                if cleanup_errors:
                    report["restore_errors"] = cleanup_errors
            if failure is not None:
                raise failure
            self.wait_frames()
            try:
                report["before_restored_capture"] = self.request("console_get", name=name)
                verify(report["before_restored_capture"], baseline, expected_int=baseline_int)
                self.capture(output / "restored", layer)
                report["after_restored_capture"] = self.request("console_get", name=name)
                verify(report["after_restored_capture"], baseline, expected_int=baseline_int)
                report["restored_capture_valid"] = True
            except BaseException:
                report["restoration_verified"] = False
                raise
            report["status"] = "captured_and_restored_visual_review_pending"
            return report
        except BaseException as error:
            report["status"] = "failed"
            report["error"] = str(error)
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
        """Compare three controls at zero, restoring each before the next test."""
        return self.graphics_batch(output, tuple((folder, title, name, 0)
                                   for folder, title, name in REFLECTION_SOURCES), "reflection", "reflections.json")

    def translucency(self, output: Path) -> dict:
        """Compare two native material paths, restoring each before proceeding."""
        return self.graphics_batch(output, TRANSLUCENCY_SOURCES, "translucency", "translucency.json")

    def stereo(self, output: Path) -> dict:
        return self.graphics_batch(output, STEREO_SOURCES, "stereo", "stereo.json")

    def visibility(self, output: Path) -> dict:
        """Separate visibility paths with per-control restoration, without input."""
        previous = getattr(self, "require_foreground", False)
        self.require_foreground = True
        try:
            self.assert_focus()
            return self.graphics_batch(output, VISIBILITY_SOURCES, "visibility", "visibility.json")
        finally:
            # Restore requests deliberately do not require foreground focus.
            self.require_foreground = previous

    def impostors(self, output: Path) -> dict:
        previous = getattr(self, "require_foreground", False)
        self.require_foreground = True
        try:
            self.assert_focus()
            return self.graphics_batch(output, IMPOSTOR_SOURCES, "impostor", "impostors.json")
        finally:
            self.require_foreground = previous

    def lod_inputs(self, output: Path, seconds: int = 8, view_uniforms: bool = False,
                   mesh_bindings: bool = False, raw_snapshots: bool = False, state_swap: bool = False,
                   state_swap_mode: str = "exchange") -> dict:
        """Capture an expiring read-only trace and images; no rendering change unless state_swap.

        state_swap: for the trace window only, the two main views exchange their view-state
        pointers every frame (restored after each frame's submissions and when the window
        ends). A diagnostic write; the trace records which state each eye slot carried."""
        if not 1 <= seconds <= 30:
            raise ValueError("LOD input recording must be 1..30 seconds")
        state = self.assert_live()
        if "lod_probe" not in state:
            raise RuntimeError("This backend does not include the LOD input probe")
        if state["lod_probe"].get("active"):
            raise RuntimeError("A LOD input recording is already active")
        if view_uniforms and state["lod_probe"].get("view_uniforms_supported") is not True:
            raise RuntimeError("This backend does not support View uniform tracing")
        if mesh_bindings and state["lod_probe"].get("mesh_bindings_supported") is not True:
            raise RuntimeError("This backend does not support mesh binding tracing")
        if raw_snapshots and state["lod_probe"].get("raw_snapshots_supported") is not True:
            raise RuntimeError("This backend does not support raw eye snapshots")
        shadow = state.get("shadow") or {}
        if state_swap:
            if shadow.get("state_swap_supported") is not True:
                raise RuntimeError("This backend does not support the view-state swap")
            if (not shadow.get("ready") or shadow.get("faulted") or shadow.get("test_active")
                    or shadow.get("state_swap_active")):
                raise RuntimeError("Eye pair is not verified and idle; the view-state swap was not started")
        revision = state["lod_probe"].get("mesh_binding_hook_revision")
        if mesh_bindings and (type(revision) is not int or revision != 2):
            raise RuntimeError("Mesh tracing is blocked on this backend: its observer can overwrite a native branch target. Use a backend with hook revision 2; ordinary gameplay and recording do not install this observer.")
        output.mkdir(parents=True, exist_ok=False)
        report = dict(pid=self.pid, status="incomplete", read_only=not state_swap, seconds=seconds,
                      state_swap_requested=state_swap,
                      view_uniforms_requested=view_uniforms,
                      mesh_bindings_requested=mesh_bindings,
                      raw_snapshots_requested=raw_snapshots,
                      interpretation="Instanced binding inputs only. Matching CPU family frames is not GPU frame proof; visual review is required.")
        started = None
        extra_traces = []
        if view_uniforms:
            extra_traces.append(("view_uniforms", "View uniform", "view-ub"))
        if mesh_bindings:
            extra_traces.append(("mesh_bindings", "Mesh binding", "mesh-bindings"))
        extra_sources = {}
        stopped_cleanly = False
        swap_started = swap_stopped = False

        def extra_trace_path(trace: dict, key: str, label: str, prefix: str) -> Path:
            sample = trace.get(key)
            if not isinstance(sample, dict) or sample.get("requested") is not True:
                raise RuntimeError(f"Backend did not acknowledge the requested {label} trace")
            raw_path = sample.get("path")
            if not isinstance(raw_path, str) or not raw_path:
                raise RuntimeError(f"{label} recording path is missing")
            source = Path(raw_path).resolve()
            if (source.parent != (self.profile / "diagnostics").resolve()
                    or source.suffix != ".jsonl" or not source.name.startswith(f"{prefix}-{self.pid}-")):
                raise RuntimeError(f"{label} recording path or identity is invalid")
            return source

        previous = getattr(self, "require_foreground", False)
        self.require_foreground = True
        try:
            self.assert_focus()
            self.capture(output / "baseline", self.capture_layer())
            options = {key: True for key, _, _ in extra_traces}
            if raw_snapshots:
                options["raw_snapshots"] = True
            if state_swap:
                # Longer than the trace so every traced pair is swapped; ended explicitly below.
                swap_started = True
                begin = self.request("state_swap", seconds=min(60, seconds + 15), mode=state_swap_mode)["shadow"]
                report["state_swap_begin"] = begin
                if not begin.get("state_swap_active"):
                    raise RuntimeError("Backend did not open the view-state swap window")
                self.wait_frames(1)
                if self.request("shadow_query")["shadow"].get("state_swap_applied", 0) <= begin.get("state_swap_applied", 0):
                    raise RuntimeError("View-state swap did not apply to any eye pair")
            started = self.request("lod_probe", seconds=seconds, **options)
            report["begin"] = started
            if raw_snapshots and (started["lod_probe"].get("raw_snapshots") or {}).get("requested") is not True:
                raise RuntimeError("Backend did not acknowledge the requested raw eye snapshots")
            for key, label, prefix in extra_traces:
                extra_sources[key] = extra_trace_path(started["lod_probe"], key, label, prefix)
            self.wait_frames(seconds)
            report["end"] = self.request("lod_probe", seconds=0)
            trace = report["end"]["lod_probe"]
            stopped_cleanly = trace.get("active") is False
            if not stopped_cleanly or trace.get("error"):
                raise RuntimeError("LOD recording did not stop cleanly: " + str(trace.get("error", "still active")))
            source = Path(trace["path"]).resolve()
            if (source.parent != (self.profile / "diagnostics").resolve()
                    or source.suffix != ".jsonl" or not source.name.startswith(f"lod-{self.pid}-")
                    or str(source) != str(Path(started["lod_probe"]["path"]).resolve())
                    or source.stat().st_size > 32 * 1024 * 1024):
                raise RuntimeError("LOD recording path, identity or size is invalid")
            shutil.copyfile(source, output / "lod.jsonl")
            report["trace_sha256"] = hashlib.sha256((output / "lod.jsonl").read_bytes()).hexdigest()
            for key, label, prefix in extra_traces:
                source = extra_trace_path(trace, key, label, prefix)
                if source != extra_sources[key]:
                    raise RuntimeError(f"{label} recording path changed after start")
                sample = trace[key]
                if sample.get("error") or type(sample.get("truncated")) is not bool:
                    raise RuntimeError(f"{label} recording failed or omitted its truncation status")
                limit = 64 * 1024 * 1024
                if not source.is_file() or source.stat().st_size > limit:
                    raise RuntimeError(f"{label} recording file or size is invalid")
                # The backend has stopped; retain an explicit bound even if a
                # local writer changes the file after the stat check.
                with source.open("rb") as file:
                    data = file.read(limit + 1)
                if len(data) > limit:
                    raise RuntimeError(f"{label} recording exceeds the 64 MiB limit")
                (output / f"{prefix}.jsonl").write_bytes(data)
                report[f"{key}_trace_sha256"] = hashlib.sha256(data).hexdigest()
                report[f"{key}_trace_truncated"] = sample["truncated"]
            if state_swap:
                report["state_swap_end"] = self.request("shadow_query")["shadow"]
                report["state_swap_stop"] = self.request("state_swap", seconds=0)["shadow"]
                swap_stopped = True
                self.wait_frames(1)
                final = self.request("shadow_query")["shadow"]
                report["state_swap_restored"] = final
                if (final.get("faulted") or final.get("state_swap_active")
                        or final.get("state_swap_restored") != final.get("state_swap_applied")
                        or final.get("state_swap_applied", 0) <= report["state_swap_begin"].get("state_swap_applied", 0)):
                    raise RuntimeError("View-state swap did not apply and restore cleanly")
            self.capture(output / "after", self.capture_layer())
            report["status"] = "captured_inputs_visual_review_pending" if trace.get("written", 0) else "no_covered_draws"
            return report
        except BaseException as error:
            report["error"] = str(error)
            raise
        finally:
            if swap_started and not swap_stopped:
                try:
                    report["state_swap_cleanup"] = self.request("state_swap", seconds=0)
                except BaseException as error:
                    report["state_swap_cleanup_error"] = str(error)  # The window expires within 60 seconds.
            if started is not None and not stopped_cleanly:
                try:
                    report["cleanup"] = self.request("lod_probe", seconds=0)
                except BaseException as error:
                    report["cleanup_error"] = str(error)  # Backend expires within 30 seconds.
            self.require_foreground = previous
            write_json(output / "lod-inputs.json", report)

    def state_swap_window(self, seconds: int, mode: str = "exchange") -> dict:
        """Open (1..60 s) or end (0) the diagnostic view-state swap and return at once, so Launcher.exe
        can record the swapped condition: this client's control lock is released on exit, and the
        window expires on its own in the backend."""
        if not 0 <= seconds <= 60:
            raise ValueError("View-state swap window must be 0..60 seconds")
        shadow = self.assert_live().get("shadow") or {}
        if seconds:
            if shadow.get("state_swap_supported") is not True:
                raise RuntimeError("This backend does not support the view-state swap")
            if (not shadow.get("ready") or shadow.get("faulted") or shadow.get("test_active")
                    or shadow.get("state_swap_active")):
                raise RuntimeError("Eye pair is not verified and idle; the view-state swap was not started")
        reply = self.request("state_swap", seconds=seconds, mode=mode)["shadow"]
        if bool(reply.get("state_swap_active")) != bool(seconds):
            raise RuntimeError("Backend did not " + ("open" if seconds else "end") + " the view-state swap window")
        return reply

    @staticmethod
    def candidate_state(state: dict, values: tuple) -> None:
        planar,translucency,hide=values
        for key,expected in (("planar_eye_correction",planar),("stereo_translucency",translucency)):
            sample=state.get(key,{})
            if sample.get("faulted") or sample.get("enabled") is not expected:
                raise RuntimeError(f"{key} did not enter the requested healthy state: {sample.get('error') or 'enabled state mismatch'}")
        kuro=state.get("kuro_reflection",{})
        if kuro.get("faulted") or kuro.get("mode")!=(2 if hide else 1 if planar else 0):
            raise RuntimeError("Kuro reflection comparison did not enter the requested healthy state: " +
                               (kuro.get("error") or "mode mismatch"))

    def stereo_candidates(self, output: Path) -> dict:
        """Five bounded comparisons; restore configured switches even on capture failure."""
        initial=self.assert_live()
        if not all(key in initial for key in ("stereo_candidate_test","planar_eye_correction","stereo_translucency","kuro_reflection")):
            raise RuntimeError("This backend lacks the stereo candidate batch; install the staged build first")
        if initial.get("active") or initial["stereo_candidate_test"].get("active") or initial.get("shadow",{}).get("test_active"):
            raise RuntimeError("Another graphics comparison is already active")
        options=initial.get("live_options",{})
        if (options.get("VR_RenderingMethod")!="0" or options.get("VR_NativeStereoFix")!="false" or
                options.get("VR_ExtremeCompatibilityMode")!="false"):
            raise RuntimeError("Stereo candidates require native stereo with Native Stereo Fix and Extreme Compatibility off")
        option_keys=("VR_WuWaPlanarEyeParameters","VR_WuWaStereoTranslucency","VR_WuWaHideKuroReflections")
        if any(options.get(key) not in ("true","false") for key in option_keys):
            raise RuntimeError("Missing configured stereo candidate values")
        configured=tuple(options[key]=="true" for key in option_keys)
        output.mkdir(parents=True,exist_ok=False)
        layer=self.capture_layer()
        report={"pid":self.pid,"status":"incomplete","capture_source":getattr(self,"capture_source","simulator"),
                "layer":layer,"initial":initial,"stages":[],"input_sent":False,
                "interpretation":"Captured images and hook counters need visual review; counters do not prove the defect is repaired."}
        try:
            for folder,title,values in STEREO_CANDIDATES:
                stage={"folder":folder,"title":title,"values":values}; report["stages"].append(stage)
                begin=None
                try:
                    begin=self.request("stereo_candidates",seconds=45,values=list(values))
                    stage["begin"]=begin
                    self.wait_frames(2,begin)
                    before=self.assert_live()
                    stage["before"]=before
                    self.candidate_state(before,values)
                    self.capture(output/folder,layer)
                    self.wait_frames(2,begin)
                    after=self.assert_live()
                    stage["after"]=after
                    self.candidate_state(after,values)
                    active=after.get("stereo_candidate_test",{})
                    if not active.get("active") or active.get("id")!=begin["id"]:
                        raise RuntimeError("Stereo candidate comparison ended during capture")
                    stage["unexercised"]=[]
                    for index,key,counter in ((0,"planar_eye_correction","applied"),
                                               (1,"stereo_translucency","forced_full_resolution"),
                                               (2,"kuro_reflection","suppressed")):
                        if values[index] and after[key].get(counter,0)<=before[key].get(counter,0):
                            stage["unexercised"].append(key)
                finally:
                    if begin is not None:
                        stage["restore"]=self.request("stereo_candidates",seconds=0,lease_id=begin["id"])
                        if stage["restore"]["stereo_candidate_test"].get("active"):
                            raise RuntimeError("Stereo candidate comparison restoration not confirmed")
                    else: stage["restore_note"]="No begin response; a consumed request expires within 45 seconds. Restoration is unconfirmed."
                self.wait_frames()
                restored=self.assert_live(); self.candidate_state(restored,configured)
                if restored.get("stereo_candidate_test",{}).get("active"):
                    raise RuntimeError("Stereo candidate comparison unexpectedly remains active")
            self.capture(output/"restored",layer)
            report["restored"]=self.assert_live()
            if any(report["restored"].get("live_options",{}).get(key)!=options[key] for key in option_keys):
                raise RuntimeError("Configured stereo choices changed during comparison")
            report["status"]="captured_and_restored_visual_review_pending"
            return report
        except BaseException as error:
            report["error"]=str(error); raise
        finally:
            write_json(output/"stereo-candidates.json",report)
            rows=[]
            for stage in report["stages"]:
                folder=stage["folder"]
                if (output/folder/"image.png").exists():
                    rows.append(f'<h2>{html.escape(stage["title"])}</h2><img src="{folder}/image.png" alt="Both eyes">')
            (output/"index.html").write_text('<!doctype html><meta charset="utf-8"><title>Stereo comparison</title>'
                '<style>body{background:#171717;color:#eee;font:16px system-ui;margin:24px}img{max-width:100%}</style>'
                '<h1>Stereo comparison</h1><p>Visual review pending. A still cannot establish temporal stability. '
                'See stereo-candidates.json for correction counters and restoration evidence.</p>'+''.join(rows),encoding="utf-8")

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
                if detail.get("interpretation"):
                    parts.append('<p>' + html.escape(detail["interpretation"]) + '</p>')
                for stage, label in (("baseline", "Before"), ("changed", f"Changed to {value}"), ("restored", "Restored")):
                    relative = f"{folder}/{stage}/image.png"
                    if (output / relative).is_file():
                        parts.append(f'<h3>{label}</h3><a href="{relative}"><img src="{relative}" alt="{label}"></a>')
                parts.append('</section>')
            (output / "index.html").write_text('\n'.join(parts), encoding="utf-8")

    def full_views(self, output: Path) -> dict:
        previous = getattr(self, "require_foreground", False)
        self.require_foreground = True
        try:
            return self.shadows(output, full_view=True)
        finally:
            self.require_foreground = previous

    def shadows(self, output: Path, full_view: bool = False) -> dict:
        """Compare the opposite pass state, then return to the original setting."""
        output.mkdir(parents=True, exist_ok=False)
        report = {"pid": self.pid, "test": "full_view_pass" if full_view else "second_eye_primary_pass", "status": "incomplete"}
        begin = None
        try:
            baseline = self.request("shadow_query")["shadow"]
            report["baseline"] = baseline
            if full_view and not baseline.get("full_view_supported"):
                raise RuntimeError("Backend does not support the full-view comparison")
            configurable = "configured_enabled" in baseline
            if (not baseline.get("ready") or baseline.get("faulted") or baseline.get("test_active")
                    or (baseline.get("enabled") and not configurable)):
                raise RuntimeError("Shadow view pair is not verified and idle")
            changed_enabled = False if full_view else not baseline["enabled"]
            report["changed_enabled"] = changed_enabled
            self.wait_frames()
            self.capture(output / "baseline", "all")
            try:
                fields = {"enabled": changed_enabled} if configurable else {}
                if full_view:
                    fields["full_view"] = True
                self.assert_focus()
                begin = self.request("shadow_pass", seconds=45, **fields)
                report["begin"] = begin
                if begin["shadow"]["enabled"] != changed_enabled:
                    raise RuntimeError("Backend did not select the requested shadow state")
                if full_view and not begin["shadow"].get("full_view_enabled"):
                    raise RuntimeError("Backend did not enable the full-view comparison")
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
                if full_view and (not active.get("full_view_enabled") or
                        active["full_applied"] <= begin["shadow"]["full_applied"] or
                        active["full_applied"] != active["full_restored"]):
                    raise RuntimeError("Full-view comparison did not apply and restore cleanly")
            finally:
                if begin is not None:
                    report["disable"] = self.request("shadow_pass", seconds=0)
            restored = self.request("shadow_query")["shadow"]
            report["restored"] = restored
            if (restored.get("enabled") != baseline["enabled"] or restored.get("faulted")
                    or restored.get("test_active") or restored["applied"] != restored["restored"]
                    or restored.get("full_view_enabled", False)
                    or restored.get("full_applied", 0) != restored.get("full_restored", 0)
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
    console = commands.add_parser("console", help="Set one r.* console variable for 30 s via the console manager, capture, restore")
    console.add_argument("name")
    console.add_argument("--value", type=float, required=True)
    console.add_argument("--output", type=Path, required=True)
    console.add_argument("--layer", choices=("all", "projection"))
    reflections = commands.add_parser("reflections", help="Compare three reflection controls at zero one at a time, restoring each")
    reflections.add_argument("--output", type=Path, required=True)
    reflections.add_argument("--pid", type=int, help="Refuse a different game process")
    translucency = commands.add_parser("translucency", help="Capture full-resolution and blur material comparisons with restoration")
    translucency.add_argument("--output", type=Path, required=True)
    translucency.add_argument("--pid", type=int, help="Refuse a different game process")
    stereo = commands.add_parser("stereo", help="Compare reflection and translucency paths in one restored batch")
    stereo.add_argument("--output", type=Path, required=True)
    stereo.add_argument("--pid", type=int, help="Refuse a different game process")
    visibility = commands.add_parser("visibility", help="Compare occlusion and foliage paths, restoring each; keep WuWa focused")
    visibility.add_argument("--output", type=Path, required=True)
    visibility.add_argument("--pid", type=int, required=True, help="Refuse a different game process")
    candidates=commands.add_parser("stereo-candidates",help="Capture and restore the three targeted stereo candidates in one batch")
    candidates.add_argument("--output",type=Path,required=True)
    candidates.add_argument("--pid",type=int,required=True)
    values = commands.add_parser("reflection-values", help="Read the three reflection values without changing them")
    values.add_argument("--pid", type=int, help="Refuse a different game process")
    values.add_argument("--output", type=Path, help="Optional JSON receipt path")
    shadows = commands.add_parser("shadows")
    shadows.add_argument("--output", type=Path, required=True)
    full_views = commands.add_parser("full-views", help="Temporarily render both eyes as full views, then restore; keep WuWa focused")
    full_views.add_argument("--output", type=Path, required=True)
    full_views.add_argument("--pid", type=int, required=True)
    impostors = commands.add_parser("impostors", help="Compare source meshes and impostors with automatic restoration")
    impostors.add_argument("--output", type=Path, required=True)
    impostors.add_argument("--pid", type=int, required=True)
    lod = commands.add_parser("lod-inputs", help="Read-only LOD input trace and stationary scene captures")
    lod.add_argument("--view-uniforms", action="store_true", help="Also record bounded View uniform ownership on a supporting backend")
    lod.add_argument("--mesh-bindings", action="store_true", help="Also record bounded mesh bindings before vertex-factory dispatch; this is not GPU draw proof")
    lod.add_argument("--raw-snapshots", action="store_true", help="Also record bounded raw copies of both eye slots' view and view-state windows (equal values included) in lod.jsonl")
    lod.add_argument("--state-swap-mode", choices=STATE_SWAP_MODES, default="exchange")
    lod.add_argument("--state-swap", action="store_true", help="Diagnostic write: the two main views exchange view-state pointers for the trace window, restored every frame and at the end")
    lod.add_argument("--output", type=Path, required=True)
    lod.add_argument("--pid", type=int, required=True)
    lod.add_argument("--seconds", type=int, choices=range(1, 31), metavar="1..30", default=8)
    for command in (capture, graphics, console, reflections, translucency, stereo, visibility, candidates, shadows, full_views, impostors, lod):
        command.add_argument("--capture-source", choices=("simulator", "steamvr"), default="simulator",
                             help="Select an already running runtime; never switches or starts one")
    bench = commands.add_parser("bench", help="Fix bench: open a timed construct_mode (1..3) or target_swap window and exit")
    bench.add_argument("--pid", type=int, required=True)
    bench.add_argument("--op", choices=("construct_mode", "target_swap", "eye_swap", "second_eye", "lod_sync"), required=True)
    bench.add_argument("--mode", type=int, default=0, choices=range(0, 5))
    bench.add_argument("--seconds", type=int, required=True, choices=range(0, 61), metavar="0..60")
    swap_window = commands.add_parser("state-swap", help="Diagnostic: open (1..60 s) or end (0) the view-state swap and exit; it expires on its own")
    swap_window.add_argument("--seconds", type=int, required=True, choices=range(0, 61), metavar="0..60")
    swap_window.add_argument("--pid", type=int, required=True)
    swap_window.add_argument("--mode", choices=STATE_SWAP_MODES, default="exchange",
                             help="exchange (E3), first_for_both or second_for_both (both views share one state)")
    shadow_pass = commands.add_parser("shadow-pass")
    shadow_pass.add_argument("--seconds", type=int, default=0)
    trace = commands.add_parser("trace")
    trace.add_argument("--seconds", type=int, default=90)
    trace.add_argument("--until-stopped", action="store_true", help="Record until stopped, game exit or 1000 changes")
    record = commands.add_parser("record-motion", help="Record bounded local controller/camera sidecar; never generates input")
    record.add_argument("--seconds", type=int, choices=range(0, 301), metavar="0..300", default=120)
    record.add_argument("--recording-id", default="")
    planar = commands.add_parser("planar-probe", help="Observe reflection eye parameters without changing rendering")
    planar.add_argument("--seconds", type=int, choices=range(0, 121), metavar="0..120", default=60)
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
    if args.command in ("reflections", "reflection-values", "translucency", "stereo", "visibility", "stereo-candidates", "full-views", "impostors", "lod-inputs", "state-swap", "bench") and args.pid is not None and client.pid != args.pid:
        raise RuntimeError("Game process changed before graphics comparison")
    with client.exclusive():
        if args.command == "status":
            result = client.assert_live()
        elif args.command == "capture":
            result = client.capture(args.output, args.layer)
        elif args.command == "graphics":
            result = client.graphics(args.name, args.value, args.output, args.layer)
        elif args.command == "console":
            result = client.console(args.name, args.value, args.output, args.layer)
        elif args.command == "reflections":
            result = client.reflections(args.output)
        elif args.command == "translucency":
            result = client.translucency(args.output)
        elif args.command == "stereo":
            result = client.stereo(args.output)
        elif args.command == "visibility":
            result = client.visibility(args.output)
        elif args.command == "impostors":
            result = client.impostors(args.output)
        elif args.command == "lod-inputs":
            result = client.lod_inputs(args.output, args.seconds, view_uniforms=args.view_uniforms,
                                       mesh_bindings=args.mesh_bindings, raw_snapshots=args.raw_snapshots,
                                       state_swap=args.state_swap, state_swap_mode=args.state_swap_mode)
        elif args.command == "stereo-candidates":
            result = client.stereo_candidates(args.output)
        elif args.command == "reflection-values":
            result = client.reflection_values()
            if args.output is not None:
                args.output.parent.mkdir(parents=True, exist_ok=True)
                write_json(args.output, result)
        elif args.command == "shadows":
            result = client.shadows(args.output)
        elif args.command == "full-views":
            result = client.full_views(args.output)
        elif args.command == "shadow-pass":
            result = client.request("shadow_pass", seconds=args.seconds)
        elif args.command == "bench":
            reply = client.request(args.op, seconds=args.seconds, mode=args.mode)
            result = reply.get("second_eye") or reply["shadow"]
        elif args.command == "state-swap":
            result = client.state_swap_window(args.seconds, args.mode)
        elif args.command == "trace":
            result = client.request("trace", seconds=0 if args.until_stopped else args.seconds, until_stopped=args.until_stopped)
        elif args.command == "record-motion":
            result = client.request("record_motion", seconds=args.seconds, recording_id=args.recording_id)
        elif args.command == "planar-probe":
            result = client.request("planar_probe", seconds=args.seconds)
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
