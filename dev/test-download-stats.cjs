// Isolated Node checks for dev/download-stats.cjs. No network: GitHub is a fake fetch.
const fs = require('node:fs'), path = require('node:path'), os = require('node:os');
const assert = require('node:assert/strict');
const stats = require('./download-stats.cjs');

const asset = (name, count) => ({name, download_count: count});
// Shaped like the real API response for beta-1-2-8 (17 Nov 2026).
const r128 = {tag_name: 'beta-1-2-8', published_at: '2026-11-17T15:38:13Z', draft: false, prerelease: true, assets: [
  asset('ChronoHaxx.WuWaVR-1.2.8-win-beta-full.nupkg', 9), asset('releases.win-beta.json', 1), asset('SHA256SUMS.txt', 2),
  asset('START-HERE.txt', 4), asset('WuWa-VR-Launcher.zip', 19), asset('WuWa-VR-Setup.exe', 9), asset('WuWa-VR-Source.zip', 1)]};
const r127 = {tag_name: 'beta-1-2-7', published_at: '2026-11-12T12:45:42Z', draft: false, prerelease: true, assets: [
  asset('WuWa-VR-Launcher.zip', 40), asset('WuWa-VR-Setup.exe', 12)]};
const draft = {tag_name: 'beta-1-2-9', published_at: null, draft: true, assets: [asset('WuWa-VR-Setup.exe', 3)]};
const clone = value => JSON.parse(JSON.stringify(value));

function fakeFetch(pages, seen = []) {
  return async (url, options) => {
    seen.push({url, headers: options.headers});
    const page = Number(new URL(url).searchParams.get('page'));
    if (pages === 'fail') return {ok: false, status: 502, json: async () => ({})};
    return {ok: true, status: 200, json: async () => pages[page - 1] || []};
  };
}

const checks = [];
const check = (name, fn) => checks.push([name, fn]);

check('classifies every asset kind a release publishes', () => {
  assert.equal(stats.classify('WuWa-VR-Setup.exe'), 'installer');
  assert.equal(stats.classify('ChronoHaxx.WuWaVR-win-beta-Setup.exe'), 'installer');
  assert.equal(stats.classify('ChronoHaxx.WuWaVR-1.2.8-win-beta-full.nupkg'), 'app_update');
  assert.equal(stats.classify('ChronoHaxx.WuWaVR-1.2.8-win-beta-delta.nupkg'), 'app_update');
  assert.equal(stats.classify('WuWa-VR-Launcher.zip'), 'mod_package');
  assert.equal(stats.classify('WuWa-VR-Source.zip'), 'source');
  assert.equal(stats.classify('SHA256SUMS.txt'), 'docs');
  assert.equal(stats.classify('releases.win-beta.json'), 'docs');
  assert.equal(stats.classify('mystery.bin'), 'other');
});

check('CSV fields with commas and quotes survive a round trip', () => {
  const fields = ['a', 'b'];
  const text = 'a,b\n' + stats.csvLine(fields, {a: 'x, "y"', b: 'line\nbreak'}) + '\n';
  assert.deepEqual(stats.parseCsv(text), [{a: 'x, "y"', b: 'line\nbreak'}]);
});

check('first run logs every asset, skips drafts and totals by category', () => {
  const next = stats.nextFiles({releases: [r128, r127, draft], date: '2026-11-23'});
  const log = stats.parseCsv(next.assets);
  assert.equal(log.length, 9);
  assert(!log.some(row => row.tag === 'beta-1-2-9'), 'drafts are not counted');
  const latest = JSON.parse(next.latest);
  assert.deepEqual(latest.totals, {all: 97, installer: 21, app_update: 9, mod_package: 59, source: 1, docs: 7, other: 0});
  assert.deepEqual(latest.releases.map(r => r.tag), ['beta-1-2-8', 'beta-1-2-7'], 'newest release first');
  assert.equal(latest.releases[0].installer, 9);
  assert.equal(stats.parseCsv(next.daily).length, 1);
});

