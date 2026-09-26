# Release preparation

## Sharing update — 26 September

The website and launcher now expose **Code & contribute** and **Feedback**.
One `repositoryUrl` in `site/config.js` enables GitHub/source, fork and existing
issue links across the site, translated pages and launcher. Feedback prepares
a reviewable issue draft; it does not submit a report or attach logs. The
launcher supplies only its build/runtime labels to the form.

Use `python dev/configure-sharing.py --repository <confirmed-GitHub-URL>`
after choosing the actual repository. `--support-url <owner-payment-page>` is
optional. This configures local files and GitHub's issue chooser/funding file;
it does not create a repository, deploy, create a financial account or pay anyone.
`--check` is read-only. The source map is in [Code and contributions](DEVELOPING.md).

Generate the source review bundle with:

```powershell
python dev/package-wuwa.py developer-review --output extracted/release-packs/WuWa-VR-source-review-<date-time>
```

This packages the current 20:11 checkpoint's hash-verified native/UESDK patches,
matching active Lua, notices and reconstruction instructions. It excludes DLLs,
game files, recordings and personal profiles. It is prepared for author review,
with public redistribution scope still unconfirmed. A source review bundle
and a complete buildable public mod repository are different artifacts.

For the public showcase, use the allowlisted `public-site` export below. Review
that exact folder, then create/push a dedicated repository from it when approved.
Its manual Pages workflow deploys only `site/`. Do not push this development
workspace or its history. Keep the private mod ZIP out of public Releases until
component permissions and release acceptance are settled.

The September 25 presentation pass prepares a static website and private
recovery archives. It does **not** publish the repository, open donations or
establish permission to redistribute the combined runtime.

## Release artifacts

1. **Website preview:** `site/` contains real development captures, a player
   guide rendered from `docs/`, credits and an informational support page.
   It runs as static files, with no analytics, remote fonts, embeds or payments.
2. **Private recovery kit:** an existing local runtime plus its exact archived
   profile, source-change patches, available notices and per-file SHA-256 hashes.
   PDBs, logs, game files and diagnostic memory captures are omitted. This is
   not an installer, public release or fresh live-settings snapshot.
3. **Development workspace:** source, patches and research records remain here.
   Never upload this entire folder or its Git history as the website repository.
4. **Private portable launcher package:** `WuWa VR Launcher.exe`, a private
   CPython copy, the launcher helpers, the reviewed site and receipt-verified
   checkpoint runtimes/profiles, with a manifest and notices. Local test use
   only; it contains runtime files whose public redistribution is not cleared.

## Regenerate and check the website

The checked-in HTML needs no build step to host. When the player documents
change, run `node dev/build-site.cjs` with `marked@17.0.5` available. It can use
`WUWA_NODE_MODULES` or the prepared developer runtime. The generator renders
the guide and credits from their Markdown references.

`node dev/check-site.cjs` uses Playwright to serve only `site/` on loopback and
checks the generated pages under `/wuwa-vr/`, at desktop and phone sizes. It checks
local links, anchors, images, overflow, shortcut search/reset, no-JavaScript
fallback, and absent/invalid/valid-fixture support links. `WUWA_CHROMIUM` can
select an already installed Chromium executable. It saves screenshots and a
print-guide PDF under `extracted/presentation-20260925/browser`.

Preview screenshots must be visually inspected as well. These tests do not
establish deployed GitHub Pages behavior or headset readability.

## Create review exports

With Python 3.10+ from the project root, choose fresh output directories:

```powershell
python dev/package-wuwa.py public-site --output extracted/release-packs/showcase-preview
python dev/package-wuwa.py private-kit --build menu-camera-qol-20260924 --output extracted/release-packs/PRIVATE-menu-camera-2331
python dev/package-wuwa.py private-kit --build camera-tested-20260924 --output extracted/release-packs/PRIVATE-camera-2237
```

The tool never installs, uploads or overwrites an existing export. The public
export uses `release/public-files.json`, reviewed image hashes and disclosure
checks. It contains only the static site, an export README, a **manual-dispatch**
Pages workflow and manifests. Private archives verify runtime/profile/patch
identities against saved receipts, copy available notices and verify the ZIP
contents. Run their `Verify-Kit.ps1` for a read-only integrity check.

