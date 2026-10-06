# Enum discovery startup repair, 6 October 2026

App **1.0.10**, package **beta-2026-10-06-enum-startup**, build
**enum-startup-20261006**, game **3.7**. Affected-PC and headset acceptance remain
unverified. Keep the 1.0.9 release and its original evidence for rollback.

## Remote evidence and limits

The owner reported that the simulator appeared briefly and the game then closed
by itself on Windows 11 with Steam. The 1.0.9 attempt reached 65 verified DX12
device callbacks through the repaired dispatch path, created OpenXR swapchains
and logged both eye projections. The stability check correctly failed when the
original game process disappeared. This is progress beyond the earlier flat-only
startup, not a successful end-to-end launch.

Engine metadata initialization last logged `FEnumProperty` finding the underlying
property at offset `0x80`. The watchdog later replaced the DX12 hook after the
frame stream stopped. The sampled log has no exception stack or crash dump:
neither the final SDK message nor the hook replacement establishes the exit cause.
AMD, a driver and the simulator have not been established as causes.

## Repairs

The SDK previously cast every pointer-sized candidate as `UEnum`, including the
underlying `FField` property, and called the engine's raw FName conversion using
that incompatible layout. The SDK itself warns that raw name conversion is unsafe
for brute-force candidates and that some crashes cannot be recovered by catch-all
exception handling. The candidate now verifies UObject registration before class
access and compares against the known Enum class. It does not hardcode `0x88` as
the answer. Unresolved offsets must not be used as offset-zero object reads.

A separate lifecycle defect exists in cached `ResizeBuffers` and `ResizeTarget`
callbacks: after a same-API replacement, the new hook has no selected swapchain
yet, but the old callbacks dereferenced its null hook. The own-process baseline
reproduced `0xC0000005` for both callbacks. Guarded forwarding protects replacement
and destruction without invoking the new renderer's resize handler for an old
chain. This defect is proven in the fixture; its involvement in the remote exit
is not proven.

Startup diagnostics also need a consistent failed outcome after a failed
stability check, current process observations during that check, and bounded
retention of metadata and renderer initialization milestones. Earlier saved
attempts remain unchanged.

## Validation

Fifteen enum cases passed against the exact production source/header with a
controlled SDK boundary: valid and shifted layouts, identity/index/class refusal,
unreadable pointers, missing metadata, reentry and unresolved getters. The old
source reached the unsafe UEnum name-conversion stub; the repaired UEnum scan
made no candidate UObject/name calls. This does not claim that unchanged numeric
FField discovery makes no guarded engine-name calls.

The resize baseline reproduced both access violations. Repaired cases passed for
callbacks queued before a replacement's first Present, a different selected
chain, active selected resizing and destruction. Existing dispatch regression
modes passed; the rendered mode completed 90 GPU readbacks and non-TEST Presents.
These hooks use real hidden DXGI objects with a Framework mutex stub, not the
game or full OpenXR renderer.

The reporting changes passed 96 PowerShell checks and 31 Python tests, including
the actual settle-script path with controlled process/log observations. Reads
are limited to 512 KiB; compact output is capped in Python characters, not UTF-8
bytes. The tests launch no game and alter no installed runtime or profile.

The backend build, 15 enum-discovery cases and renderer resize/dispatch regressions passed. Successful startup on the affected PC and physical-headset acceptance remain unverified.

Source reconstruction, launcher checks, isolated package installation, updater packaging and website checks passed. No affected-PC or headset acceptance is implied.

Private receipts are under `extracted/enum-startup-release-20261006`. Input
diagnostics and user paths are not release assets. The public source overlays and
both upstream patches must reconstruct the exact build inputs; `mod/checkpoint.json`
records the backend, UEVR patch, UESDK patch, launcher and package identities.

All 136 UEVR overlay files and 4 UESDK overlay files match the native build
inputs. Both source patches passed forward application and full-tree comparison
against their pinned bases. The full backend build finished successfully at
20:06:23 BST, with one low-priority compiler worker. The injector is unchanged.

| Artifact | SHA-256 |
| --- | --- |
| Backend | `26b32f2c6cc2d52c29fa786ca932806b53469cc3b0a3a2f32b8ed174fffbe3b6` |
| UEVR source patch | `e98c4f8e8e2b7526fecbbf754b4862526fb1af028c058ec405ac8b79456ace7b` |
| UESDK source patch | `3fde0cac206f631094d7931c346642ef470f8ca0e3b118b1047e24c8088e1bdc` |
| Portable ZIP | `1531c0fc9b07b350e2b5a85b97655070450b95d0b95e60cfedbc32adf5107599` |
| Launcher | `e6cb12ec0b46dafea278d18ea782d9f9c716c196a36ece305cb98e904861d31b` |
| Setup | `659b182b3cd16456ebc0e1d7e89b2ba7da9346fa454b4d5b3a2fac774ec29b1f` |

## Focused user acceptance

- [ ] Close WuWa, update the app to 1.0.10, then explicitly install
  **beta-2026-10-06-enum-startup** in step 02. An app update retains the old package
  selection.
- [ ] Keep the same runtime and graphics settings. Launch once; verify that VR
  remains running after the 30-second stability check, rather than accepting a
  brief simulator window as success.
- [ ] If stable, check the UEVR menu and normal exit. If it exits again, copy
  diagnostics before another attempt. Headset comfort remains a separate check.

## User result — 6 October 2026

The user reported "finally this works" for 1.0.10 on the previously affected
Windows 11 Steam PC using the bundled simulator. Startup is now user-confirmed
for that setup. Menu/exit details, PS controller input and physical-headset
comfort were not separately reported; the checklist above is retained as written.
