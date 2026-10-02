"""Make the manual-install ZIP (no launcher) from a released one-build launcher ZIP.

Same files the launcher installs: the build's runtime folder (UEVR backend, LuaVR, OpenXR/OpenVR
loaders, injectors) and its profile seed, plus the notices. Scripts and plugins that UEVR would not
load (*.disabled backups) are left out. Writes WuWa-VR-Manual.zip next to --out.

    python dev/package-manual.py --launcher-zip WuWa-VR-Launcher.zip --out <dir>
"""
from __future__ import annotations

import argparse
import hashlib
import json
import zipfile
from pathlib import PurePosixPath

KEEP_TOP = ("LICENSE.md", "CREDITS.md", "RISK.md", "SUPPORT.md", "LICENSES/", "notices/")
RUNTIME_SKIP = {"Custom_UEVR_Injector.txt", "registration.json"}

GUIDE = """WuWa VR beta, manual install (no launcher)
Build: {build}

Use this if you prefer UEVR's usual injector to the WuWa VR Launcher. It holds the same
files the launcher installs, so the game behaves the same.

1. Back up your UEVR profile for the game, if you have one: rename
     %APPDATA%\\UnrealVRMod\\Client-Win64-Shipping
   to something like Client-Win64-Shipping.backup
2. Copy everything inside the "profile" folder into
     %APPDATA%\\UnrealVRMod\\Client-Win64-Shipping   (create it if needed)
   Other UEVR plugins or scripts left in that folder can conflict with this build.
3. Start your headset software (OpenXR or SteamVR), then Wuthering Waves, and wait for
   the title screen.
4. Run UEVRInjector.exe from the "UEVR" folder, choose Client-Win64-Shipping, OpenXR (or
   OpenVR), then Inject. The injector loads the DLLs next to it: keep the UEVR folder
   together and do not mix in DLLs from another UEVR version.
   Custom_UEVR_Injector.exe also works if its UEVR folder setting points at this folder.
5. In game: L3 + R3 (both stick clicks) or Insert opens UEVR. "WuWa controls" holds the
   shortcuts, camera, HUD and fixes. Native Stereo Fix with Same Pass is on in this
   profile; keep it on.

Undo: delete the Client-Win64-Shipping folder and rename your backup back.
Check the files: SHA256SUMS.txt lists every file's SHA-256.
Unofficial fan mod. Anti-cheat may restrict or ban accounts. Read RISK.md first.
Guide and help: https://chronohaxx.github.io/wuwa-vr/
"""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--launcher-zip", required=True)
    ap.add_argument("--out", required=True, help="output folder")
    args = ap.parse_args()
    src = zipfile.ZipFile(args.launcher_zip)
    top = src.namelist()[0].split("/")[0] + "/"
    manifest = json.loads(src.read(top + "manifest.json").decode("utf-8-sig"))
    catalog = json.loads(src.read(top + "app/dev/wuwa-builds.json").decode("utf-8-sig"))
    build = next(b for b in catalog["builds"] if b["id"] == manifest["defaultBuild"])
    runtime = f"{top}app/{build['runtime']}/"
    seed = f"{top}app/{build['seed']}/"
    folder = top.rstrip("/").replace("WuWa VR Launcher", "WuWa VR").replace("WuWa VR ", "WuWa VR manual ", 1) + "/"
    files: dict[str, bytes] = {}
    for info in src.infolist():
        name = info.filename
        if info.is_dir():
            continue
        rel = name[len(top):]
        if name.startswith(runtime):
            if PurePosixPath(name).name not in RUNTIME_SKIP:
                files["UEVR/" + name[len(runtime):]] = src.read(info)
        elif name.startswith(seed):
            part = name[len(seed):]
            if part.startswith("scripts/") and not part.endswith(".lua"):
                continue
            if part.startswith("plugins/") and not part.endswith(".dll"):
                continue
            files["profile/" + part] = src.read(info)
        elif rel.startswith(KEEP_TOP):
            files[rel] = src.read(info)
    if hashlib.sha256(files["UEVR/UEVRBackend.dll"]).hexdigest() != build["sha256"].lower():
        raise SystemExit("backend does not match the catalog hash")
    files["MANUAL-INSTALL.txt"] = GUIDE.format(build=build["name"]).replace("\n", "\r\n").encode("utf-8")
    sums = "".join(f"{hashlib.sha256(data).hexdigest()}  {name}\n" for name, data in sorted(files.items()))
    files["SHA256SUMS.txt"] = sums.encode("utf-8")
    out = f"{args.out}/WuWa-VR-Manual.zip"
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name, data in sorted(files.items()):
            z.writestr(folder + name, data)
    digest = hashlib.sha256(open(out, "rb").read()).hexdigest()
    print(json.dumps({"zip": out, "folder": folder.rstrip("/"), "files": len(files), "build": build["id"],
                      "bytes": len(open(out, "rb").read()), "sha256": digest}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
