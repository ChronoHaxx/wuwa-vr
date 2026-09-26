"""WuWa VR portable launcher: a loopback-only helper for the player package.

It serves one local page, applies builds through the packaged PowerShell
helpers and opens the player guide. It never starts Python child processes,
so it behaves the same under the package's private Python as in development.
Read-only files live in the package's app folder; writable state lives in
%LOCALAPPDATA%\\WuWa VR Launcher (WUWA_VR_DATA overrides it for tests).
"""
from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
from datetime import datetime
import hashlib
import importlib.util
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import msvcrt
import os
from pathlib import Path, PurePosixPath
import re
import secrets
import subprocess
import sys
import threading
import time
from urllib.parse import unquote, urlparse
from urllib.request import urlopen
import webbrowser
import winreg

APP_ID = "wuwa-vr-player-launcher"
GAME_PROCESSES = {"client-win64-shipping.exe", "wuthering waves.exe", "custom_uevr_injector.exe", "uevrinjector.exe"}
STATIC_TYPES = {".html": "text/html; charset=utf-8", ".css": "text/css; charset=utf-8",
                ".js": "text/javascript; charset=utf-8", ".svg": "image/svg+xml", ".png": "image/png",
                ".jpg": "image/jpeg", ".webp": "image/webp", ".mp4": "video/mp4", ".webm": "video/webm",
                ".vtt": "text/vtt; charset=utf-8", ".pdf": "application/pdf"}
# Files a running package is expected to gain; they are not integrity failures.
EXPECTED_WRITES = re.compile(r"^app/runtime/[^/]+/Custom_UEVR_Injector\.txt$")
IDLE_EXIT_SECONDS = 20 * 60
EXIT_REPORTED = 3


class Config:
    """Resolved locations. Nothing is created until the launcher starts."""

    def __init__(self, app=None, data=None, profile=None):
        self.app = Path(app or Path(__file__).resolve().parent.parent).resolve()
        self.package = self.app.parent
        if data is None:
            override = os.environ.get("WUWA_VR_DATA")
            if override:
                data = Path(override)
            else:
                local = os.environ.get("LOCALAPPDATA")
                if not local:
                    raise RuntimeError("LOCALAPPDATA is not set, so settings have nowhere to be saved.")
                data = Path(local) / "WuWa VR Launcher"
        self.data = Path(data).absolute()
        if profile is None:
            roaming = os.environ.get("APPDATA")
            if not roaming:
                raise RuntimeError("APPDATA is not set, so the UEVR profile cannot be located.")
            profile = Path(roaming) / "UnrealVRMod" / "Client-Win64-Shipping"
        self.profile = Path(profile)

    @property
    def settings_file(self):
        return self.data / "settings.json"

    @property
    def logs(self):
        return self.data / "logs"


CONFIG: Config | None = None
TOKEN = secrets.token_urlsafe(32)
BASE_URL = ""
LOCK = threading.Lock()
JOB = {"running": False, "kind": "", "message": "Ready", "output": "", "error": False, "code": None}
RECORDING = {"running": False, "stopFile": "", "folder": ""}
LAST_REQUEST = time.monotonic()
VERIFY_RESULT: dict = {}
SERVER = None


def config() -> Config:
    if CONFIG is None:
        raise RuntimeError("Launcher is not configured")
    return CONFIG


def read_json(path, default=None):
    try:
        return json.loads(Path(path).read_text(encoding="utf-8-sig"))
    except (OSError, ValueError):
        return {} if default is None else default


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, ensure_ascii=False), encoding="utf-8")
    temporary.replace(path)


def log(message):
    try:
        folder = config().logs
        folder.mkdir(parents=True, exist_ok=True)
        stamp = datetime.now().astimezone()
        with (folder / f"launcher-{stamp:%Y%m%d}.log").open("a", encoding="utf-8") as handle:
            handle.write(f"{stamp.isoformat(timespec='seconds')} {message}\n")
    except (OSError, RuntimeError):
        pass


def redact(text):
    """Keep diagnostics shareable: hide the Windows account name in paths."""
    text = str(text)
    for name in ("USERPROFILE", "LOCALAPPDATA", "APPDATA"):
        value = os.environ.get(name)
        if value and len(value) > 3:
            text = text.replace(value, "%" + name + "%")
    user = os.environ.get("USERNAME")
    if user and len(user) > 2:
        text = re.sub(re.escape(user), "%USERNAME%", text, flags=re.IGNORECASE)
    return text


def portable_info():
    return read_json(config().app / "portable.json")


def catalog():
    builds = read_json(config().app / "dev/wuwa-builds.json").get("builds", [])
    return [b for b in builds if isinstance(b, dict) and isinstance(b.get("id"), str)]


def build_by_id(build_id):
    return next((b for b in catalog() if b["id"] == build_id), None)


def runtime_folder(build):
    return (config().app / build["runtime"]).resolve()


def has_non_ascii(path):
    return any(ord(c) > 127 for c in str(path))


