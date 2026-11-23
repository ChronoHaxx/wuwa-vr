# WuWa VR download counts

Written daily by `.github/workflows/download-stats.yml` on `main` from the public
GitHub release asset counters. No visitor data. What the numbers mean, and their
limits, is in `docs/METRICS.md` on `main`.

- `assets.csv`: append-only log, one row each time an asset's cumulative count changes.
- `daily.csv`: all-time totals by category, one row per day.
- `latest.json`: per-release and per-asset counts from the last run.
