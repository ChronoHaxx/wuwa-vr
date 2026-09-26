# Easier Windows packaging

**Status, 25 September 2026:** a private **portable launcher package** exists as
a local prototype: a ZIP containing a folder with `WuWa VR Launcher.exe`. Players
extract it and double-click the EXE; no Python installation, terminal or GitHub
account is needed. It is not public, and its real UAC/game launch has not yet
been exercised by a person.

## What the package does

- `WuWa VR Launcher.exe` is a small native program (source included under
  `app/dev/portable`). It starts a private copy of CPython in the `python`
  folder, which runs the launcher script. It never asks for administrator
  rights itself, uses no network and ignores any Python installed on the PC.
- The script serves one page on `127.0.0.1` and opens it in the browser. Only
  that page can send it commands. Opening the EXE again reuses the running page.
  It stops itself after about 20 idle minutes with no page and no game open.
- Read-only files stay in the package folder. Settings, backups and logs live
  in `%LOCALAPPDATA%\WuWa VR Launcher`, so a moved or re-extracted package keeps
  its backups.
- It finds the official launcher, Steam or Epic install from Windows' own
  records, or accepts a chosen `launcher.exe`. Detection does not verify
  injection: Steam previously failed and has not been retested; Epic is
  untested. The new portable wrapper also needs an official-launcher game test.
  For experimental Steam/Epic startup, the player
  presses Play there.
- Apply, launch, reset, restore and integrity checks reuse the tested
  PowerShell build/launch helpers. First use on a PC without a UEVR profile
  creates one; **Restore my settings from before WuWa VR** undoes that.
- Game injection still needs Windows permission, because the game runs as
  administrator. The launcher switches the global OpenXR runtime only through
  the explicit **Use headset / Use simulator** buttons, with Windows permission
  and the game/injector closed. It remembers the previous headset runtime and
  never closes the game.

## Why not PyInstaller for this prototype

[PyInstaller](https://pyinstaller.org/en/stable/operating-mode.html) was the
first recommendation. It is not installed on the build PC, and adding it means
downloading third-party packages, which was not approved for this pass. Its
bootloaders are also a frequent source of antivirus false positives, which
would add confusion to a mod that already carries anti-cheat risk. The
prototype therefore copies an already-installed CPython (standard library
only, precompiled) and adds a transparent C stub. The player-facing result is
the same: extract, then open one EXE. PyInstaller one-folder remains an option
if the owner approves installing it.

## Windows prompts

SmartScreen evaluates publisher and file reputation. The new unsigned EXE may
show "Windows protected your PC"; signed new releases can also be
unrecognised. Microsoft says EV certificates no longer provide an immediate
bypass. Signing helps identify the publisher but cannot guarantee
warning-free startup. See
[Microsoft's guidance](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation).
Do not tell players to disable SmartScreen, antivirus or anti-cheat. No paid
signing service or certificate has been purchased.

## Before calling a bundle ready

Done in the prototype, with automated checks: separate read-only and writable
files; game-location choice; no Python child processes; required-file and
integrity checks; repeat launch; paths with spaces and non-English
characters; missing-file errors; backup, restore and removal steps; exact
versions, manifest and notices.

Still open:

- A person must run **Apply & launch** through the real Windows prompt and
  game, including cancelling the prompt once.
- Test on another Windows account/PC without Python or the development tools.
- Injection from a folder whose path has non-English characters is untested;
  the launcher warns about it.
- Include only files whose distribution is permitted. The UEVR backend's
  notice is "All rights reserved", so a public combined package needs the
  owners' permission first. An EXE does not change UEVR/community licensing.
- Show the [account-risk notice](RISK.md) before any future public download.

An MSI/MSIX installer can follow if it improves updates or trust. There is
currently no public binary or independently tested fresh-PC installation.
