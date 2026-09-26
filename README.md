# WuWa VR

[Website and visual guide](https://chronohaxx.github.io/wuwa-vr/) ·
[Releases](https://github.com/ChronoHaxx/wuwa-vr/releases) ·
[Report a bug or request a feature](https://chronohaxx.github.io/wuwa-vr/feedback.html) ·
[Optional Ko-fi support](https://ko-fi.com/chronohax)

An unofficial Wuthering Waves VR project built on praydog's UEVR and the
community's WuWa work. Current development includes Xbox controls, adjustable
HUD, first-person cameras and an optional 6DOF window. NPC labels, some
translucent materials, character-preview reflections and full-animation aiming
still have open issues. See the [known issues](docs/TROUBLESHOOTING.md).

**Mod downloads are not published yet.** The saved launcher packages contain
upstream components whose redistribution permission is being clarified. This
repository currently publishes the website, guides and issue tracker. It does
not claim that the native mod is MIT-licensed or ready for a general release.

**Code & contribute:** open `site/developers.html` for the source map and current
rendering issues. Fork this repository for website/guide contributions, or use
the issue templates for bugs, features and language requests. This export is
not the full native-mod source; a separate pinned source-review bundle is
prepared for upstream collaboration.

Static project showcase, player guide, community credits and optional creator
support page. Open `site/index.html` locally or serve the `site` directory.
No package install or build step is needed to view it.

This export intentionally has no game binaries, backend DLLs, private logs,
personal profiles, extracted game assets or development Git history. It is a
website preview, not a mod download. Original website and guide work has a
[scoped MIT license](LICENSE.md). Game captures and upstream components retain
their owners' rights. Please keep access free, credit authors and share
improvements; these are community requests, not additional MIT restrictions.

**Use the mod at your own risk: anti-cheat detection, account restrictions or
a permanent ban are possible.** It is unofficial and not approved by Kuro
Games. See the [risk notice](docs/RISK.md). Cosmetic use is not a safety guarantee.

## Deployment

Review the page copy and game captures. Only set the payment link in
`site/config.js` to a support account supplied and approved by the owner.
Donations do not buy promised updates, compatibility or permanent support.
Set `repositoryUrl` to the confirmed GitHub repository to enable source, fork,
existing-issue links and reviewable issue drafts. Configure both destinations
locally with `python dev/configure-sharing.py --repository <GitHub-URL>` and,
optionally, `--support-url <owner-payment-page>`. `--check` reports the current
state without editing. Nothing is posted automatically.
The site collects no payment details and has no analytics, external fonts or
third-party embeds. A configured support link opens the external provider.

This public repository contains the reviewed website export. Do not upload the
original development workspace or its history here. GitHub Pages uses GitHub
Actions; after reviewing and pushing a site update, manually run **Publish
reviewed showcase**. Ordinary pushes do not deploy it. The workflow uploads
only `site/`.

GitHub's [custom Pages workflow documentation](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages)
describes the environment and Pages permissions. The relative asset links work
under a project subpath as well as a domain root. No custom domain is configured.

`manifest.json` and `SHA256SUMS.txt` describe this export. Local generation and
browser checks do not establish live deployment, financial-account ownership or
in-game/headset acceptance.

## Editing

The generated pages work without build tools. To edit the Markdown or translations,
install a current Node.js LTS, run `npm install`, then `npm run build`. For optional
browser checks, run `npx playwright install chromium` and `npm run check`. These
are developer dependencies only; visitors need no Node or Python installation.
`release/site-media.json` holds public video descriptors. Private media provenance
and local capture paths are excluded, and are not required to regenerate the site.
Language sources are under `site/languages/`; Arabic uses right-to-left layout.
Initial translations need community review and do not translate the game or
the native UEVR interface.