check('a rerun on the same day with no change adds nothing', () => {
  const first = stats.nextFiles({releases: [r128, r127], date: '2026-11-23'});
  const again = stats.nextFiles({assetsCsv: first.assets, dailyCsv: first.daily, releases: [r128, r127], date: '2026-11-23'});
  assert.equal(again.changed, 0);
  assert.equal(again.assets, first.assets);
  assert.equal(again.daily, first.daily);
});

check('the next day logs only the assets whose count moved', () => {
  const first = stats.nextFiles({releases: [r128, r127], date: '2026-11-23'});
  const moved = clone(r128); moved.assets.find(a => a.name === 'WuWa-VR-Setup.exe').download_count = 12;
  const next = stats.nextFiles({assetsCsv: first.assets, dailyCsv: first.daily, releases: [moved, r127], date: '2026-11-24'});
  assert.equal(next.changed, 1);
  assert.equal(stats.parseCsv(next.assets).length, stats.parseCsv(first.assets).length + 1);
  const days = stats.parseCsv(next.daily);
  assert.deepEqual(days.map(d => [d.date, d.all, d.installer]), [['2026-11-23', '97', '21'], ['2026-11-24', '100', '24']]);
});

check('deleting a release keeps its downloads in the all-time total', () => {
  const first = stats.nextFiles({releases: [r128, r127], date: '2026-11-23'});
  const next = stats.nextFiles({assetsCsv: first.assets, dailyCsv: first.daily, releases: [r128], date: '2026-11-24'});
  const latest = JSON.parse(next.latest);
  assert.equal(latest.totals.all, 97);
  assert(latest.releases.find(r => r.tag === 'beta-1-2-7').assets.every(a => a.removed === true));
  assert(!latest.releases.find(r => r.tag === 'beta-1-2-8').assets.some(a => a.removed));
});

check('fetches every page and sends the token only when there is one', async () => {
  const many = Array.from({length: 100}, (_, i) => ({...r127, tag_name: 'old-' + i}));
  const seen = [];
  const releases = await stats.fetchReleases({repo: 'o/r', token: 'abc', fetchImpl: fakeFetch([many, [r128]], seen)});
  assert.equal(releases.length, 101);
  assert.deepEqual(seen.map(s => new URL(s.url).searchParams.get('page')), ['1', '2']);
  assert.equal(seen[0].headers.Authorization, 'Bearer abc');
  const anonymous = [];
  await stats.fetchReleases({repo: 'o/r', token: '', fetchImpl: fakeFetch([[r128]], anonymous)});
  assert.equal(anonymous[0].headers.Authorization, undefined);
});

check('an API failure writes nothing', async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'wuwa-download-stats-'));
  await assert.rejects(stats.run({dataDir: dir, token: '', fetchImpl: fakeFetch('fail')}), /GitHub API 502/);
  assert.deepEqual(fs.readdirSync(dir), []);
});

check('run writes the three data files and a README inside the data folder only', async () => {
  const parent = fs.mkdtempSync(path.join(os.tmpdir(), 'wuwa-download-stats-'));
  const dir = path.join(parent, 'downloads');
  await stats.run({dataDir: dir, date: '2026-11-23', token: '', fetchImpl: fakeFetch([[r128, r127]])});
  assert.deepEqual(fs.readdirSync(dir).sort(), ['README.md', 'assets.csv', 'daily.csv', 'latest.json']);
  assert.deepEqual(fs.readdirSync(parent), ['downloads']);
  await stats.run({dataDir: dir, date: '2026-11-24', token: '', fetchImpl: fakeFetch([[r128, r127]])});
  assert.equal(stats.parseCsv(fs.readFileSync(path.join(dir, 'daily.csv'), 'utf8')).length, 2);
});

(async () => {
  let failed = 0;
  for (const [name, fn] of checks) {
    try { await fn(); console.log('ok   ' + name); }
    catch (error) { failed++; console.log('FAIL ' + name + '\n     ' + (error.stack || error).toString().split('\n').slice(0, 3).join('\n     ')); }
  }
  console.log(failed ? `${failed} of ${checks.length} download-stats checks failed.` : `All ${checks.length} download-stats checks passed.`);
  process.exitCode = failed ? 1 : 0;
})();
