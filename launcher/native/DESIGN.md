# Why this small desktop manager

Reference check: 3 October 2026. This explains the current candidate rather
than proposing a second launcher backend.

| Existing project | Relevant parts | Decision for this candidate |
| --- | --- | --- |
| [CircuitLordVRModInstaller](https://github.com/CircuitLord/CircuitLordVRModInstaller) | MIT installer; .NET Framework 4.8 single EXE, release manifests, SHA-256 downloads, explicit ownership of installed files. | Adapt its checksum/HTTPS pattern and ownership approach. The reviewed source revision is pinned in `THIRD-PARTY-NOTICES.txt`; retain the notice in distributions. Do not use its closed mod payloads. |
| [Velopack](https://github.com/velopack/velopack) | MIT application installer/updater, delta packages and a self-updating portable application format. | A plausible future choice for updating the manager EXE itself. The current requirement is primarily installing versioned WuWa payload ZIPs; adding an application-update feed does not replace game detection, authenticated backend control or game-idle checks. No Velopack code is included. |

The candidate uses Windows' .NET Framework assemblies and the project's existing
Python/PowerShell backend. It does not add a second injector, profile writer or
runtime switcher. Its download cache and version selection are specific to our
portable package manifest and settings-preservation contract.

The manager does **not** silently update its own EXE. Release payload updates,
repair and rollback are supported; replacing the manager itself is currently a
separate download. A future self-updater should use a proven framework rather
than an executable-replacement mechanism added to this code.

Evidence remains split: component tests and a real public-package download /
helper smoke pass, while native input, UAC, game launch, capture and headset
acceptance are pending. See `README.md` for exact current evidence.
