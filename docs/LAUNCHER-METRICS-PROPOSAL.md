# Launcher metrics: proposal and decision

**Status: decided 23 Nov 2026: Option 0.** The launcher sends no usage data,
and nothing here gets built without the owner's explicit go-ahead. Silent telemetry is off
the table: modding communities react badly to it, rightly.

## What the launcher contacts today

| When | Request | Who sees it |
| --- | --- | --- |
| Every launcher start (and **Check for updates**) | Update feed `chronohaxx.github.io/wuwa-vr/launcher-updates/releases.win-beta.json` (Velopack, `LauncherUpdateService.FeedUrl`) | GitHub Pages |
| Every launcher start | Mod catalog `raw.githubusercontent.com/.../launcher/native/catalog.public.json` (`RepoClient.CatalogUrl`) | GitHub |
| Launcher update | `ChronoHaxx.WuWaVR-<version>-win-beta-full.nupkg` from the release | GitHub (counted, see below) |
| Install VR, mod auto-update | `WuWa-VR-Launcher.zip` from the release | GitHub (counted) |

Velopack adds query parameters to the feed request. Per its source, these are
`localVersion`, `id` and `stagingId`, where `stagingId` is a random id Velopack
keeps per install for staged rollouts. Check this against the pinned Velopack
version before relying on it. GitHub already receives these; we never do.

## Option 0: neither (works today, zero code)

Layer 1 already counts the launcher's own downloads per release
([METRICS.md](METRICS.md)):

- `app_update` (the `.nupkg`) = existing installs that took that launcher update.
- `mod_package` (`WuWa-VR-Launcher.zip`) = VR mod installs of that release, by the launcher or the portable ZIP.

**Gives:** installs reached per release and how fast each release spreads.
**Doesn't give:** how many launchers open per day or week between releases,
which versions are still in use, or which settings people use.
**Disclosure:** none beyond the privacy note's "GitHub counts downloads".

## Option A: count update checks at an endpoint we control

Point `FeedUrl` at a Cloudflare Worker that counts the request and redirects
to the same GitHub Pages feed. (The feed downloader wraps Velopack's
`HttpClientFileDownloader`, and .NET follows HTTPS redirects by default; a
launcher test would confirm it before release.) Each
request adds one row: the day, the channel, and `localVersion` if it matches a
published version (else `other`). The Worker drops `id`, `stagingId`, the IP and
the browser string, and never logs the request.

**Gives:** launcher starts per day, and the share of starts on each version
(version adoption). That is an estimate of activity, not of people: one player
opening the launcher three times counts three. Report it as "launcher starts".
**Doesn't give:** unique installs or weekly actives (that would need an id,
which is the line not to cross), or settings.
**Costs:**
- One launcher release to change `FeedUrl`; older launchers keep using the
  Pages feed until they update themselves.
- Updates gain a dependency on the Worker. Mitigation: if the Worker fails,
  the launcher retries the Pages feed directly (a small change in
  `VelopackLauncherUpdateAdapter`, with a test).
- Disclosure: a line in the README, the privacy note in ten languages, and the
  release notes of the version that introduces it.
- Uses the same Worker and dataset as the site events (`site=wuwa-vr-launcher`).

## Option B: opt-in "share anonymous usage stats", off by default

A setting in the launcher, off until the player turns it on, with the exact
list of what is sent next to it and a "show what would be sent" view. No id.
Proposed events:

| Event | Properties | Decision |
| --- | --- | --- |
| `launcher_start` | version | Activity among people who opted in |
| `vr_session_start` | runtime (`steamvr`, `oculus`, `virtual_desktop`, `other`), game copy (`steam`, `kuro`) | Which setups to test first |
| `vr_session_end` | length bucket (`<5m`, `5-30m`, `30-120m`, `>2h`) | Whether sessions end early (comfort, crashes) |
| `settings_snapshot` | which options differ from the defaults, from a fixed list | Which settings people use or never find |

**Gives:** the only route to "which settings people use".
**Costs:**
- Few people opt in (often a few percent), and those who do are the keenest,
  so the sample is biased.
- Launcher code, tests and about 10 new strings in ten languages.
- Settings live in the VR mod's config; the launcher would read them at its
  next start. The injected mod itself must stay network-free, for players'
  trust and anti-cheat optics.
- Even opt-in, a "telemetry" headline can cost goodwill; the list, the default
  of off and the open source code are the defence.

## Recommendation

1. **Now: Option 0.** It is already running and answers "how many install,
   per release" without touching the launcher.
2. **Option A at the next launcher release, if** a "launcher starts per week"
   trend is worth one disclosed, low-risk change for showcasing.
3. **Option B: not now.** The site's FAQ, shortcut-search and guide events,
   plus feedback reports, answer "what don't people know about" first.
   Revisit if a specific settings decision needs real usage data.

## Decision

- [x] 0 only  - [ ] A  - [ ] B  - [ ] A and B
- Claude, delegated by Hazimi ("do them all for me"), 23 Nov 2026, following the recommendation above.
  A and B stay open: either needs Hazimi's own go-ahead before any launcher code changes.
