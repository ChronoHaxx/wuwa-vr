# Cinematic release acceptance — 4 October 2026

Release: `beta-2026-10-04-cinematic`. Launcher: 1.0.1.
Backend: `cinematic-20261004`. Exact identities are in `mod/checkpoint.json`.

The owner confirmed that the immersive framing repair works in their simulator
quest replay and requested making it the default and publishing this update.
The accepted private build was `screen-comfort-20261004` (backend SHA-256
`13dbc7df61cb74633e196759eba7f8c9f04d5894239bbc08cf3412e50d5372ef`).
The new native build changes only the default and presentation wording relative
to that candidate; it retains the same framing algorithm. Explicit saved OFF
preferences remain authoritative.

The latest local recording, `20261004-220346-570bc86d`, contains about 68 seconds
of combat and cinematic footage. Sampled cinematic shots have matching top and
bottom framing in both simulator eyes. The recording ended when the preview
stopped; its motion sidecar is incomplete and is not exact GPU-frame evidence.
No private recording, screenshot or UID is included in this public source.

Automatic switching was enabled but its status reported no detector samples.
The native log explains the failure: the optional detector module was unavailable.
UEVR temporarily adds the scripts folder to Lua's module path during top-level
execution, then restores it before callbacks. Resolving the inert helper during
top-level execution repairs the delayed `require` failure; actual detector
activation stays deferred until the option is enabled. An offline regression
reproduces that temporary module-path behavior.

## Verified for this release

- The owner accepted the tested simulator cinematic framing; headset comfort
  and coverage of other scenes remain pending.
- Native build passed with one low-priority worker and a 1.50 GiB peak child
  private-memory footprint. Source reconstruction and all 127 overlay files
  match the build inputs.
- Automatic-cinema Lua tests: 13 groups; HUD refresh: 18; menu HUD: 10.
  Existing manual mono/screen shortcut tests passed in the preceding candidate.
- Launcher component, controller, local SDK update and offscreen WPF checks
  passed. The exact compiled assembly installed and verified the release ZIP
  in an isolated store. No production app update or game launch was performed.
- Website checks passed: 14 release cases, 25 general cases and 7 metadata cases.
  Google ownership verification and the sitemap were retained.

## Still open

1. Long black loading between some dialogue/scenes: no captured cause yet.
2. Flat-menu backgrounds moving separately from the controls: manual mono is
   the workaround; selective automatic handling is not implemented.
3. Unsupported HUD aspect reset: the new read-only schema report still needs
   an affected runtime capture.
4. Automatic story/movie recognition: OFF by default and unverified after the
   startup repair. Rare prerecorded movie playback has not been tested.

The current framing repair is accepted for the reported simulator case, not
proof of complete cinematic support, full 360-degree scene content or physical
headset comfort. Prior public packages remain available for rollback.
