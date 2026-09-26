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
Forward application and source comparison passed; this is not a fresh-PC
build guarantee. Test in a separate profile, with the game closed when copying
the three Lua scripts and language catalogs. Use the release for ordinary play.
