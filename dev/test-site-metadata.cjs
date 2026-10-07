// Isolated Node checks. No browser, network, deployment or package build.
const fs = require('node:fs'), path = require('node:path'), os = require('node:os');
const assert = require('node:assert/strict'), {execFileSync} = require('node:child_process');
const metadata = require('./site-metadata.cjs');
const root = path.resolve(__dirname, '..');
const fixture = fs.mkdtempSync(path.join(os.tmpdir(), 'wuwa-site-metadata-'));
const site = path.join(fixture, 'site');
const protectedFiles = ['google3ebe097a88f39e29.html', 'launcher-updates/releases.win-beta.json'];
const tracked = [...metadata.pages, ...protectedFiles, 'sitemap.xml'];
const snapshot = () => Object.fromEntries(tracked.map(file => [file, fs.readFileSync(path.join(site, file), 'utf8')]));
const head = html => /<head\b[^>]*>([\s\S]*?)<\/head>/i.exec(html)[1];
const outsideHead = html => html.replace(/<head\b[^>]*>[\s\S]*?<\/head>/i, '<head></head>');
const managedValues = (html, selector) => [...head(html).matchAll(selector)].map(match => match[1]);
const checks = [];
function generate(...args) {
  execFileSync(process.execPath, [...process.execArgv, path.join(fixture, 'dev/build-site.cjs'), ...args],
    {cwd: fixture, env: process.env, encoding: 'utf8', timeout: 30000, windowsHide: true});
}
function verify() {
  assert.equal(metadata.pages.length, 22);
  for (const file of metadata.pages) {
    const html = fs.readFileSync(path.join(site, file), 'utf8');
    assert.deepEqual(managedValues(html, /<link rel="canonical" href="([^"]+)">/g),
      [metadata.canonicalUrl(file)], 'Exactly one self canonical: ' + file);
    assert.equal((head(html).match(/<title\b/g) || []).length, 1, file);
    assert.equal((head(html).match(/<meta name="description"/g) || []).length, 1, file);
    assert(!/<meta\b[^>]*(?:name|http-equiv)=["'](?:robots|googlebot)["'][^>]*noindex/i.test(head(html)), file);
    assert.deepEqual(managedValues(html, /<meta property="og:url" content="([^"]+)">/g), [metadata.canonicalUrl(file)]);
    for (const key of ['og:title', 'og:description', 'og:type', 'og:site_name', 'twitter:card', 'twitter:title', 'twitter:description']) {
      assert.equal([...head(html).matchAll(new RegExp(`<(?:meta) (?:name|property)="${key}"`, 'g'))].length, 1, file + ' ' + key);
    }
  }
  const sitemap = fs.readFileSync(path.join(site, 'sitemap.xml'), 'utf8');
  const urls = [...sitemap.matchAll(/<loc>([^<]+)<\/loc>/g)].map(match => match[1]);
  assert.deepEqual(urls, metadata.pages.map(metadata.canonicalUrl));
  assert.equal(new Set(urls).size, 22);
  assert(urls.includes(metadata.baseUrl));
  assert(!urls.some(url => /index\.html|google3ebe|launcher-updates/.test(url)));
  for (const file of ['index.html', 'guide.html', 'l/en.html', 'l/zh-Hans.html']) {
    const html = fs.readFileSync(path.join(site, file), 'utf8');
    const title = /<title>([^<]+)<\/title>/.exec(head(html))[1];
    const description = /<meta name="description" content="([^"]+)">/.exec(head(html))[1];
    assert(title.includes(file.includes('zh-Hans') ? '鸣潮 VR' : 'Wuthering Waves VR'), file);
    assert(description.includes('Windows') && description.includes(file.includes('zh-Hans') ? '社区测试版' : 'community beta'), file);
    assert(!/3\.7/.test(title + description), file);
    assert.deepEqual(managedValues(html, /<meta property="og:image" content="([^"]+)">/g),
      [metadata.baseUrl + 'media/run-poster.jpg']);
  }
}
function verifyReleaseSections() {
  const guide = fs.readFileSync(path.join(site, 'guide.html'), 'utf8');
  const controls = /<section id="controls">([\s\S]*?)<\/section>/.exec(guide)[1];
  const recovery = /<section id="recovery">([\s\S]*?)<\/section>/.exec(guide)[1];
  assert(controls.includes('Fixture: current controls source.'));
  assert(recovery.includes('Fixture: current recovery source.'));
  for (const text of ['Toggle mono theatre', 'Toggle stereoscopic screen', 'Toggle the temporary 10× diorama', 'Optional Quest controllers for walking']) {
    assert(controls.includes(text), 'Current control missing: ' + text);
  }
  assert.equal((controls.match(/id="control-search"/g) || []).length, 1);
  assert.equal((controls.match(/id="control-count"/g) || []).length, 1);
  const bodies = [...controls.matchAll(/<tbody>([\s\S]*?)<\/tbody>/g)];
  assert(bodies.length > 0);
  for (const [, body] of bodies) assert(!/<tr>/.test(body), 'Unsearchable control row');
  assert((controls.match(/<tr data-control-row(?:\s[^>]*)?>/g) || []).length > 30);
  const questRows = [...controls.matchAll(/<tr data-control-row data-context="([^"]*)">([\s\S]*?)<\/tr>/g)]
    .filter(([, context]) => context.toLowerCase().includes('quest')).map(([, , body]) => body);
  assert(questRows.some(row => row.includes('Right A / B')) && questRows.some(row => row.includes('Left Menu')),
    'Quest heading must remain searchable through each mapping row context');
  assert(recovery.includes('Cinematic framing is on by default') && recovery.includes('advanced portable fallback'));
  assert(recovery.includes('prerendered movie switching has not been tested'));
  assert(recovery.includes('Headset comfort') && recovery.includes('Reset HUD aspect'));
  for (const file of ['index.html', 'l/en.html', 'l/zh-Hans.html']) {
    const html = fs.readFileSync(path.join(site, file), 'utf8');
    assert(html.includes(file.includes('zh-Hans') ? '预渲染影片也未测试' : 'prerendered movie has not been tested'), file);
  }
  const risk = fs.readFileSync(path.join(site, 'risk.html'), 'utf8');
  assert(!risk.includes('combined-mod distribution remains pending'));
  assert(risk.includes('Publication does not change') && risk.includes('Account restrictions'));
  for (const [name, html] of [['guide.html', guide], ['risk.html', risk]]) {
    for (const [, href] of html.matchAll(/href="([^"]*)"/g)) {
      if (/^(https?:|mailto:)/.test(href)) continue;
      const url = new URL(href, 'https://fixture.invalid/' + name);
      const target = path.join(site, decodeURIComponent(url.pathname));
      assert(fs.existsSync(target), `Broken local guide link: ${name} ${href}`);
      if (url.hash) assert(fs.readFileSync(target, 'utf8').includes(`id="${decodeURIComponent(url.hash.slice(1))}"`),
        `Missing guide anchor: ${name} ${href}`);
      assert(!/\.md(?:#|$)/.test(href), 'Unmapped local Markdown link');
    }
  }
}
try {
  // Copy only generator inputs. Videos are existence-only fixtures: generators
  // do not decode media, and copying large recordings would add no coverage.
  fs.cpSync(path.join(root, 'site'), site, {recursive: true, filter: file => !/\.(mp4|webm)$/i.test(file)});
  for (const directory of ['docs', 'LICENSES']) fs.cpSync(path.join(root, directory), path.join(fixture, directory), {recursive: true});
  for (const file of ['CREDITS.md', 'SUPPORT.md', 'CONTRIBUTING.md',
    'release/site-media.json', 'dev/build-site.cjs', 'dev/site-metadata.cjs', 'dev/site-layout.cjs',
    'dev/build-community.cjs', 'dev/build-home.cjs', 'dev/site-report.cjs', 'dev/site-status.json',
    'release/security-reports.json']) {
    fs.mkdirSync(path.dirname(path.join(fixture, file)), {recursive: true});
    fs.copyFileSync(path.join(root, file), path.join(fixture, file));
  }
  fs.cpSync(path.join(root, 'dev/i18n'), path.join(fixture, 'dev/i18n'),
    {recursive: true, filter: file => !file.includes(path.sep + 'work')});
  for (const item of JSON.parse(fs.readFileSync(path.join(fixture, 'release/site-media.json'), 'utf8')).media) {
    assert(/^site\/media\/[\w.-]+\.(mp4|webm)$/.test(item.file));
    fs.writeFileSync(path.join(fixture, item.file), 'existence-only video fixture');
  }
  const before = snapshot();
  generate('--metadata-only');
  verify();
  const first = snapshot();
  for (const file of metadata.pages) assert.equal(outsideHead(first[file]), outsideHead(before[file]), 'Body/shell changed: ' + file);
  for (const file of protectedFiles) assert.equal(first[file], before[file], 'Protected bytes changed: ' + file);
  checks.push('22 self canonicals and share tags; EN/zh entry text; root sitemap; body/shell/verification/feed preserved');
  generate('--metadata-only');
  assert.deepEqual(snapshot(), first);
  checks.push('metadata-only regeneration is byte-identical');

  const duplicate = first['index.html'].replace('</head>', '<link href="https://old.invalid/" rel="canonical"><meta name="twitter:card" content="summary"></head>');
  const repaired = metadata.updateHtml('index.html', duplicate);
  assert.equal(repaired, first['index.html']);
  assert.throws(() => metadata.updateHtml('google3ebe097a88f39e29.html', '<head><title>x</title><meta name="description" content="x"></head>'), /Not a public content page/);
  checks.push('stale/duplicate owned metadata replaced; verification page rejected');

  fs.appendFileSync(path.join(fixture, 'docs/CONTROLS.md'), '\n\nFixture: current controls source.\n');
  fs.appendFileSync(path.join(fixture, 'docs/TROUBLESHOOTING.md'), '\n\nFixture: current recovery source.\n');
  generate('--launcher-release');
  verify();
  verifyReleaseSections();
  checks.push('scoped current controls/recovery ingestion, searchable tables, EN/zh cutscene limits, risk notice and local links');
  const scoped = snapshot();
  for (const file of protectedFiles) assert.equal(scoped[file], before[file]);
  generate('--launcher-release');
  assert.deepEqual(snapshot(), scoped);
  checks.push('scoped release generator preserves metadata and is byte-identical on repeat');

  generate();
  verify();
  verifyReleaseSections();
  const full = snapshot();
  for (const file of protectedFiles) assert.equal(full[file], before[file]);
  generate();
  assert.deepEqual(snapshot(), full);
  checks.push('full generator preserves metadata/protected files and is byte-identical on repeat');
  const mediaPath = path.join(fixture, 'release/site-media.json');
  const media = JSON.parse(fs.readFileSync(mediaPath, 'utf8'));
  media.media.forEach(item => { item.ownerApproved = false; });
  fs.writeFileSync(mediaPath, JSON.stringify(media));
  assert.throws(() => metadata.apply(site), /owner-approved/);
  assert.deepEqual(snapshot(), full);
  checks.push('unapproved share media fails before page writes');
  console.log(JSON.stringify({passed: checks.length, pages: metadata.pages.length, checks}, null, 2));
} finally {
  // Delete only the exact temporary tree created above, after checking its scope.
  const resolved = path.resolve(fixture), temporaryRoot = path.resolve(os.tmpdir()) + path.sep;
  assert(resolved.startsWith(temporaryRoot) && path.basename(resolved).startsWith('wuwa-site-metadata-'));
  fs.rmSync(resolved, {recursive: true, force: true});
}
