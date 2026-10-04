# Launcher 1.0.0 beta verification

Release: `beta-2026-10-04-launcher`. Mod: `npc-rim-20261004` for WuWa 3.7.
This pairs the compact Windows launcher, its update package, the complete VR
payload and reconstructible native source. `mod/checkpoint.json` records the
backend, installer and portable identities; the release has SHA256SUMS.txt.

The new launcher uses Velopack 1.2.161, explicitly downloads and restarts for app
updates, and leaves mod builds, settings and recordings in their existing separate
LocalAppData folders. App updates require a fresh idle check, including when no mod
is selected. A fixed website feed points to immutable GitHub package assets;
frequent mod releases cannot hide app updates. Portable developer tools remain
available separately. The Setup EXE is unsigned.

Completed background verification on 4 October:

- 81 manager component checks and 10 controller-diagnostic groups.
- 17 updater groups, including real SDK discovery, absolute asset URLs, checksum
  rejection, interrupted downloads, offline checks and idle/restart safeguards.
- 29 offscreen WPF checks exercising actual button events, retries, bilingual
  errors, selection, recording states and update controls.
- Exact final VR ZIP installed into isolated storage, verified against its
  manifest, and connected through the real packaged helper.
- Actual helper/WPF end-to-end checks: conflicting helper recovery, readiness,
  reconnect, playtest persistence, interrupted-helper retry, package repair and
  cleanup. No game launch, injection or runtime/profile change was performed.
- A separate disposable app using the same SDK completed silent installation,
  1.0.0-to-1.0.1 download, explicit apply/restart with a new process and version,
  and official uninstall. An external settings marker survived every step.
  This tests the installer/update mechanism; it is not production shortcut or
  fresh-PC acceptance. An initial restricted run failed at registry registration;
  its own official updater cleaned it before the successful permitted run.
- Full website browser checks 25/25, focused release checks 14/14, community
  checks 15/15 and bilingual offline-guide layout checks 3/3.
- All 122 native overlays match the recorded backend reconstruction. Two existing
  trailing spaces in the compiled D3D12 source are retained to preserve identity.

The optional NPC rim suppression starts off. Two stationary simulator A/B/A
comparisons reproduced removal of the extra right-eye rim; enabling it also
removes intended character rim lighting. It is a workaround, not a stereo shader
repair, and new-toggle headset acceptance remains pending. Earlier accepted
ultimate-camera/foliage changes and owner-confirmed 2D brightness remain included.
Weapon/Echo reflections and some cinematic one-eye effects remain open.

Production clean-PC installation, shortcuts and live headset/game acceptance are
not claimed by these background results. Test those through the grouped player
checklist shipped with the launcher before treating the beta as stable.