## Build the private portable launcher package

From the project root, with Visual Studio's C++ tools and a local 64-bit
CPython 3.10+ install (it is copied, not downloaded). Use new output names:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File dev\portable\Build-LauncherStub.ps1 -Output extracted\new-stub-folder
python dev\build-portable.py --python <CPython install folder> --stub "extracted\new-stub-folder\WuWa VR Launcher.exe" --build camera-tested-20260924 --build stereo-camera-20260926 --output "extracted\release-packs\WuWa-VR-Launcher-PRIVATE-<date-time>"
```

The builder refuses an existing output, verifies every runtime/profile file
against its checkpoint receipt, lets only an owner-tested build be the
default, precompiles the private standard library and checks the finished
ZIP byte for byte. `dev/test_portable_package.py` then tests the built ZIP
end to end when `WUWA_PORTABLE_ZIP` points at it. `node dev/portable/make-icon.cjs`
regenerates the icon if `site/media/mark.svg` changes.

## Adding a real gameplay clip

The site shows no video until the owner supplies one; never substitute other
footage or present a still or simulator capture as headset video.

1. Put the clip under `site/media/` as `.mp4` (H.264/AAC) or `.webm`, with an
   optional poster image. Keep it short and remove account names/IDs.
2. Add an entry to `release/media-provenance.json` with `"kind": "gameplay-video"`,
   `"ownerApproved": true`, the `file`, optional `poster`, a `caption`, the
   exact `build` name, `recorded` date and `headset` / runtime.
3. Add the clip (and poster) to `release/public-files.json` `files` and pin
   their SHA-256 values under `reviewedMedia`. The dashboard's guide preview
   serves reviewed media automatically.
4. Run `node dev/build-site.cjs` and the site checks. The home page then shows
   the clip with its caption above the still captures.

## Permissions and attribution still to settle

The pinned UEVR root license says All rights reserved. Its include-directory
MIT notice does not cover the whole backend. The original WuWa profile's
checkout has no top-level license. Public visibility is not a redistribution
grant. A public combined DLL/profile release therefore needs its allowed
scope clarified with the relevant owners, along with dependency notices and
any required matching source. Preserve exact credits and do not relicense
third-party files. This is a release-preparation finding, not legal advice.

The preserved community Lua explicitly credits **polar** as original author
and **SannpoKun** for modification; `mirudo2` is the repository owner. Older
workspace notes used those names imprecisely. The new credit page distinguishes
them. Discord posts/attachments and game data are not republished in the site.

## Publishing the showcase later

Choose a public repository name and review the standalone export first. There
is currently no remote configured for this development repository. Create a
separate repository from the reviewed export rather than publishing this
history. In GitHub Pages, select GitHub Actions and manually run the supplied
workflow when approved. It uploads only `site/` and does not run on push.
GitHub documents the [Pages workflow and environment permissions](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages).

No hosting account, repository or payment account was created by preparing
these files. The owner supplied `https://ko-fi.com/chronohax`; it is configured
locally in the support page, `site/config.js` and GitHub's funding file. Preparing
these links does not publish them. Keep support optional and separate from
availability/compatibility.
GitHub Sponsors is one possible route for eligible open-source contributors;
its [official overview](https://docs.github.com/en/sponsors/getting-started-with-github-sponsors/about-github-sponsors)
describes eligibility. No eligibility or existing account is assumed here.

## Before a public mod release

- Verify the exact game version and rebuild/signature compatibility.
- Test a clean setup, install, rollback and settings persistence on another path.
- Record physical headset checks for input/focus, menus, camera movement and exit.
- Resolve or clearly document one-eye NPC labels and duplicated preview reflections.
- Keep Native Stereo Fix off in the recommended starting profile.
- Check media for account IDs, private paths, private posts and overstated claims.
- Offer a small, exact build with a timestamp, manifest and honest known issues.

These are remaining release tasks, not claims that the current local kit
already passed them.
