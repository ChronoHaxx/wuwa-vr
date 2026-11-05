// Original site tooling, MIT. Fails when the website, the README or the docs pointers
// differ from their sources: rebuilds them in a temporary copy with the release
// command (node dev/build-site.cjs --launcher-release) and compares every output.
// The translation check (dev/i18n/check_all.py) runs it, so a release cannot ship
// a stale page. Node standard library plus marked (the site build's own dependency).
//   node dev/check-site-sync.cjs
const fs = require('node:fs'), path = require('node:path'), os = require('node:os');
const {execFileSync} = require('node:child_process');
const root = path.resolve(__dirname, '..');
const fixture = fs.mkdtempSync(path.join(os.tmpdir(), 'wuwa-site-sync-'));
const copy = (from, filter) => {
  const source = path.join(root, from), target = path.join(fixture, from);
  if (!fs.existsSync(source)) return;
  fs.mkdirSync(path.dirname(target), {recursive: true});
  fs.cpSync(source, target, {recursive: true, filter});
};
// Generated files: every site page and the sitemap, the README and the docs pointers.
const outputs = dir => {
  const list = [];
  const walk = rel => {
    for (const entry of fs.readdirSync(path.join(dir, rel), {withFileTypes: true})) {
      const name = path.posix.join(rel, entry.name);
      if (entry.isDirectory()) { if (!['media', 'fonts', 'launcher-updates'].includes(entry.name)) walk(name); }
      else if (/\.(html|xml)$/.test(entry.name)) list.push(name);
    }
  };
  walk('site');
  return list.concat(Object.keys(require('./build-readme.cjs').outputs())).sort();
};
let failed = [];
try {
  // Media are existence-only for videos: the generators never decode them.
  copy('site', file => !/\.(mp4|webm)$/i.test(file));
  for (const file of fs.readdirSync(path.join(root, 'site/media'))) {
    if (/\.(mp4|webm)$/i.test(file)) fs.writeFileSync(path.join(fixture, 'site/media', file), 'video fixture');
  }
  for (const item of ['dev/i18n', 'dev/site-content', 'docs', 'LICENSES', 'mod/localization/wuwa', 'mod/controls.json',
    'release/site-media.json', 'release/security-reports.json', 'CREDITS.md', 'SUPPORT.md', 'CONTRIBUTING.md', 'README.md',
    'dev/site-status.json']) copy(item, file => !file.includes(path.sep + 'work'));
  for (const file of fs.readdirSync(__dirname)) if (/\.cjs$/.test(file)) copy('dev/' + file);
  execFileSync(process.execPath, [path.join(fixture, 'dev/build-site.cjs'), '--launcher-release'],
    {cwd: fixture, env: process.env, encoding: 'utf8', timeout: 120000, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe']});
  const built = outputs(fixture), committed = outputs(root);
  for (const file of new Set([...built, ...committed])) {
    const a = path.join(fixture, file), b = path.join(root, file);
    if (!fs.existsSync(a)) failed.push(`${file}: not produced by the build (remove it or add its source)`);
    else if (!fs.existsSync(b)) failed.push(`${file}: missing; run node dev/build-site.cjs --launcher-release`);
    else if (fs.readFileSync(a, 'utf8') !== fs.readFileSync(b, 'utf8')) failed.push(`${file}: stale; run node dev/build-site.cjs --launcher-release`);
  }
  const problems = require('./build-guide.cjs').problems();
  failed = failed.concat(problems);
  console.log(failed.length ? failed.join('\n') : `OK: ${built.length} website, README and docs pointer files match their sources`);
} catch (error) {
  failed.push(String(error.stderr || error.message || error));
  console.log(failed.join('\n'));
} finally {
  fs.rmSync(fixture, {recursive: true, force: true});
}
process.exitCode = failed.length ? 1 : 0;