def processes():
    """Game, game launcher child and injector processes, by image name only."""
    found = []

    class Entry(ctypes.Structure):
        _fields_ = [("size", wintypes.DWORD), ("usage", wintypes.DWORD), ("pid", wintypes.DWORD),
                    ("heap", ctypes.c_size_t), ("module", wintypes.DWORD), ("threads", wintypes.DWORD),
                    ("parent", wintypes.DWORD), ("priority", wintypes.LONG), ("flags", wintypes.DWORD),
                    ("name", wintypes.WCHAR * 260)]
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
    kernel.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    kernel.Process32FirstW.argtypes = kernel.Process32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(Entry)]
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = kernel.CreateToolhelp32Snapshot(2, 0)
    if handle == ctypes.c_void_p(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        entry = Entry()
        entry.size = ctypes.sizeof(entry)
        available = kernel.Process32FirstW(handle, ctypes.byref(entry))
        while available:
            if entry.name.lower() in GAME_PROCESSES:
                found.append({"pid": entry.pid, "name": entry.name})
            available = kernel.Process32NextW(handle, ctypes.byref(entry))
    finally:
        kernel.CloseHandle(handle)
    return found


def injector_runtime():
    try:
        for line in (config().profile / "injector_config.txt").read_text(encoding="utf-8-sig").splitlines():
            if line.startswith("custom_var_urvr_folder="):
                return line.split("=", 1)[1].strip()
    except (OSError, UnicodeDecodeError):
        pass
    return ""


def runtime_manifest(path):
    if not path:
        return False
    try:
        file = Path(path)
        manifest = json.loads(file.read_text(encoding="utf-8-sig"))
        library = Path(manifest["runtime"]["library_path"])
        return (library if library.is_absolute() else file.parent / library).is_file()
    except (OSError, ValueError, KeyError, TypeError):
        return False


def simulator_manifest(path):
    try:
        runtime = json.loads(Path(path).read_text(encoding="utf-8-sig"))["runtime"]
        return runtime.get("name") == "OpenXR Simulator" and Path(runtime["library_path"]).name.lower() == "openxr_simulator.dll"
    except (OSError, ValueError, KeyError, TypeError):
        return False


def openxr_status():
    """Read-only; changing the runtime requires an explicit guarded POST."""
    previous = ""
    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Khronos\OpenXR\1", 0,
                            winreg.KEY_READ | winreg.KEY_WOW64_64KEY) as key:
            active = str(winreg.QueryValueEx(key, "ActiveRuntime")[0])
            try:
                previous = str(winreg.QueryValueEx(key, "PreviousActiveRuntime")[0])
            except OSError:
                pass
    except OSError:
        return {"available": False, "message": "No OpenXR runtime is selected. Open SteamVR, Quest Link or Virtual Desktop and set it as the OpenXR runtime."}
    lower = active.lower()
    name = ("SteamVR" if "steamxr" in lower else "Meta Quest Link" if "oculus" in lower else
            "Virtual Desktop" if "virtualdesktop" in lower or "virtual desktop" in lower else
            "OpenXR Simulator (development)" if "simulator" in lower else Path(active).stem)
    ok = runtime_manifest(active)
    simulator = config().app / "dev-tools/OpenXR-Simulator/openxr_simulator.json"
    is_simulator = simulator_manifest(active) or (not Path(active).is_file() and Path(active).name.lower() == "openxr_simulator.json")
    headset = previous if is_simulator else active
    return {"available": ok, "name": name, "manifest": active,
            "isSimulator": is_simulator, "canSimulator": runtime_manifest(simulator) and ok,
            "canHeadset": runtime_manifest(headset) and not simulator_manifest(headset),
            "message": "" if ok else "The selected OpenXR runtime file is missing. Re-select your headset software as the OpenXR runtime."}


def require_runtime(mode, expected):
    if mode not in ("headset", "simulator"):
        raise ValueError("Choose headset or simulator.")
    require_idle()
    current = openxr_status()
    if not isinstance(expected, str) or not expected or current.get("manifest") != expected:
        raise ValueError("The runtime changed. Refresh and choose again.")
    if not current.get("canHeadset" if mode == "headset" else "canSimulator"):
        raise ValueError("Runtime unavailable. Select your headset runtime in its own app first; the simulator also needs to be included in this package.")


def switch_runtime(mode, expected):
    # Recheck after entering the job, and again inside the elevated helper.
    require_runtime(mode, expected)
    result = powershell("-File", str(config().app / "dev/wuwa-runtime.ps1"), "-Mode", mode,
                        "-ExpectedActive", expected, "-DataRoot", str(config().data))
    if result.returncode:
        raise OperationError(result.returncode, last_error_line(result.stdout) or "Runtime switch failed.")
    return result.stdout.strip()


