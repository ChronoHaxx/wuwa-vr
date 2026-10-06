# Steam launch preview — 6 October 2026

Private package `candidate-20261006-steam`, package build `steam-20261006`.
Delivery: `E:\Coding\wuwa-vr\extracted\steam-launcher-20261006\Ready\WuWa VR.exe`.
The renderer remains the compiled `graphics-20261006-r2` DLL; no graphics defaults,
renderer algorithms, active profile or runtime registration were changed by this work.

## Player flow

Install this private package in step 02. Under step 01, **Change → Steam ·
Wuthering Waves → Use this installation** saves the selected Steam copy. Browse
also accepts its `Wuthering Waves.exe` when the matching app 3513350 manifest and
`Client/Binaries/Win64/Client-Win64-Shipping.exe` exist. Kuro's `launcher.exe`
remains a separate option. Saved official, Steam and manual choices are retained;
multiple detected installs require a choice.

Launch dispatches the fixed `steam://rungameid/3513350` URI through the existing
unelevated Windows desktop shell. The elevated worker never directly starts
Steam or the bootstrap as a fallback. It gives a recovery error if that shell
cannot be verified. Steam updates/sign-in can still delay or prevent startup.

The new injector's `--target-path` request requires the selected shipping path.
It checks path and creation identity again on the opened process handle before
loading a DLL. Unknown paths, ambiguous matches and changed process identity fail
closed. The old no-argument/name-only route is preserved. Old injectors are rejected
using a passive version-resource capability marker, without executing them.

## Verification and limits

- Compiled native discovery found the user's real Steam install and the separate
  Kuro install. The packaged Python helper found the same Steam copy and persisted
  a choice only in fixture settings.
- Native launcher: 91 core tests, 10 controller groups, 17 updater groups and 29
  offscreen WPF checks passed. Steam picker/disconnected persistence/reconnect
  and switching back to Kuro were tested with inert files and HTTP responses.
- Six Python Steam groups and PS5 Steam fixtures passed. The actual developer
  page was exercised headlessly with inert HTTP actions. Injector: 36 compiled
  argument/path/identity checks and existing graphics-preservation tests passed.
- The actual compiled installer installed and reopened the exact archive in an
  isolated store; bundled Python import/self-check and manifest verification passed.
- The native builder used one low-priority worker with a memory guard. NuGet's
  vulnerability metadata endpoint was unavailable; cached restore/build passed.
- Codex's background session has no interactive desktop shell (`GetShellWindow=0`).
  That read-only check failed closed before any URI was sent. Actual desktop COM
  dispatch, Steam injection and headset gameplay remain **unverified**. Earlier
  Steam injection attempts failed; this preview does not erase that history.

No game launch, injection, visible app takeover, production installation, runtime
change or publication was performed. Prior graphics-r2 rollback is preserved.
The existing renderer/loading caveats remain; this does not establish a repair
for Indath's initialization crash.

## Exact artifacts

- Launcher SHA-256: `99cead40a716e25420d59f8b36c2baf06a675fab235653e9ca0d57c9f40a9485`
- Injector SHA-256: `244104e019a1b6dd5bc63775a6407e56cde61fbb781130643941adc108170384`
- Renderer SHA-256: `c3e19ef50f1b5e9a28b2d09e61743381fd3ba38287f5fafd30ee0486e5eb2c2c`
- Portable payload SHA-256: `413031612c4ec0d2add7599133658527e1bc84a51c070727df0613b48a229e8d`
- Complete ZIP SHA-256: `978b53a19de31f1e5c873857c054af18b5cbe4f4c2aa53ed88d2fc4c7d5e5940`

Build, staging, packaged-helper and delivery receipts are in the artifact folder.
Installer receipt: `launcher/native/work/verify-install-6eed1af18b00428ea20af6f00ffa0d20/install-test-receipt.json`.
Use `Ready/TEST THIS.txt` for the three-step setup and short live acceptance check.

- [ ] Steam reaches actual VR gameplay on the user's PC.
- [ ] Xbox input, menus and both-eye rendering remain usable; normal exit works.
- [ ] Reopening retains Steam; switching back to Kuro remains available.

## First owner launch and onboarding review

On 6 October, the owner reported that the private Steam candidate appears to
launch successfully and is running. Read-only evidence matches the selected
`steam-20261006` launch at 11:36:48 BST: UE backend entry at 11:36:53 and fresh
diagnostic status for game PID 85316, with stereo rendering log activity after
11:40. This establishes live startup/backend activity, not headset comfort,
correct both-eye appearance, controls or normal exit. Those checks remain open.

Fresh installs already support **01 choose game → 02 install VR → launch**.
The supplied step-02-first instruction was an upgrade workaround: an older
installed helper rejects Steam mode, while the current native UI immediately
tries to apply it and defaults its release picker to that older installed package.
The pending game choice is retained and does not prevent installation. Before
public Steam rollout, defer unsupported Steam settings across that upgrade and
show an actionable package-update state instead of a connection failure. Cover
old helper → choose Steam → install new package → apply the pending choice once;
do not auto-launch as part of installation.

The live progress file also remained at "waiting for UEVR" despite fresh backend
activity. Diagnose exact-path observation/exit tracking before claiming the
launcher progress is reliable for this Steam run. Do not restart/inject again
to investigate without the user's authorization.
