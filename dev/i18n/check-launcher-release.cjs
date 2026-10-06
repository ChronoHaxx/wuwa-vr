// Loopback-only UI check. Never follows download links or invokes the installer.
const fs = require('node:fs'), path = require('node:path'), http = require('node:http');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '../..'), site = path.join(root, 'site');
const modules = process.env.WUWA_NODE_MODULES || path.join(process.env.USERPROFILE || '', '.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
const {chromium} = require(require.resolve('playwright', {paths: [root, modules]}));
const out = path.resolve(process.argv[2] || path.join(root, 'dev/i18n/work/launcher-release'));
const release = JSON.parse(fs.readFileSync(path.join(root, 'dev/site-status.json'), 'utf8'));
const {tag} = release;
const server = http.createServer((req, res) => {
  const file = path.resolve(site, '.' + decodeURIComponent(new URL(req.url, 'http://localhost').pathname));
  if (!file.startsWith(site + path.sep) || !fs.existsSync(file) || !fs.statSync(file).isFile()) { res.writeHead(404); return res.end(); }
  res.setHeader('Content-Type', ({'.html': 'text/html; charset=utf-8', '.js': 'text/javascript', '.css': 'text/css', '.png': 'image/png', '.svg': 'image/svg+xml'})[path.extname(file)] || 'application/octet-stream');
  res.end(fs.readFileSync(file));
});
(async () => {
  fs.mkdirSync(out, {recursive: true});
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const base = `http://127.0.0.1:${server.address().port}`;
  const browser = await chromium.launch({headless: true, ...(process.env.WUWA_CHROMIUM ? {executablePath: process.env.WUWA_CHROMIUM} : {})});
  const errors = [], checks = [];
  try {
    const context = await browser.newContext();
    await context.route('**/*', route => route.request().url().startsWith(base + '/') ? route.continue() : route.abort());
    const page = await context.newPage();
    page.on('pageerror', error => errors.push(String(error)));
    page.on('response', response => { if (response.status() >= 400) errors.push(`${response.status()} ${response.url()}`); });
    for (const width of [1440, 390, 320]) {
      await page.setViewportSize({width, height: 900});
      for (const name of ['index.html', 'guide.html', 'l/en.html', 'l/zh-Hans.html']) {
        await page.goto(base + '/' + name);
        assert.equal(await page.locator('h1').count(), 1);
        assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), `Overflow: ${name} ${width}`);
        const primary = page.locator('#release-download a.primary').first();
        const expected = `https://github.com/ChronoHaxx/wuwa-vr/releases/download/${tag}/WuWa-VR-Setup.exe`;
        if (name === 'guide.html') assert.equal(await page.getByRole('link', {name: 'Download WuWa-VR-Setup.exe', exact: true}).getAttribute('href'), expected);
        else assert.equal(await primary.getAttribute('href'), expected);
        assert(await page.locator(`a[href$="/${tag}/WuWa-VR-Launcher.zip"]`).count() > 0);
        assert(await page.locator(`a[href$="/tag/${release.previousTag}"]`).count() > 0);
        assert.equal(await page.locator('.visual-steps img').count(), 0, 'Obsolete launcher screenshots remain');
        assert.equal(await page.locator(name === 'index.html' ? '#get-started ol li' : '#start > ol:first-of-type > li').count(), 3);
        // Caveats can be collapsed for a quieter page; they must remain in the document.
        const text = await page.locator('main').textContent();
        assert(text.includes(release.appVersion) && text.includes(release.game));
        assert(text.includes(name.includes('zh-Hans') ? '默认关闭' : 'off by default'));
        assert(text.includes(name.includes('zh-Hans') ? '预渲染' : 'prerendered'));
        assert(text.includes(name.includes('zh-Hans') ? '底层双眼渲染问题尚未解决' : 'underlying stereo rendering fault is unresolved'));
        for (const href of await page.locator('a[href]').evaluateAll(nodes => nodes.map(node => node.getAttribute('href')))) {
          if (/^(https?:|mailto:)/.test(href)) continue;
          const target = new URL(href, base + '/' + name), file = path.join(site, decodeURIComponent(target.pathname));
          assert(fs.existsSync(file), `Missing link: ${name} ${href}`);
          if (target.hash) assert(fs.readFileSync(file, 'utf8').includes(`id="${decodeURIComponent(target.hash.slice(1))}"`), `Missing anchor: ${name} ${href}`);
        }
        if (width !== 320 && name !== 'l/en.html') await page.screenshot({path: path.join(out, `${name.replaceAll('/', '-').replace('.html', '')}-${width}.png`), fullPage: true});
        checks.push(`${name}: ${width}px layout, installer/portable/previous links, three steps, beta limits, local anchors`);
      }
    }
    for (const name of ['l/ja.html', 'l/ko.html', 'l/es.html', 'l/pt-BR.html', 'l/fr.html', 'l/de.html', 'l/ru.html', 'l/ar.html']) {
      await page.goto(base + '/' + name);
      assert(await page.locator('#launcher-version-note').isVisible());
      assert.equal(await page.locator('#launcher-version-note a').count(), 2);
    }
    checks.push('eight other locale guides retain a visible portable-guide scope note and EN/zh links');
    const plain = await browser.newContext({javaScriptEnabled: false, viewport: {width: 390, height: 844}});
    const nojs = await plain.newPage();
    await nojs.goto(base + '/l/zh-Hans.html');
    assert(await nojs.locator('#release-download a.primary').isVisible());
    assert.equal(await nojs.locator('.launcher-steps li').count(), 3);
    await plain.close();
    checks.push('Chinese installer and three steps usable without JavaScript');
    assert.deepEqual(errors, []);
    fs.writeFileSync(path.join(out, 'receipt.json'), JSON.stringify({at: new Date().toISOString(), checks, errors, scope: 'Local browser only; future release assets were not fetched, installer not run.'}, null, 2));
    console.log(JSON.stringify({passed: checks.length, errors, output: out}));
  } finally { await browser.close(); await new Promise(resolve => server.close(resolve)); }
})().catch(error => { console.error(error); server.close(); process.exitCode = 1; });