# ------------------------------------------------------------------ game ---
def registry_entries():
    """Uninstall entries mentioning Wuthering Waves. Read-only."""
    roots = [(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall", winreg.KEY_WOW64_64KEY),
             (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall", 0),
             (winreg.HKEY_CURRENT_USER, r"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall", 0)]
    entries = []
    for hive, path, flag in roots:
        try:
            with winreg.OpenKey(hive, path, 0, winreg.KEY_READ | flag) as root:
                index = 0
                while True:
                    try:
                        name = winreg.EnumKey(root, index)
                    except OSError:
                        break
                    index += 1
                    try:
                        with winreg.OpenKey(root, name) as key:
                            values = {}
                            for field in ("DisplayName", "InstallLocation", "DisplayIcon"):
                                try:
                                    values[field] = str(winreg.QueryValueEx(key, field)[0])
                                except OSError:
                                    values[field] = ""
                    except OSError:
                        continue
                    if "wuthering waves" in values["DisplayName"].lower():
                        entries.append({"key": name, **values})
        except OSError:
            continue
    return entries


def epic_entries():
    folder = Path(os.environ.get("PROGRAMDATA", r"C:\ProgramData")) / "Epic/EpicGamesLauncher/Data/Manifests"
    entries = []
    try:
        for item in folder.glob("*.item"):
            record = read_json(item)
            if "wuthering waves" in str(record.get("DisplayName", "")).lower():
                entries.append({"location": str(record.get("InstallLocation", ""))})
    except OSError:
        pass
    return entries


def clean_icon_path(value):
    value = value.strip().strip('"')
    return re.sub(r",\s*-?\d+$", "", value).strip('"')


def detect_games(registry=registry_entries, epic=epic_entries, default=r"C:\Program Files\Wuthering Waves\launcher.exe"):
    """Candidate ways to start the game, best first. Paths are only suggestions."""
    found, seen = [], set()

    def add(kind, label, path=""):
        key = (kind, str(path).lower())
        if key not in seen:
            seen.add(key)
            found.append({"kind": kind, "label": label, "path": str(path)})
    for entry in registry():
        location = entry.get("InstallLocation", "")
        if entry.get("key", "").startswith("Steam App "):
            add("steam", "Steam installation detected. Injection previously failed and has not been retested.", location)
            continue
        icon = clean_icon_path(entry.get("DisplayIcon", ""))
        candidates = [Path(icon)] if icon.lower().endswith("launcher.exe") else []
        if location:
            candidates.append(Path(location) / "launcher.exe")
        for candidate in candidates:
            if candidate.is_absolute() and candidate.is_file():
                add("official", "Official Wuthering Waves launcher", candidate)
                break
    for entry in epic():
        add("epic", "Epic installation detected. Injection is untested.", entry["location"])
    if Path(default).is_file():
        add("official", "Official Wuthering Waves launcher", Path(default))
    return found


def validate_launcher(path):
    if not isinstance(path, str) or not path.strip():
        raise ValueError("Enter the full path to launcher.exe.")
    path = path.strip().strip('"')
    candidate = Path(path)
    if any(c in path for c in '\r\n"') or not candidate.is_absolute():
        raise ValueError("Enter the complete path, for example C:\\Program Files\\Wuthering Waves\\launcher.exe.")
    if candidate.is_dir():
        candidate = candidate / "launcher.exe"
    if candidate.name.lower() != "launcher.exe":
        raise ValueError("Choose the game's launcher.exe. Manual start for Steam/Epic is experimental; injection is not verified.")
    if not candidate.is_file():
        raise ValueError(f"launcher.exe was not found at {candidate}.")
    return str(candidate.resolve())


def settings():
    saved = read_json(config().settings_file)
    return saved if isinstance(saved, dict) else {}


def save_settings(changes):
    current = settings()
    current.update(changes)
    current["updatedAt"] = datetime.now().astimezone().isoformat(timespec="seconds")
    write_json(config().settings_file, current)
    return current


def game_status():
    detected = detect_games()
    saved = settings()
    mode = saved.get("gameStart")
    launcher = saved.get("gameLauncher", "")
    problem = ""
    if mode == "launcher":
        try:
            launcher = validate_launcher(launcher)
        except ValueError as error:
            problem = str(error)
    elif mode != "manual":
        official = next((d for d in detected if d["kind"] == "official"), None)
        mode, launcher = ("launcher", official["path"]) if official else ("manual", "")
    return {"mode": mode, "launcher": launcher, "saved": "gameStart" in saved, "problem": problem, "detected": detected}


# ----------------------------------------------------------------- state ---
def launch_state():
    """Progress written by the elevated startup, confined to our data folder."""
    state = read_json(config().data / "launch-state.json")
    if not isinstance(state, dict):
        return {}
    shown = {k: state.get(k) for k in ("phase", "message", "outcome", "buildId", "started") if k in state}
    run = state.get("runDir")
    if isinstance(run, str) and run:
        try:
            folder = Path(run).resolve()
            if folder.parent == (config().data / "runs").resolve():
                shown["runId"] = folder.name
        except OSError:
            pass
    return shown


def status():
    info = portable_info()
    state = read_json(config().data / "state.json")
    running = processes()
    builds = []
    for build in catalog():
        builds.append({key: build.get(key) for key in ("id", "name", "role", "summary", "known", "savedAt", "sha256", "description")})
    selected = build_by_id(state.get("selected", "")) if isinstance(state, dict) else None
    active = injector_runtime()
    matches = bool(selected and active and Path(active).resolve() == runtime_folder(selected))
    saved = settings()
    return {
        "package": {"name": info.get("packageName", "WuWa VR Launcher"), "id": info.get("packageId", ""),
                    "createdAt": info.get("createdAt", ""), "defaultBuild": info.get("defaultBuild", ""),
                    "private": info.get("private", True), "folder": redact(config().package),
                    "nonAsciiPath": has_non_ascii(config().package)},
        "builds": builds,
        "selected": selected["id"] if selected else "",
        "selectionMatches": matches,
        "profileExists": config().profile.is_dir(),
        "originalBackup": bool(isinstance(state, dict) and state.get("originalBackup")),
        "restoredOriginalAt": state.get("restoredOriginalAt", "") if isinstance(state, dict) else "",
        "processes": running,
        "gameRunning": any(p["name"].lower() not in ("custom_uevr_injector.exe", "uevrinjector.exe") for p in running),
        "injectorRunning": any(p["name"].lower() in ("custom_uevr_injector.exe", "uevrinjector.exe") for p in running),
        "openxr": openxr_status(),
        "game": game_status(),
        "riskAcknowledged": bool(saved.get("riskAcknowledgedAt")),
        "job": dict(JOB),
        "launch": launch_state(),
        "integrity": dict(VERIFY_RESULT),
        "dataFolder": redact(config().data),
        "recording": {"running": RECORDING["running"], "available": (config().app / "dev-tools/wuwa-recorder.exe").is_file()},
        "comparisons": {"available": all((config().app / "dev-tools" / name).is_file()
                                          for name in ("steamvr-capture.exe", "openvr_api.dll"))},
    }


# ------------------------------------------------------------ operations ---
def powershell(*arguments, encoding="oem"):
    """Run a packaged helper with Windows PowerShell 5.1, without a window."""
    environment = {key: value for key, value in os.environ.items() if key.upper() != "PSMODULEPATH"}
    executable = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32/WindowsPowerShell/v1.0/powershell.exe"
    if not executable.is_file():
        raise RuntimeError("Windows PowerShell was not found. It is part of Windows 10/11; repair Windows before retrying.")
    # Legacy helpers use the OEM code page; the build helper emits UTF-8 JSON
    # so paths survive accounts and package folders with non-ASCII characters.
    return subprocess.run([str(executable), "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", *arguments],
                          cwd=config().app, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                          env=environment, text=True, encoding=encoding, errors="replace",
                          creationflags=subprocess.CREATE_NO_WINDOW)


def build_command(action, build_id=None, reset=False):
    arguments = ["-File", str(config().app / "dev/wuwa-build.ps1"), "-Action", action,
                 "-DataRoot", str(config().data)]
    if build_id:
        arguments += ["-Id", build_id]
    if reset:
        arguments.append("-ResetToSupplied")
    return arguments


def run_build(action, build_id=None, reset=False):
    result = powershell(*build_command(action, build_id, reset), encoding="utf-8")
    text = result.stdout.strip()
    if result.returncode:
        raise OperationError(result.returncode, last_error_line(text) or "The operation failed.")
    return text


class OperationError(RuntimeError):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


def last_error_line(text):
    """PowerShell error records end with location noise; keep the reason."""
    lines = [line.strip() for line in str(text).splitlines() if line.strip()]
    for line in lines:
        if not line.startswith(("At ", "+ ", "CategoryInfo", "FullyQualifiedErrorId")) and not line.startswith("+"):
            return line
    return lines[0] if lines else ""


LAUNCH_RESULTS = {
    0: "Windows permission accepted. Press Play in your chosen game launcher. Steam/Epic injection remains unverified. Progress appears below.",
    2: "A launch is already in progress. Wait for it to finish, or use Stop waiting.",
    3: "A Windows permission prompt is already open. Look for it in the taskbar and answer it.",
    4: "Windows permission was declined, so nothing was started. Choose Apply & launch again when ready.",
}


def require_idle():
    if processes():
        raise ValueError("Close Wuthering Waves and any waiting injector first. The launcher never closes the game for you.")


def require_build(build_id):
    build = build_by_id(build_id)
    if build is None:
        raise ValueError("Choose one of the builds in this package.")
    return build


def apply_build(build_id, reset=False):
    output = run_build("Select", build_id, reset)
    try:
        result = json.loads(output)
    except ValueError:
        result = {}
    if result.get("changed") is False:
        return "This build is already applied. Your saved settings are unchanged."
    return "Build applied. Your previous UEVR settings were backed up first."


def launch_build(build_id):
    apply_build(build_id)
    JOB["message"] = "Waiting for Windows permission. Choose Yes in the Windows prompt."
    result = powershell(*build_command("Launch", build_id), encoding="utf-8")
    detail = last_error_line(result.stdout.strip())
    if result.returncode in LAUNCH_RESULTS and result.returncode != 0:
        raise OperationError(result.returncode, LAUNCH_RESULTS[result.returncode])
    if result.returncode:
        raise OperationError(result.returncode, "Launch did not start: " + (detail or "unknown reason."))
    return LAUNCH_RESULTS[0]


def check_ready(build_id):
    apply_needed = ""
    build = require_build(build_id)
    active = injector_runtime()
    if not active or Path(active).resolve() != runtime_folder(build):
        apply_needed = " This build is not applied yet; Apply & launch will apply it first."
    arguments = ["-File", str(config().app / "dev/start-wuwa-build.ps1"), "-Id", build_id, "-CheckOnly", "-NoDialog",
                 "-DataRoot", str(config().data)]
    result = powershell(*arguments)
    text = last_error_line(result.stdout.strip())
    if result.returncode:
        if apply_needed and "Apply this build" in text:
            return "Ready to apply." + apply_needed
        raise OperationError(result.returncode, text or "Readiness check failed.")
    return "Ready: game closed, build files verified and headset runtime found. Nothing was changed." + apply_needed


def cancel_launch():
    state = read_json(config().data / "launch-state.json")
    cancel = state.get("cancelPath") if isinstance(state, dict) else None
    if not isinstance(cancel, str) or not cancel:
        raise ValueError("No launch is waiting.")
    target = Path(cancel).resolve()
    if target.parent.parent != (config().data / "runs").resolve() or target.name != "cancel.request":
        raise ValueError("The recorded launch is not from this launcher.")
    target.write_text("cancel\n", encoding="utf-8")
    return "Asked the waiting startup to stop. The game is never closed by the launcher."


def verify_package():
    """Compare every packaged file with manifest.json. Read-only."""
    package = config().package
    manifest = read_json(package / "manifest.json")
    files = manifest.get("files") if isinstance(manifest, dict) else None
    if not isinstance(files, dict) or not files:
        raise ValueError("manifest.json is missing or unreadable. Extract the whole ZIP again.")
    missing, changed = [], []
    for relative, expected in sorted(files.items()):
        path = PurePosixPath(relative) if isinstance(relative, str) else PurePosixPath("/")
        if path.is_absolute() or ".." in path.parts or "\\" in relative or ":" in relative:
            raise ValueError("manifest.json contains an invalid path.")
        file = package.joinpath(*path.parts)
        if not file.is_file():
            missing.append(relative)
            continue
        with file.open("rb") as handle:
            if hashlib.sha256(handle.read()).hexdigest() != expected:
                changed.append(relative)
    listed = {name.casefold() for name in files} | {"manifest.json", "sha256sums.txt"}
    extra = []
    for folder in ("app", "python"):
        for file in (package / folder).rglob("*"):
            if file.is_file():
                relative = file.relative_to(package).as_posix()
                if relative.casefold() not in listed and not EXPECTED_WRITES.match(relative) and "__pycache__" not in relative:
                    extra.append(relative)
    ok = not missing and not changed
    result = {"ok": ok, "checked": len(files), "missing": missing[:20], "changed": changed[:20],
              "extra": extra[:20], "time": datetime.now().astimezone().isoformat(timespec="seconds")}
    VERIFY_RESULT.clear()
    VERIFY_RESULT.update(result)
    if not ok:
        raise ValueError(f"{len(missing)} missing and {len(changed)} changed file(s). Extract the complete ZIP to a new folder."
                         + (" First problem: " + (missing + changed)[0] if missing or changed else ""))
    note = f" {len(extra)} unexpected file(s) were found; they were not used." if extra else ""
    return f"All {len(files)} packaged files match the manifest.{note}"


def diagnostics():
    info = status()
    lines = [f"WuWa VR Launcher diagnostics, {datetime.now().astimezone().isoformat(timespec='seconds')}",
             f"Package: {info['package']['name']} ({info['package']['id']}), created {info['package']['createdAt']}",
             f"Package folder: {info['package']['folder']}; data folder: {info['dataFolder']}",
             f"Selected build: {info['selected'] or 'none'}; injector points at it: {info['selectionMatches']}",
             f"OpenXR: {info['openxr'].get('name', 'unavailable')} ({'ok' if info['openxr'].get('available') else info['openxr'].get('message')})",
             f"Game start: {info['game']['mode']} {redact(info['game']['launcher'])} {info['game']['problem']}".rstrip(),
             f"Running: {', '.join(p['name'] for p in info['processes']) or 'none'}",
             f"Last job: {info['job'].get('message')} {redact(info['job'].get('output', ''))[:400]}",
             f"Last launch: {json.dumps(info['launch'])}",
             f"Integrity: {json.dumps(info['integrity']) if info['integrity'] else 'not checked this session'}",
             f"Python: {sys.version.split()[0]}; Windows: {sys.getwindowsversion().major}.{sys.getwindowsversion().build}"]
    try:
        newest = sorted(config().logs.glob("launcher-*.log"))[-1]
        tail = newest.read_text(encoding="utf-8", errors="replace").splitlines()[-25:]
        lines += ["", "Recent launcher log:", *(redact(line) for line in tail)]
    except (IndexError, OSError):
        pass
    return "\n".join(lines) + "\n"


def begin_job(kind, message, operation):
    with LOCK:
        if JOB["running"]:
            raise ValueError("Another operation is still running.")
        JOB.update(running=True, kind=kind, message=message, output="", error=False, code=None)

    def work():
        try:
            output = operation()
            JOB.update(message="Finished", output=output or "Done", error=False, code=0)
            log(f"{kind}: {output}")
        except OperationError as error:
            JOB.update(message="Needs attention", output=str(error), error=True, code=error.code)
            log(f"{kind} failed ({error.code}): {redact(error)}")
        except Exception as error:  # Report every failure on the page.
            JOB.update(message="Needs attention", output=str(error), error=True, code=1)
            log(f"{kind} failed: {redact(error)}")
        finally:
            with LOCK:
                JOB["running"] = False
    threading.Thread(target=work, daemon=True).start()


def open_folder(which):
    folders = {"data": config().data, "logs": config().logs, "package": config().package, "profile": config().profile,
               "recordings": config().data / "recordings", "comparisons": config().data / "comparisons"}
    folder = folders.get(which)
    if folder is None:
        raise ValueError("Unknown folder.")
    if which in ("data", "logs", "recordings", "comparisons"):
        folder.mkdir(parents=True, exist_ok=True)
    if not folder.is_dir():
        raise ValueError("That folder does not exist yet.")
    os.startfile(str(folder))  # Opens File Explorer; nothing is executed.


def record_playtest(video_only=False, fps=30, eye_width=1024):
    if type(fps) is not int or fps not in (30,45,60): raise ValueError('Choose 30, 45 or 60 fps.')
    if type(eye_width) is not int or eye_width not in (720,1024,1280): raise ValueError('Choose 720, 1024 or 1280 pixels per eye.')
    folder=config().data / "recordings" / (datetime.now().strftime("%Y%m%d-%H%M%S")+'-'+secrets.token_hex(2))
    folder.parent.mkdir(parents=True,exist_ok=True)
    stop_file=folder.with_suffix('.stop')
    RECORDING.update(running=True,stopFile=str(stop_file),folder=str(folder))
    try:
        spec=importlib.util.spec_from_file_location('wuwa_recording',config().app/'dev/record-wuwa.py')
        module=importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        args=argparse.Namespace(output=folder,seconds=300,stop_file=stop_file,video_only=video_only,
            recorder=config().app/'dev-tools/wuwa-recorder.exe',openvr=config().app/'dev-tools/openvr_api.dll',
            pid=None,fps=fps,eye_width=eye_width,profile=config().profile)
        module.record(args)
        return 'Recording saved. Open recordings, then replay.html for controller/camera annotations or clean-sbs.mp4 for clean footage. '+str(folder)
    finally:
        RECORDING.update(running=False,stopFile='')


def compare_graphics():
    """One bounded batch in the current scene; the helper restores each lease."""
    spec = importlib.util.spec_from_file_location("wuwa_graphics", config().app / "dev/wuwa-test.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    client = module.LiveTest(profile=config().profile, capture_source="steamvr")
    client.require_foreground = True
    supported = client.assert_live().get("graphics_test_cvars", [])
    if not isinstance(supported, list) or any(name not in supported for _, _, name, _ in module.STEREO_SOURCES):
        raise RuntimeError("The running build does not support this comparison. Use the prepared stereo-labels candidate.")
    folder = config().data / "comparisons" / (datetime.now().strftime("%Y%m%d-%H%M%S") + '-' + secrets.token_hex(2))
    with client.exclusive():
        # Wait for the user to return to WuWa. Observe focus; never move it.
        deadline = time.monotonic() + 120
        while module.foreground_pid() != client.pid:
            client.assert_live()
            if time.monotonic() >= deadline:
                raise RuntimeError("WuWa did not regain foreground focus; no graphics settings were changed. Start again when ready.")
            time.sleep(.2)
        with LOCK:
            JOB["message"] = "Comparing the current scene; keep the view still until the batch finishes"
        client.stereo(folder)
    return "Comparison saved and original graphics values restored. Open comparisons, then index.html. Visible effects still need review. " + str(folder)


def static_file(url_path):
    """Serve only files inside app/site, by extension."""
    if not url_path.startswith("/guide/"):
        return None
    relative = unquote(url_path[len("/guide/"):]) or "index.html"
    if "\\" in relative or ":" in relative or relative.startswith("/"):
        return None
    base = (config().app / "site").resolve()
    file = (base / relative).resolve()
    if base not in file.parents or not file.is_file() or file.suffix.lower() not in STATIC_TYPES:
        return None
    return file, STATIC_TYPES[file.suffix.lower()]


# ------------------------------------------------------------------ HTTP ---
class Handler(BaseHTTPRequestHandler):
    server_version = "WuWaVRLauncher"
    sys_version = ""

    def log_message(self, *args):
        pass

    def reply(self, code, data, content_type="application/json; charset=utf-8"):
        if not isinstance(data, bytes):
            data = json.dumps(data).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("Content-Security-Policy", "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self'; media-src 'self'; frame-ancestors 'none'; form-action 'none'")
        self.end_headers()
        self.wfile.write(data)

    def valid_host(self):
        return self.headers.get("Host") == urlparse(BASE_URL).netloc

    def do_GET(self):
        global LAST_REQUEST
        if not self.valid_host():
            return self.reply(403, {"error": "Local access only"})
        LAST_REQUEST = time.monotonic()
        path = urlparse(self.path).path
        try:
            if path == "/":
                html = (config().app / "dev/wuwa-player.html").read_text(encoding="utf-8").replace("__TOKEN__", TOKEN)
                return self.reply(200, html.encode("utf-8"), "text/html; charset=utf-8")
            if path == "/api/status":
                return self.reply(200, status())
            if path == "/api/identity":
                return self.reply(200, {"app": APP_ID, "root": str(config().app), "pid": os.getpid()})
            if path == "/api/diagnostics":
                return self.reply(200, diagnostics().encode("utf-8"), "text/plain; charset=utf-8")
            if path == "/guide":
                self.send_response(302)
                self.send_header("Location", "/guide/index.html")
                self.send_header("Content-Length", "0")
                self.end_headers()
                return None
            found = static_file(path)
            if found:
                return self.reply(200, found[0].read_bytes(), found[1])
        except Exception as error:
            log(f"GET {path} failed: {redact(error)}")
            return self.reply(500, {"error": str(error)})
        return self.reply(404, {"error": "Not found"})

    def do_POST(self):
        global LAST_REQUEST
        if not self.valid_host() or self.headers.get("Origin") != BASE_URL or self.headers.get("X-WuWa-Token") != TOKEN:
            # Drain only small, bounded bodies before closing. On Windows an
            # unread POST can reset TCP and hide the intended 403 response.
            previous_timeout = self.connection.gettimeout()
            try:
                length = int(self.headers.get("Content-Length", 0))
                if 0 < length < 8192:
                    self.connection.settimeout(1)
                    self.rfile.read(length)
            except (ValueError, OSError):
                pass
            finally:
                self.connection.settimeout(previous_timeout)
            return self.reply(403, {"error": "Use the launcher page opened by WuWa VR Launcher.exe."})
        LAST_REQUEST = time.monotonic()
        try:
            length = int(self.headers.get("Content-Length", 0))
            if not 0 < length < 8192:
                raise ValueError("Invalid request size")
            body = json.loads(self.rfile.read(length))
            if not isinstance(body, dict):
                raise ValueError("Expected an action object")
            path = urlparse(self.path).path
            immediate = self.post_action(path, body)
            if immediate is None:
                return self.reply(404, {"error": "Unknown action"})
            return self.reply(202 if immediate is True else 200, {"ok": True} if immediate is True else immediate)
        except (ValueError, KeyError) as error:
            return self.reply(400, {"error": str(error)})
        except Exception as error:
            log(f"POST failed: {redact(error)}")
            return self.reply(500, {"error": str(error)})

    def post_action(self, path, body):
        if path == "/api/compare-graphics":
            if not any(p['name'].lower() == 'client-win64-shipping.exe' for p in processes()):
                raise ValueError('Launch WuWa through SteamVR before comparing graphics.')
            if not all((config().app/'dev-tools'/name).is_file() for name in ('steamvr-capture.exe','openvr_api.dll')):
                raise ValueError('SteamVR capture is not included in this package.')
            begin_job('comparison', 'Return to WuWa; the comparison will start when the game has focus', compare_graphics)
            return True
        if path == "/api/record-start":
            fps,eye_width=body.get('fps',30),body.get('eyeWidth',1024)
            if type(fps) is not int or fps not in (30,45,60): raise ValueError('Choose 30, 45 or 60 fps.')
            if type(eye_width) is not int or eye_width not in (720,1024,1280): raise ValueError('Choose 720, 1024 or 1280 pixels per eye.')
            if not any(p['name'].lower()=='client-win64-shipping' or p['name'].lower()=='client-win64-shipping.exe' for p in processes()):
                raise ValueError('Launch WuWa through SteamVR before recording.')
            if not (config().app/'dev-tools/wuwa-recorder.exe').is_file(): raise ValueError('Recorder is not included in this package.')
            begin_job('recording',f'Recording stereo video at a target of {fps} fps (5 minutes max)',lambda: record_playtest(body.get('videoOnly') is True,fps,eye_width))
            return True
        if path == "/api/record-stop":
            if not RECORDING['running'] or not RECORDING['stopFile']: raise ValueError('No recording is active.')
            Path(RECORDING['stopFile']).write_text('stop',encoding='ascii')
            return {"ok":True,"message":"Finishing video and replay."}
        if path == "/api/settings":
            changes = {}
            if "riskAcknowledged" in body:
                if body["riskAcknowledged"] is not True:
                    changes["riskAcknowledgedAt"] = ""
                else:
                    changes["riskAcknowledgedAt"] = datetime.now().astimezone().isoformat(timespec="seconds")
            if "gameStart" in body:
                if body["gameStart"] == "manual":
                    changes.update(gameStart="manual", gameLauncher="")
                elif body["gameStart"] == "launcher":
                    changes.update(gameStart="launcher", gameLauncher=validate_launcher(body.get("gameLauncher")))
                else:
                    raise ValueError("Choose how the game is started.")
            if not changes:
                raise ValueError("Nothing to save.")
            if JOB["running"] and "gameStart" in changes:
                raise ValueError("Wait for the current operation to finish.")
            save_settings(changes)
            return {"ok": True}
        if path == "/api/runtime":
            mode, expected = body.get("mode"), body.get("expectedActive")
            require_runtime(mode, expected)
            begin_job("runtime", "Selecting " + mode + " runtime; accept Windows' prompt",
                      lambda: switch_runtime(mode, expected))
            return True
        if path == "/api/apply":
            build = require_build(body.get("id"))
            require_idle()
            begin_job("apply", "Applying " + build["name"], lambda: apply_build(build["id"], body.get("reset") is True))
            return True
        if path == "/api/launch":
            build = require_build(body.get("id"))
            if not settings().get("riskAcknowledgedAt"):
                raise ValueError("Read the account-risk notice and tick the box before launching.")
            require_idle()
            game = game_status()
            if game["problem"]:
                raise ValueError(game["problem"])
            if not game["saved"]:
                save_settings({"gameStart": game["mode"], "gameLauncher": game["launcher"]})
            begin_job("launch", "Applying " + build["name"], lambda: launch_build(build["id"]))
            return True
        if path == "/api/check":
            build = require_build(body.get("id"))
            begin_job("check", "Checking readiness", lambda: check_ready(build["id"]))
            return True
        if path == "/api/restore":
            require_idle()
            begin_job("restore", "Restoring your settings from before WuWa VR",
                      lambda: (run_build("Restore"), "Your UEVR settings from before this launcher are restored. The mod's settings were kept in a backup.")[1])
            return True
        if path == "/api/verify":
            begin_job("verify", "Checking package files", verify_package)
            return True
        if path == "/api/cancel":
            return {"ok": True, "message": cancel_launch()}
        if path == "/api/open":
            open_folder(body.get("folder"))
            return {"ok": True}
        if path == "/api/stop":
            if JOB["running"]:
                raise ValueError("Wait for the current operation to finish before stopping the launcher.")
            threading.Thread(target=stop_server, daemon=True).start()
            return {"ok": True, "message": "Launcher stopped. You can close this tab."}
        return None


def stop_server():
    time.sleep(0.2)
    if SERVER is not None:
        SERVER.shutdown()


def idle_watch():
    """Exit when the page has been closed for a long time and nothing runs."""
    while True:
        time.sleep(30)
        if JOB["running"] or time.monotonic() - LAST_REQUEST < IDLE_EXIT_SECONDS:
            continue
        try:
            if processes():
                continue
        except OSError:
            continue
        log("Stopping after being idle with no page open.")
        stop_server()
        return


def running_launcher():
    receipt = read_json(config().data / "launcher.json")
    address = receipt.get("url", "") if isinstance(receipt, dict) else ""
    try:
        parsed = urlparse(address)
        if parsed.scheme != "http" or parsed.hostname != "127.0.0.1" or not parsed.port or parsed.path not in ("", "/"):
            return None, None
        with urlopen(address.rstrip("/") + "/api/identity", timeout=1) as response:
            identity = json.load(response)
        if identity.get("app") == APP_ID and identity.get("pid") == receipt.get("pid"):
            return address, identity.get("root", "")
    except (OSError, ValueError, KeyError):
        pass
    return None, None


def show_error(message, title="WuWa VR Launcher"):
    log("ERROR " + redact(message))
    if os.environ.get("WUWA_VR_NO_DIALOG"):
        if sys.stderr:
            print(message, file=sys.stderr, flush=True)
        return
    try:
        ctypes.windll.user32.MessageBoxW(None, str(message), title, 0x10 | 0x40000)
    except Exception:
        pass


def self_check():
    """File, data-folder and helper checks. Reads only, except a test file in the data folder."""
    checks = []

    def check(name, ok, detail=""):
        checks.append({"name": name, "ok": bool(ok), "detail": detail})
    app = config().app
    info = portable_info()
    check("package description", info.get("schema") == 1, redact(app / "portable.json"))
    for name in ("dev/wuwa-builds.json", "dev/wuwa-player.html", "dev/wuwa-build.ps1", "dev/WuWaBuildProfiles.ps1",
                 "dev/start-wuwa-build.ps1", "dev/WuWaLaunchLifecycle.ps1", "dev/WuWaOpenXR.ps1", "dev/sim-run.ps1",
                 "dev/Get-SceneCaptureCheck.ps1", "dev/Get-RunContinuityCheck.ps1", "site/index.html", "site/guide.html"):
        check("file " + name, (app / name).is_file())
    for build in catalog():
        folder = runtime_folder(build)
        backend = folder / "UEVRBackend.dll"
        ok = backend.is_file() and hashlib.sha256(backend.read_bytes()).hexdigest() == str(build.get("sha256", "")).lower()
        check("build " + build["id"], ok, "backend matches catalog" if ok else "backend missing or changed")
        check("profile " + build["id"], (app / build["seed"] / "config.txt").is_file())
    try:
        config().data.mkdir(parents=True, exist_ok=True)
        probe = config().data / (".write-test-" + secrets.token_hex(4))
        probe.write_text("ok", encoding="utf-8")
        probe.unlink()
        check("data folder writable", True, redact(config().data))
    except OSError as error:
        check("data folder writable", False, redact(error))
    powershell_exe = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32/WindowsPowerShell/v1.0/powershell.exe"
    check("Windows PowerShell", powershell_exe.is_file())
    check("package path is plain ASCII", not has_non_ascii(config().package),
          "The injector has not been tested from folders with non-English characters.")
    required = [c for c in checks if c["name"] != "package path is plain ASCII"]
    return {"ok": all(c["ok"] for c in required), "checks": checks, "python": sys.version.split()[0],
            "executable": redact(sys.executable)}


def serve(args):
    global BASE_URL, SERVER
    config().data.mkdir(parents=True, exist_ok=True)
    with (config().data / "launcher.lock").open("a+b") as lock:
        lock.seek(0)
        try:
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError:
            for _ in range(20):
                address, root = running_launcher()
                if address:
                    same = root and Path(root).resolve() == config().app
                    if not same:
                        show_error("WuWa VR Launcher is already open from another folder:\n" + str(Path(root).parent)
                                   + "\n\nUse that window, or choose Stop launcher on its page before starting this copy.")
                    if not args.no_open:
                        webbrowser.open(address)
                    if sys.stdout:
                        print(address, flush=True)
                    return 0 if same else EXIT_REPORTED
                time.sleep(0.25)
            show_error("WuWa VR Launcher is already starting but not responding yet. Try again in a few seconds.")
            return EXIT_REPORTED
        try:
            with ThreadingHTTPServer(("127.0.0.1", args.port), Handler) as server:
                SERVER = server
                BASE_URL = "http://127.0.0.1:" + str(server.server_port)
                write_json(config().data / "launcher.json", {"url": BASE_URL, "pid": os.getpid(), "app": str(config().app),
                                                             "started": datetime.now().astimezone().isoformat(timespec="seconds")})
                log(f"Started {BASE_URL} from {redact(config().app)} (Python {sys.version.split()[0]})")
                if sys.stdout:
                    print(BASE_URL, flush=True)
                if not args.no_open:
                    threading.Timer(0.3, lambda: webbrowser.open(BASE_URL)).start()
                threading.Thread(target=idle_watch, daemon=True).start()
                server.serve_forever(poll_interval=0.25)
                log("Stopped.")
        finally:
            SERVER = None
            try:
                receipt = read_json(config().data / "launcher.json")
                if isinstance(receipt, dict) and receipt.get("pid") == os.getpid():
                    (config().data / "launcher.json").unlink()
            except OSError:
                pass
            lock.seek(0)
            msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
    return 0


def main(argv=None):
    global CONFIG
    parser = argparse.ArgumentParser(description="WuWa VR portable launcher")
    parser.add_argument("--no-open", action="store_true", help="do not open the browser page")
    parser.add_argument("--port", type=int, default=0, help="loopback port (0 chooses a free one)")
    parser.add_argument("--check", action="store_true", help="print a JSON self-check and exit")
    parser.add_argument("--verify", action="store_true", help="verify packaged files against manifest.json and exit")
    args = parser.parse_args(argv)
    try:
        if not 0 <= args.port <= 65535:
            raise ValueError("port must be between 0 and 65535")
        CONFIG = Config()
        if args.check:
            result = self_check()
            if sys.stdout:
                print(json.dumps(result, indent=2), flush=True)
            return 0 if result["ok"] else 1
        if args.verify:
            try:
                message = verify_package()
                ok = True
            except ValueError as error:
                message, ok = str(error), False
            if sys.stdout:
                print(json.dumps({"ok": ok, "message": message, **VERIFY_RESULT}, indent=2), flush=True)
            return 0 if ok else 1
        info = portable_info()
        if info.get("schema") != 1 or not catalog():
            raise RuntimeError("This folder is not a complete WuWa VR Launcher package. Extract the whole ZIP, then open "
                               "WuWa VR Launcher.exe from the extracted folder.")
        return serve(args)
    except Exception as error:
        show_error(f"WuWa VR Launcher could not start:\n{error}")
        return EXIT_REPORTED


if __name__ == "__main__":
    sys.exit(main())
