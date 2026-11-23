// Original independent tooling: MIT. Snapshot of GitHub release download counts.
// No tracking: this only reads the public counter GitHub keeps on every release asset.
//
//   node dev/download-stats.cjs <data-dir>   append today's snapshot (the daily workflow does this)
//   node dev/download-stats.cjs              print the current numbers, write nothing
//
// GITHUB_TOKEN (optional) raises the API rate limit; GITHUB_REPOSITORY overrides the repository.
// Files in <data-dir>, all reconstructable from assets.csv:
//   assets.csv   one row each time an asset's cumulative count changes (append-only log)
//   daily.csv    one row per day: all-time totals by category
//   latest.json  per-release and per-asset counts as of the last run
const fs = require('node:fs'), path = require('node:path');

const REPO = process.env.GITHUB_REPOSITORY || 'ChronoHaxx/wuwa-vr';
const CATEGORIES = ['installer', 'app_update', 'mod_package', 'source', 'docs', 'other'];
const ASSET_FIELDS = ['date', 'tag', 'published', 'asset', 'category', 'download_count'];
const DAILY_FIELDS = ['date', 'all', ...CATEGORIES];

// What a download of each asset means; see docs/METRICS.md.
function classify(name) {
  if (/\.nupkg$/i.test(name)) return 'app_update';             // the launcher updating itself (Velopack)
  if (/setup[^/]*\.exe$/i.test(name)) return 'installer';      // a person running the installer
  if (/source[^/]*\.zip$/i.test(name)) return 'source';
  if (/\.zip$/i.test(name)) return 'mod_package';              // the launcher installing the mod, or the portable ZIP
  if (/\.(txt|json|md|sig|asc)$/i.test(name)) return 'docs';   // checksums, notes, update feed copies
  return 'other';
}

