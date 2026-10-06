"""Read-only Steam installation identity. No protocol dispatch or process changes."""
from pathlib import Path
import os
import re
import winreg

STEAM_APP_ID = "3513350"
MAX_METADATA = 1024 * 1024


def metadata(path):
    path = Path(path)
    try:
        if path.stat().st_size <= MAX_METADATA:
            return path.read_text(encoding="utf-8-sig")
    except (OSError, UnicodeError):
        pass
    return ""


def vdf_value(text, key):
    values = re.findall(r'"' + re.escape(key) + r'"\s*"([^"\r\n]*)"', text, re.I)
    return values[0].replace("\\\\", "\\") if len(values) == 1 else None


def validate_steam(path):
    if not isinstance(path, str) or not re.match(r"^[A-Za-z]:[\\/]", path.strip().strip('"')):
        raise ValueError("Choose the full local path to Steam's Wuthering Waves.exe.")
    path = path.strip().strip('"')
    if any(c in path for c in '\r\n"'):
        raise ValueError("The Steam game path is invalid.")
    bootstrap = Path(os.path.abspath(path))
    install, common, apps = bootstrap.parent, bootstrap.parent.parent, bootstrap.parent.parent.parent
    shipping = install / "Client/Binaries/Win64/Client-Win64-Shipping.exe"
    manifest = metadata(apps / f"appmanifest_{STEAM_APP_ID}.acf")
    if (bootstrap.name.lower() != "wuthering waves.exe" or common.name.lower() != "common"
            or apps.name.lower() != "steamapps" or not bootstrap.is_file() or not shipping.is_file()
            or vdf_value(manifest, "appid") != STEAM_APP_ID
            or (vdf_value(manifest, "installdir") or "").lower() != install.name.lower()):
        raise ValueError("Select Steam's installed Wuthering Waves.exe (app 3513350); its manifest and game files must match.")
    return str(bootstrap)


def steam_roots():
    roots = [str(Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Steam")]
    for hive, key, name in ((winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam", "SteamPath"),
                            (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Valve\Steam", "InstallPath")):
        try:
            with winreg.OpenKey(hive, key) as handle:
                roots.append(str(winreg.QueryValueEx(handle, name)[0]))
        except OSError:
            pass
    return roots


def steam_candidates(roots=None):
    libraries = {}
    def add(path):
        if len(libraries) < 64 and isinstance(path, str) and re.match(r"^[A-Za-z]:[\\/]", path):
            full = os.path.abspath(path)
            libraries[full.lower()] = Path(full)
    for root in (steam_roots() if roots is None else roots):
        add(root)
    for library in list(libraries.values()):
        text = metadata(library / "steamapps/libraryfolders.vdf")
        for value in re.findall(r'"(?:path|[0-9]+)"\s*"([^"\r\n]*)"', text, re.I):
            add(value.replace("\\\\", "\\"))
    found = []
    for library in libraries.values():
        install = vdf_value(metadata(library / f"steamapps/appmanifest_{STEAM_APP_ID}.acf"), "installdir")
        if not install or any(c in install for c in "/\\") or install in (".", ".."):
            continue
        try:
            found.append(validate_steam(str(library / "steamapps/common" / install / "Wuthering Waves.exe")))
        except ValueError:
            pass
    return found
