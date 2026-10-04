# Releases

The [4 October launcher beta](https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-2026-10-04-launcher)
provides **WuWa-VR-Setup.exe**, the per-user Windows installer for desktop app
**1.0.0**, and backend **npc-rim-20261004** for game **3.7**.
**WuWa-VR-Launcher.zip** remains the advanced portable fallback. Matching source,
setup instructions and download checksums accompany the release.
The thin EXE installs the launcher only. First-time VR installation downloads
the separate approximately 54 MB package; keep that internet requirement visible.

The installed app uses Velopack 1.2.161 for launcher updates from the stable Pages
feed `https://chronohaxx.github.io/wuwa-vr/launcher-updates/`.
Publish releases.win-beta.json on the site with absolute GitHub package URLs; upload the application packages to the paired GitHub release first. The app feed
does not use GitHub's latest-release selection. Its separate VR package
catalog supports installing, repairing and switching verified packages while
preserving player data. Keep launcher update metadata and VR package metadata
distinct; a VR package update does not replace the launcher executable.

The NPC rim workaround starts off, removes intended toon-rim lighting as well
as the tested mismatch, and still needs physical-headset checking. The underlying
stereo fault remains unresolved. Keep these limits in the public release notes.

Use a new version/tag for each checkpoint. Keep native source, profiles, component
notices and hashes paired. Verify the installer, portable ZIP, update feed and
matching source before publishing. Never publish live profiles, logs, account
IDs or raw captures by default. Publication does not relicense upstream work or
imply its authors' endorsement.

Website release inputs are `dev/site-status.json`, `docs/START-HERE.md` and
`dev/i18n/launcher-release.{en,zh-Hans}.json`. Run
`node dev/build-site.cjs --launcher-release` to refresh only the existing
download/setup pages, preserving unrelated content and verification files.
The normal full generator also applies the release refresh afterward.

The website points to the explicit beta installer asset. Upload assets and feeds
before deploying the new download links. Keep the previous release available;
do not mark the beta stable merely to use GitHub's latest-stable shortcut.

The reproducible packaging and publication commands are documented in
[INSTALLER-RELEASING.md](../launcher/native/INSTALLER-RELEASING.md).