// Minimal RFC 4180 CSV: quote a field only when it needs it.
const csvField = value => /[",\r\n]/.test(String(value)) ? `"${String(value).replace(/"/g, '""')}"` : String(value);
const csvLine = (fields, row) => fields.map(f => csvField(row[f] ?? '')).join(',');
function parseCsv(text) {
  const rows = [];
  let row = [], field = '', quoted = false;
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if (quoted) {
      if (c === '"' && text[i + 1] === '"') { field += '"'; i++; }
      else if (c === '"') quoted = false;
      else field += c;
    } else if (c === '"') quoted = true;
    else if (c === ',') { row.push(field); field = ''; }
    else if (c === '\n' || c === '\r') {
      if (c === '\r' && text[i + 1] === '\n') i++;
      row.push(field); rows.push(row); row = []; field = '';
    } else field += c;
  }
  if (field !== '' || row.length) { row.push(field); rows.push(row); }
  const [header, ...body] = rows.filter(r => r.length > 1 || r[0] !== '');
  return header ? body.map(r => Object.fromEntries(header.map((h, i) => [h, r[i] ?? '']))) : [];
}

async function fetchReleases({repo = REPO, token = process.env.GITHUB_TOKEN, fetchImpl = globalThis.fetch} = {}) {
  const releases = [];
  for (let page = 1; ; page++) {
    const headers = {'Accept': 'application/vnd.github+json', 'X-GitHub-Api-Version': '2022-11-28', 'User-Agent': 'wuwa-vr-download-stats'};
    if (token) headers.Authorization = `Bearer ${token}`;
    const response = await fetchImpl(`https://api.github.com/repos/${repo}/releases?per_page=100&page=${page}`, {headers});
    if (!response.ok) throw Error(`GitHub API ${response.status} on page ${page}`);
    const batch = await response.json();
    if (!Array.isArray(batch)) throw Error('Unexpected GitHub API response');
    releases.push(...batch);
    if (batch.length < 100) return releases;
  }
}

// Published releases only: a draft's counter is the owner testing uploads.
function flatten(releases, date) {
  return releases.filter(r => !r.draft).flatMap(r => (r.assets || []).map(a => ({
    date, tag: r.tag_name, published: (r.published_at || '').slice(0, 10), asset: a.name,
    category: classify(a.name), download_count: Number(a.download_count) || 0
  })));
}

const key = row => `${row.tag}\u0000${row.asset}`;

// Fold the log into the last known row per asset. Assets later removed from GitHub keep
// their last count, so deleting an old release never makes the all-time total drop.
function lastKnown(logRows) {
  const known = new Map();
  for (const row of logRows) known.set(key(row), {...row, download_count: Number(row.download_count) || 0});
  return known;
}

function totals(rows) {
  const sum = Object.fromEntries(['all', ...CATEGORIES].map(c => [c, 0]));
  for (const row of rows) { sum.all += row.download_count; sum[row.category] += row.download_count; }
  return sum;
}

function summarise(known, current, date) {
  const live = new Set(current.map(key)), byTag = new Map();
  for (const row of known.values()) {
    if (!byTag.has(row.tag)) byTag.set(row.tag, {tag: row.tag, published: row.published, rows: []});
    byTag.get(row.tag).rows.push(row);
  }
  const releases = [...byTag.values()].sort((a, b) => b.published.localeCompare(a.published) || b.tag.localeCompare(a.tag)).map(r => ({
    tag: r.tag, published: r.published, ...totals(r.rows),
    assets: r.rows.sort((a, b) => a.asset.localeCompare(b.asset)).map(a => ({
      name: a.asset, category: a.category, downloads: a.download_count, ...(live.has(key(a)) ? {} : {removed: true})
    }))
  }));
  return {schema: 1, repository: REPO, date, note: 'Cumulative GitHub release asset download counts. See docs/METRICS.md on main for what each category means.',
    totals: totals([...known.values()]), releases};
}

// Pure: previous file contents in, next file contents out.
function nextFiles({assetsCsv = '', dailyCsv = '', releases, date}) {
  const log = parseCsv(assetsCsv), before = lastKnown(log);
  const current = flatten(releases, date);
  const changed = current.filter(row => before.get(key(row))?.download_count !== row.download_count);
  const known = lastKnown([...log, ...changed]);
  const appended = changed.map(row => csvLine(ASSET_FIELDS, row) + '\n').join('');
  const assets = (assetsCsv.trim() ? assetsCsv.replace(/\r?\n?$/, '\n') : ASSET_FIELDS.join(',') + '\n') + appended;
  const days = parseCsv(dailyCsv).filter(row => row.date !== date);
  days.push({date, ...totals([...known.values()])});
  days.sort((a, b) => a.date.localeCompare(b.date));
  const daily = [DAILY_FIELDS.join(','), ...days.map(row => csvLine(DAILY_FIELDS, row))].join('\n') + '\n';
  return {assets, daily, latest: JSON.stringify(summarise(known, current, date), null, 2) + '\n', changed: changed.length};
}

const README = `# WuWa VR download counts

Written daily by \`.github/workflows/download-stats.yml\` on \`main\` from the public
GitHub release asset counters. No visitor data. What the numbers mean, and their
limits, is in \`docs/METRICS.md\` on \`main\`.

- \`assets.csv\`: append-only log, one row each time an asset's cumulative count changes.
- \`daily.csv\`: all-time totals by category, one row per day.
- \`latest.json\`: per-release and per-asset counts from the last run.
`;

async function run({dataDir, date = new Date().toISOString().slice(0, 10), ...options} = {}) {
  const releases = await fetchReleases(options);
  const read = file => { try { return fs.readFileSync(path.join(dataDir, file), 'utf8'); } catch { return ''; } };
  const next = nextFiles({assetsCsv: read('assets.csv'), dailyCsv: read('daily.csv'), releases, date});
  fs.mkdirSync(dataDir, {recursive: true});
  // Write every file to a temporary name first so a failure never leaves a half-updated set.
  const files = {'assets.csv': next.assets, 'daily.csv': next.daily, 'latest.json': next.latest};
  for (const [file, text] of Object.entries(files)) fs.writeFileSync(path.join(dataDir, file + '.tmp'), text);
  for (const file of Object.keys(files)) fs.renameSync(path.join(dataDir, file + '.tmp'), path.join(dataDir, file));
  fs.writeFileSync(path.join(dataDir, 'README.md'), README);
  return next;
}

function print(summary) {
  const t = summary.totals;
  console.log(`${REPO} on ${summary.date}: ${t.all} downloads (installer ${t.installer}, app update ${t.app_update}, mod package ${t.mod_package})`);
  for (const r of summary.releases.filter(r => r.all > 0))
    console.log(`  ${r.tag.padEnd(36)} ${r.published}  installer ${String(r.installer).padStart(5)}  app update ${String(r.app_update).padStart(5)}  mod package ${String(r.mod_package).padStart(5)}  all ${String(r.all).padStart(6)}`);
}

module.exports = {classify, parseCsv, csvLine, fetchReleases, flatten, lastKnown, totals, nextFiles, run, CATEGORIES};

if (require.main === module) {
  const dataDir = process.argv[2];
  const job = dataDir
    ? run({dataDir: path.resolve(dataDir)}).then(r => console.log(`Snapshot written: ${r.changed} asset counts changed.`))
    : fetchReleases().then(releases => print(JSON.parse(nextFiles({releases, date: new Date().toISOString().slice(0, 10)}).latest)));
  job.catch(error => { console.error(error.message || error); process.exitCode = 1; });
}
