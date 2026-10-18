"""Run a WuWa VR test session on the OpenXR simulator with nobody at the PC.

    python dev/sim-session.py status
    python dev/sim-session.py start [--copy kuro|steam] [--title-only]
    python dev/sim-session.py press A [--times 2]
    python dev/sim-session.py quit [--keep-runtime]
    python dev/sim-session.py runtime simulator|headset

start switches the machine-wide OpenXR runtime to the simulator, starts the applied build's
injector and the game (no official launcher, so no Play click), waits for the backend and the
uevr-mcp plugin, then presses A on the title screen until the player is in the world. quit asks
the game to exit (console "quit") and puts back the runtime start replaced.

The elevated steps go through the "WuWa VR dev helper" scheduled task; install it once with
dev/sim-helper/install.ps1 (one UAC prompt). Input reaches the game through the uevr-mcp plugin
in the profile, synced here from upstream/uevr-mcp before each start; nothing takes focus.
The build that runs is the one applied in the launcher (injector_config.txt).
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request
import uuid
import winreg
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
WORKSPACE = REPO.parent
TASK = "WuWa VR dev helper"
HELPER = Path(os.environ.get("ProgramData", r"C:\ProgramData")) / "WuWaVR-DevHelper"
# Requests and session state live in the workspace, not AppData: the Claude desktop app is an
# MSIX package, and Windows diverts new AppData folders its shells create into a private copy
# that the elevated task cannot see (17 Oct).
REQUESTS = WORKSPACE / "data" / "dev-helper"
SESSION = REQUESTS / "session.json"
PROFILE = Path(os.environ["APPDATA"]) / "UnrealVRMod" / "Client-Win64-Shipping"
LOG = PROFILE / "log.txt"
PLUGIN = WORKSPACE / "upstream/uevr-mcp/plugin/build/Release/uevr_mcp.dll"
API = "http://127.0.0.1:8899"
GAME, INJECTOR = "Client-Win64-Shipping.exe", "Custom_UEVR_Injector.exe"


def say(text):
    print(time.strftime("%H:%M:%S"), text, flush=True)


def helper_config():
    path = HELPER / "config.json"
    if not path.is_file():
        sys.exit(f"The dev helper is not installed. Run once (one UAC prompt):\n"
                 f"  powershell -ExecutionPolicy Bypass -File {REPO / 'dev/sim-helper/install.ps1'}")
    return json.loads(path.read_text(encoding="utf-8-sig"))


def running():
    out = subprocess.run(["tasklist", "/fo", "csv", "/nh"], capture_output=True, text=True).stdout.lower()
    return {name for name in (GAME, INJECTOR, "Wuthering Waves.exe") if f'"{name.lower()}"' in out}


def active_runtime():
    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Khronos\OpenXR\1") as key:
            return winreg.QueryValueEx(key, "ActiveRuntime")[0]
    except OSError:
        return ""


def runtime_mode(path, config):
    for mode in ("simulator", "headset"):
        if path and Path(path).resolve() == Path(config[mode]).resolve():
            return mode
    return ""


def helper(action, timeout=90, **fields):
    """One request to the elevated task; returns its result or exits with its error."""
    if Path(helper_config()["requests"]).resolve() != REQUESTS.resolve():
        sys.exit(f"The installed helper reads requests from {helper_config()['requests']}; reinstall it "
                 f"(dev/sim-helper/install.ps1) so it reads {REQUESTS}.")
    REQUESTS.mkdir(parents=True, exist_ok=True)
    request_id = uuid.uuid4().hex
    (REQUESTS / "request.json").write_text(json.dumps({"id": request_id, "action": action, **fields}), encoding="utf-8")
    run = subprocess.run(["schtasks", "/run", "/tn", TASK], capture_output=True, text=True)
    if run.returncode:
        sys.exit(f"Could not start the dev helper task: {run.stdout.strip()} {run.stderr.strip()}")
    result_path = HELPER / "results" / f"result-{request_id}.json"
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if result_path.is_file():
            try:
                result = json.loads(result_path.read_text(encoding="utf-8-sig"))
            except ValueError:
                time.sleep(0.3)
                continue
            if not result.get("ok"):
                sys.exit(f"Dev helper {action} failed: {result.get('error')}")
            return result
        time.sleep(0.5)
    sys.exit(f"The dev helper did not answer within {timeout} s; see {HELPER / 'helper.log'}")


def api(path, body=None, timeout=10):
    data = None if body is None else json.dumps(body).encode()
    request = urllib.request.Request(API + path, data=data, method="GET" if body is None else "POST",
                                     headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=timeout) as reply:
        return json.loads(reply.read() or b"{}")


def api_up():
    try:
        api("/api/status", timeout=3)
        return True
    except (OSError, ValueError):
        return False


def log_lines():
    try:
        return LOG.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return []


def wait(label, check, timeout, interval=2.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = check()
        if value:
            return value
        time.sleep(interval)
    sys.exit(f"Timed out after {timeout} s waiting for {label}.")


def press(button, times=1, gap=0.6):
    for i in range(times):
        api("/api/input/gamepad", {"buttons": {button.lower(): True}, "duration": 0.15})
        time.sleep(0.15 + gap)


def player():
    try:
        reply = api("/api/player", timeout=5)
    except (OSError, ValueError):
        return None, None
    pawn = reply.get("pawn") or {}
    controller = reply.get("controller") or {}
    return controller.get("class"), pawn.get("class")


def sync_plugin():
    target = PROFILE / "plugins" / "uevr_mcp.dll"
    if not PLUGIN.is_file():
        say(f"uevr-mcp build missing ({PLUGIN}); keeping the profile's copy")
        return
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    if target.is_file() and digest(target) == digest(PLUGIN):
        return
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(PLUGIN, target)
    say("Copied the current uevr-mcp plugin into the profile")


def save_session(values):
    REQUESTS.mkdir(parents=True, exist_ok=True)
    SESSION.write_text(json.dumps(values, indent=1), encoding="utf-8")


def load_session():
    try:
        return json.loads(SESSION.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def cmd_status(args):
    config = helper_config() if (HELPER / "config.json").is_file() else None
    active = active_runtime()
    print("Dev helper task :", "installed" if config else "not installed")
    print("OpenXR runtime  :", active, f"({runtime_mode(active, config)})" if config else "")
    print("Running         :", ", ".join(sorted(running())) or "nothing")
    print("uevr-mcp API    :", "up" if api_up() else "down")
    if api_up():
        print("Player          :", player())
    name = next((line for line in log_lines() if "OpenXR system Name" in line), "")
    print("Log runtime     :", name.split("]")[-1].strip() if name else "-")


def cmd_runtime(args):
    result = helper("runtime", mode=args.mode)
    say(f"OpenXR runtime: {result['active']} (was {result['before']})")


def cmd_start(args):
    config = helper_config()
    if running():
        sys.exit(f"Already running: {', '.join(sorted(running()))}. Use status, or quit first.")
    sync_plugin()
    before = active_runtime()
    if runtime_mode(before, config) != "simulator":
        helper("runtime", mode="simulator")
        save_session({"restore": runtime_mode(before, config), "restorePath": before})
        say("OpenXR runtime switched to the simulator")
    elif not load_session().get("restore"):
        save_session({"restore": "", "restorePath": before})
    header = (log_lines() or [""])[0]
    started = time.monotonic()
    launch = helper("launch", copy=args.copy)
    say(f"Started the {args.copy} copy: injector {launch['injectorPid']}, game {launch['gamePid']}, "
        f"backend {Path(launch['runtimeFolder']).name}")
    wait("the backend log of this launch", lambda: (log_lines() or [""])[0] != header, 240)
    line = wait("the OpenXR system line", lambda: next((l for l in log_lines() if "OpenXR system Name" in l), ""), 240)
    say("Log: " + line.split("]")[-1].strip())
    wait("the uevr-mcp plugin API", api_up, 240)
    say(f"uevr-mcp API up after {time.monotonic() - started:.0f} s")
    if args.title_only:
        return
    seen = None
    deadline = time.monotonic() + args.world_timeout
    while time.monotonic() < deadline:
        controller, pawn = player()
        if (controller, pawn) != seen:
            say(f"Player controller {controller}, pawn {pawn}")
            seen = (controller, pawn)
        if pawn and controller and "login" not in (pawn + controller).lower():
            say(f"In the world after {time.monotonic() - started:.0f} s")
            return
        press("a", times=2)
        time.sleep(4)
    sys.exit("Not in the world yet; check a screenshot (python dev/sim-session.py status).")


def cmd_press(args):
    press(args.button, args.times)
    say(f"Pressed {args.button} x{args.times}")


def cmd_quit(args):
    if GAME in running():
        if not api_up():
            sys.exit("The game runs but the uevr-mcp API is down; quit it from the game menu.")
        api("/api/console/command", {"command": "quit"})
        say("Asked the game to quit")
        wait("the game to exit", lambda: not ({GAME, INJECTOR} & running()), 120)
        say("Game closed")
    session = load_session()
    if args.keep_runtime or not session.get("restore"):
        return
    helper("runtime", mode=session["restore"])
    save_session({})
    say(f"OpenXR runtime restored ({session['restore']})")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("status").set_defaults(run=cmd_status)
    runtime = sub.add_parser("runtime")
    runtime.add_argument("mode", choices=["simulator", "headset"])
    runtime.set_defaults(run=cmd_runtime)
    start = sub.add_parser("start")
    start.add_argument("--copy", choices=["kuro", "steam"], default="kuro")
    start.add_argument("--title-only", action="store_true", help="stop at the title screen")
    start.add_argument("--world-timeout", type=int, default=240)
    start.set_defaults(run=cmd_start)
    press_parser = sub.add_parser("press")
    press_parser.add_argument("button")
    press_parser.add_argument("--times", type=int, default=1)
    press_parser.set_defaults(run=cmd_press)
    quit_parser = sub.add_parser("quit")
    quit_parser.add_argument("--keep-runtime", action="store_true")
    quit_parser.set_defaults(run=cmd_quit)
    args = parser.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
