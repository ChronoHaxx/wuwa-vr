# Metrics

What WuWa VR measures, where the numbers live and how far they can be trusted.
Real numbers only: nothing here is an estimate unless it says so.

| Layer | What | Status |
| --- | --- | --- |
| 1. Downloads | GitHub's own download counter on each release asset, snapshotted daily | Built: `.github/workflows/download-stats.yml` |
| 2. Website | Daily visitors, page views, download clicks, guide sections reached, outbound clicks (cookie-free) | Signed off: [TRACKING-PLAN.md](TRACKING-PLAN.md) |
| 3. Launcher | Nothing (Option 0: the download counts above) | Decided: [LAUNCHER-METRICS-PROPOSAL.md](LAUNCHER-METRICS-PROPOSAL.md) |

## 1. Download counts

GitHub counts every download of every release asset and shows the cumulative
number through its API. It records no date, so a daily job copies the counters
into the repository and growth over time is kept from the first run on.
Counts from before the first run survive only as part of the cumulative total.

**No tracking is involved.** The job reads public counters; it learns nothing
about who downloaded.

- **Job:** `.github/workflows/download-stats.yml`, daily at 03:17 UTC, or **Run workflow** on the Actions tab.
- **Data:** the `metrics` branch, folder `downloads/`. A separate branch keeps the daily commits out of
  `main`, so release pushes never need a pull first and the site is not redeployed every day.
  - `assets.csv`: append-only log. A row each time an asset's cumulative count changes:
    `date, tag, published, asset, category, download_count`.
  - `daily.csv`: all-time totals by category, one row per day. This is the growth curve.
  - `latest.json`: per-release and per-asset counts from the last run.
- **Now, locally:** `node dev/download-stats.cjs` prints the current numbers per release and writes nothing.
- **Tests:** `node dev/test-download-stats.cjs` (no network). The workflow runs them before each snapshot.

### What each category means

The launcher downloads release assets itself, so the counters say more than
"how many people clicked Download". One player shows up in several rows:

| Category | Asset | Who downloads it | Read it as |
| --- | --- | --- | --- |
| `installer` | `WuWa-VR-Setup.exe` | A person, from the site, README or release page | New installs (and reinstalls) |
| `app_update` | `ChronoHaxx.WuWaVR-<version>-win-beta-full.nupkg` (and `-delta`) | The installed launcher updating itself (Velopack) | Existing installs that took that launcher update |
| `mod_package` | `WuWa-VR-Launcher.zip` | The launcher installing the VR mod (Install VR, auto-update), and people taking the portable ZIP | VR mod installs of that release |
| `source` | `WuWa-VR-Source.zip` | People reading or rebuilding the code | Interest from developers |
| `docs` | `SHA256SUMS.txt`, `START-HERE.txt`, `releases.win-beta.json` | People checking a download, the release page | Not players |

A new player: `installer` +1, then `mod_package` +1 on **Install VR**.
An existing player when a release lands: `app_update` +1, then `mod_package` +1.
So never add the categories together and call it "users".

### Limits

- A download is a request, not a person. The same person on two PCs, a retry
  after a failed download, a mirror or a bot each count again. Treat every
  number as an upper bound on people.
- The Velopack installer carries the launcher inside it, so a fresh install
  does not download the `.nupkg`. `app_update` therefore counts only installs
  that already existed (to the best of our reading of Velopack; check one
  release's numbers against your own update to confirm).
- The README and release-page links go straight to GitHub, so a site that
  measures download clicks (layer 2) sees fewer than `installer` does.
- Deleting a release or re-uploading an asset resets GitHub's counter. The log
  keeps the last count seen, so the all-time total does not drop; an asset
  later missing from GitHub is marked `"removed": true` in `latest.json`.
- GitHub turns off scheduled workflows in a public repository after 60 days
  with no activity in it. If no release ships for two months, check the
  Actions tab; it is not certain that the job's own commits count as activity.

### Quoting the numbers

- "*N* installer downloads across *M* public beta releases since *first release date*."
- "Each release is installed by about *K* existing launchers within a week"
  (the release's `app_update` count, 7 days after `published`, from `assets.csv`).
- Say "downloads", not "users" or "players". Layer 2 counts daily visitors to the site; a month's figure is visitor-days, not unique people.

## Metrics snapshot

Filled in after two to four weeks of daily data (first full read due mid
December 2026). Every figure gets its date and source.

| Measure | Value | As of | Source |
| --- | --- | --- | --- |
| Total installer downloads | — | — | `daily.csv`, `installer` |
| VR mod installs (all releases) | — | — | `daily.csv`, `mod_package` |
| Downloads per release | — | — | `latest.json` |
| Daily site visitors (average) and monthly visitor-days | — | — | Layer 2, once live |
| Top guides and languages | — | — | Layer 2, once live |
| Biggest drop-off between landing, download and setup guide | — | — | Layers 1 and 2 |
