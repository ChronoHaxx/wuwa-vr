# Reconstruct the native source

Needs Git and UEVR's Windows C++ build tools. From this repository's root,
using a new `uevr-build` directory:

```powershell
git -c core.autocrlf=false clone https://github.com/praydog/UEVR.git uevr-build
git -C uevr-build checkout 4ee5c6b6162dee2291fc75f9dfc57667f6d45a2d
git -C uevr-build config core.autocrlf false
git -C uevr-build config submodule.dependencies/submodules/UESDK.url https://github.com/praydog/UESDK.git
git -C uevr-build -c core.autocrlf=false submodule update --init --recursive
git -C uevr-build/dependencies/submodules/UESDK checkout 14478dfae61e8385e0e62116718925c4099b1d18
git -C uevr-build -c core.autocrlf=false apply --exclude=dependencies/submodules/UESDK ../mod/source-changes/uevr-working-tree-full.patch
git -C uevr-build/dependencies/submodules/UESDK -c core.autocrlf=false apply ../../../../mod/source-changes/uesdk-working-tree.patch
```

Follow UEVR's build instructions and build the `uevr` target. These patches
already include the portal changes. Do not stack older patches.
Use one compiler worker on the shared PC. For an already configured MSBuild
project, `dev/build-native-limited.py --project <uevr.vcxproj> --log-dir <folder>`
sets `/m:1` and `/MP1`, uses low priority and stops above its memory budget.
The 3 October build peaked at 1.44 GiB after replacing the diagnostic rings'
large aggregate initializers with equivalent default initialization. The ring
test in `dev/test_diagnostic_rings.cpp` checks initialization over dirty memory
and publish/consume/reset behavior. This does not change rendering algorithms.
The subsequent full rebuild exposed a separate large variadic Lua key-enum
registration. Its homogeneous insertion replacement preserves all 143 key values,
read-only errors and `pairs` iteration; the isolated optimized compile peaked at
1.889 GiB. See `docs/launch-kit/COMPILER-ENUM-20261004.md` and
`dev/test_lua_enum.cpp`. Keep both repairs when reconstructing the source.
Forward application and source comparison passed; this is not a fresh-PC
build guarantee. Test in a separate profile, with the game closed when copying
all supplied Lua scripts (including the companion modules) and language catalogs.
Use the release for ordinary play.

## Injector source for the Steam and graphics settings beta

The native DLL alone does not fix the injector's automatic graphics-file writes.
For the graphics-r2 renderer and Steam release, reconstruct the companion injector from
`mod/injector/source.json`: checkout `mirudo2/Custom-UEVR-Injector` at
`95d7eee535dda4c59cfe812e4e7942ac05da5541`, then copy the `mod/injector/GUI/`
overlay into its `GUI/` directory. It includes the earlier local injection and
logging reliability changes as well as the graphics-file correction.

Build `GUI/Custom_UEVR_Injector.csproj` with Visual Studio MSBuild, configuration
`Release`, platform `x64`, `/m:1` and `/p:UseSharedCompilation=false`; its target
is .NET Framework 4.7.2. Keep existing dependency assemblies alongside the output.
Run `dev/test_injector_graphics.ps1 -AssemblyPath <exe> -OutputRoot <inert folder>`
using Windows PowerShell 5.1. That test loads the assembly and uses a never-shown
form with fixture files; it does not launch a game or invoke injection.

The injector suppresses automatic graphics serialization when the launcher's
policy receipt is present. The launcher remains responsible for validating the
receipt and migrating profiles with the game closed. Explicit injector slider
edits are startup overrides, not live-game controls. Preserve component licensing.

The Steam beta reuses the graphics-r2 renderer and extends the
injector overlay with `Program.cs`, `TargetPathBinding.cs` and
`Properties/AssemblyInfo.cs`. Copy the overlay recursively. Run
`dev/test_injector_target_path.ps1 -AssemblyPath <exe> -OutputRoot <inert folder>`
in Windows PowerShell 5.1 to check argument parsing, exact-path/instance selection
and the passive capability marker without enumerating or injecting a live game.
The launcher requires this marked injector before dispatching a Steam launch.
See `docs/launch-kit/STEAM-LAUNCHER-20261006.md` for verification boundaries.
