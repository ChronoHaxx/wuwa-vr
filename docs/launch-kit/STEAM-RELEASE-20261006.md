# Steam and graphics settings beta — 6 October 2026

Release `beta-2026-10-06-steam`, desktop launcher **1.0.2**, VR package build
`steam-20261006`, game **3.7**. The owner explicitly requested publication.

The renderer and injector are byte-identical to the private Steam package the
owner launched. Renderer `graphics-20261006-r2` SHA-256:
`c3e19ef50f1b5e9a28b2d09e61743381fd3ba38287f5fafd30ee0486e5eb2c2c`.
Injector SHA-256:
`244104e019a1b6dd5bc63775a6407e56cde61fbb781130643941adc108170384`.
`mod/checkpoint.json` and the release checksums identify the public payload.

## Player changes

- Choose the detected Kuro or Steam installation. Steam uses its fixed game URI
  through the normal desktop shell, then targets the selected shipping executable.
  Exact process identity is checked again before injection; no name-only fallback
  is permitted for an explicit Steam target.
- Choosing Steam with an older installed helper retains the choice, offers the
  new package in step 02, and keeps the old helper connected for installation.
  The new helper applies the pending choice once. Installation never launches.
- Game graphics preferences are preserved. Recognized inherited startup presets
  and proven r1-generated overrides are backed up and removed; custom overrides
  are preserved. The injector no longer regenerates a default level-2 preset for
  profiles managed by the new policy.
- Existing player menu grouping, incident tools and optional walking input are
  included. The accepted rendering/cinematic corrections and manual modes remain.

Installed launchers check for app updates at startup. App update/restart and VR
package installation remain separate, explicit operations. Updating to 1.0.2 does
not silently change the selected VR package. Portable EXEs do not self-update.
Existing public releases stay available for rollback.

## Evidence and limits

The owner reported the Steam game running on 6 October; fresh UE backend entry,
diagnostic identity and stereo rendering activity confirmed startup on that PC.
Earlier graphics-r2 startup retained the cleaned graphics files. Neither result
is a physical-headset or other-PC acceptance result.

Before this release, the private Steam package passed 91 native core cases,
10 controller groups, 17 updater groups, 29 offscreen WPF cases, six Python Steam
groups, PowerShell Steam fixtures, 36 compiled injector path/identity cases,
graphics/profile regressions, actual isolated package installation and a bundled
Python self-check. Public packaging reruns affected native and installer checks.

A read-only source audit matched all 128 renderer overlays to the reconstructed
graphics-r2 source, the frozen patch to its receipt, all 12 injector overlays to
the injector build receipt, and all seven recorded helper hashes to source.
The release-specific evidence is retained locally under
`extracted/steam-public-20261006`; it contains no production install or game launch.

Final 1.0.2 validation passed: 91 native core cases, 10 controller groups,
17 updater groups and all offscreen window scenarios, including both fresh
Steam setup and the old-helper upgrade handoff. The harness logged 34 window
PASS lines; its historical footer still prints 29. The exact compiled launcher
installed the public archive into an isolated store and retained its selection.
The bundled Python self-check, 842-file manifest verification, Steam discovery
and fixture-only settings roundtrip passed. Installer/update packaging passed.
The publishing helper's 26 tests and the website's 14 release / 25 general checks
passed; ownership verification and sitemap bytes were unchanged. NuGet's
vulnerability metadata endpoint was unavailable; cached dependency restore and
compilation completed. Three existing unused test-hook field warnings remain.

Remaining issues include a launch progress label that can lag actual backend
startup, some scene/dialogue stalls, unavailable HUD refresh, moving backgrounds
behind flat menus and some stereo effects. Automatic cinema remains experimental,
off by default and unverified for actual prerendered movies. This does not claim
to fix Indath's initialization crash. Optical hands, optional walking controls,
headset comfort, other-PC installation and full Steam input/exit still need testing.

## Optional grouped player check

- [ ] On a second PC, choose the intended Steam/Kuro copy, install this exact
  package, launch, and record actual gameplay separately from the progress label.
- [ ] Check Xbox input, menus, both-eye near/far appearance and frame rate with
  the game's graphics settings.
- [ ] Check cinematic framing and manual mono/stereo fallback, including return
  to gameplay, without assuming automatic cinema detection is working.
- [ ] Exit normally and reopen; confirm the selected installation and package
  persist. Report the package, storefront, runtime, scene and exact error.

Publication is authorized; unchecked device acceptance is retained honestly.
