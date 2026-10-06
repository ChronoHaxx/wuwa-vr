# Device-verified dispatch candidate, 6 October 2026

App **1.0.9**, package **beta-2026-10-06-device-dispatch**, build
**device-dispatch-20261006**, for game **3.7**. This candidate passed the background
checks below. Successful startup on the affected PC and headset acceptance remain
unverified. Publication is checked separately against the release assets and feed.

## What the latest report establishes

The **1.0.8 affected-PC retest failed**. The selected Steam process and the loaded
DLLs were verified. The active DX11 callback positively identified a DX12 device
and the same swapchain interface. Once probes switched, the DX12 probe recorded
zero callbacks and zero bridge calls. Therefore, the 1.0.8 retired-callback
handoff was never reached on that PC. Successful isolated retained-callback tests
from 1.0.8 do not establish that the remote dispatch mechanism was reproduced.

The report does not establish AMD, a driver, Windows 11, or another program as
the cause. The same swapchain slot was observed; the reason that one callback
entry receives frames and the other does not remains unconfirmed.

## Candidate behavior

Retire the DX11 probe normally, including its resize hook. After positive DX12
identification, the DX12 hook can own the known working callback entry instead
of depending on calls reaching its ordinary entry. The retired entry forwards
through the existing guarded DX12 path. The same swapchain, device and direct
command queue must be verified; rejection preserves original presentation.

DX12 retains its own swapchain, device and original callable; it does not depend
on keeping the old DX11 object alive. The retired DX11 Present hook is disarmed
before DX12 installs the same entry. A failed DX11 query alone is not proof of
DX12. Unsupported queue layouts retain the ordinary DX12 route. A matched source
whose queue validation fails forwards its own original callable.

The candidate retains startup evidence, exact Steam target checks, cancellation,
confirmed helper recovery, uninstall safeguards, graphics choices and accepted
view fixes. No user diagnostics are release assets.

## Evidence boundaries

The backend build and focused callback-dispatch tests passed. Successful startup on the affected PC and physical-headset acceptance remain unverified.

Source reconstruction, launcher checks, isolated package installation, updater packaging and website checks passed. No affected-PC or headset acceptance is implied.

Receipts belong under `extracted/device-dispatch-release-20261006`. Update these
statements only from completed checks. The previous public 1.0.8 assets remain
available unchanged; that version is a rollback option with a known startup failure
on the affected PC, not a successful compatibility baseline.

The rendered fixture uses the production hooks, real DXGI, GPU clear/readback,
non-TEST Present calls and separate render/monitor threads. Its Framework is an
explicit mutex stub with the 61-frame bootstrap sequence; it does not initialize
the full Framework, game or OpenXR. Windows are hidden, so successful GPU readback
and Present calls do not establish visible headset output. The separate entry
delivery gate models the reported callback asymmetry; it does not establish why
that asymmetry happens on the affected PC.

The final normal rendered check produced 90 successful GPU readbacks and 90
non-TEST Present calls: 61 initial DX11 observations, then 29 matching DX12
callbacks. Resize produced one DX12 callback and no retired DX11 resize callback.
Rendering continued after the retired DX11 object was destroyed. The separate
entry-gate repair check also delivered 29 DX12 callbacks. Its pinned 1.0.8 baseline
reproduced zero DX12 callbacks; that is a labelled model of the symptom, not proof
of the remote cause. Retained calls, Present1, hot switching and refusal paths
passed their regression checks. Offscreen launcher tests, isolated PackageStore
installation, updater packaging and 29 website checks passed.

| Artifact | SHA-256 |
| --- | --- |
| Backend | `a999659464f394f5072e8abc1c670b3811fba9457854f19ca3290deb3d16ccc8` |
| Source patch | `411736d63447b48ae216e5ef71b990835c3ce7b3939b38881d9782855a26768d` |
| Portable ZIP | `a8b4c4a748388bcbff8834a12693622e6294f21847665e453aac0f5a8016d095` |
| Launcher | `42e91ed6bddbe9ca1c0b55157f396f587cb3a4e880e6836b37ce25453b5dea98` |
| Setup | `c6768dc8f702d587be3e846d723ad4a760c899c509633ee2e2fa538059179739` |

All 136 overlay files match the native build inputs. The forward-applied source
patch reconstructs tree `a6a71a34fff61dde13b5a40b8eefa0a457074351` from the pinned
upstream base, preserving the other 237 index entries. The injector is unchanged.

## Player update and focused checks

When released, update the app to **1.0.9** while idle. In step 02, explicitly
select and install **beta-2026-10-06-device-dispatch**. Updating the app alone
preserves the selected VR package. Keep earlier packages and backups.

1. Keep the same Steam/Kuro installation, runtime and graphics settings. Launch
   once; verify the actual game/VR output rather than DLL loading alone.
2. If still flat, use **Stop waiting** and **Troubleshooting → Copy diagnostics**
   before another attempt. Do not run another injector in parallel.
3. If VR starts, check the UEVR menu, one existing view toggle and normal exit.
   Headset appearance and comfort need separate physical acceptance.
