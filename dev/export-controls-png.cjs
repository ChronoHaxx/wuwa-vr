// Original site tooling, MIT. Saves the home page's controller card as one PNG
// per language (site/media/controls/<code>.png) for sharing outside the site.
// Run after dev/build-site.cjs --launcher-release:
//   node dev/export-controls-png.cjs [--review <dir>]
// --review also saves the whole controls section at desktop and phone widths.
const fs = require('node:fs'), path = require('node:path'), http = require('node:http');
const root = path.resolve(__dirname, '..'), site = path.join(root, 'site');
const modules = process.env.WUWA_NODE_MODULES || path.join(process.env.USERPROFILE || process.env.HOME || '', '.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
const {chromium} = require(require.resolve('playwright', {paths: [root, modules]}));
const codes = require('./site-layout.cjs').languages.map(([code]) => code);
const review = process.argv.includes('--review') ? path.resolve(process.argv[process.argv.indexOf('--review') + 1]) : null;
const types = {'.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8', '.svg': 'image/svg+xml', '.woff2': 'font/woff2', '.webp': 'image/webp', '.jpg': 'image/jpeg', '.png': 'image/png'};
const server = http.createServer((req, res) => {
  const file = path.resolve(site, decodeURIComponent(new URL(req.url, 'http://localhost').pathname).slice(1) || 'index.html');
  if (!file.startsWith(site + path.sep) || !fs.existsSync(file) || !fs.statSync(file).isFile()) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, {'Content-Type': types[path.extname(file)] || 'application/octet-stream'}); res.end(fs.readFileSync(file));
});
(async () => {
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${server.address().port}/`;
  const browser = await chromium.launch({headless: true, ...(process.env.WUWA_CHROMIUM ? {executablePath: process.env.WUWA_CHROMIUM} : {})});
  const out = path.join(site, 'media/controls');
  fs.mkdirSync(out, {recursive: true});
  if (review) fs.mkdirSync(review, {recursive: true});
  try {
    for (const code of codes) {
      const page = await browser.newPage({viewport: {width: 1700, height: 1400}, deviceScaleFactor: 1});
      // Video and YouTube are irrelevant here; keep the page offline and still.
      await page.route(/^https?:\/\/(?!127\.0\.0\.1)/, r => r.abort());
      await page.goto(base + (code === 'en' ? 'index.html' : `l/${code}.html`));
      await page.evaluate(() => document.fonts.ready);
      if (review) {
        await page.addStyleTag({content: '.site-header{position:static!important}'});  // no sticky header over long shots
        for (const [name, width] of [['desktop', 1440], ['phone', 390]]) {
          await page.setViewportSize({width, height: 1000});
          await page.locator('#controls').screenshot({path: path.join(review, `${code}-${name}.png`)});
        }
        await page.setViewportSize({width: 1700, height: 1400});
      }
      // Alone on the page, so a right-to-left layout cannot push the fixed-width card off screen.
      await page.evaluate(() => { const card = document.getElementById('pad-card'); card.classList.add('is-export'); document.body.replaceChildren(card); });
      // Fonts, then two frames so the leader lines redraw for the export width.
      await page.evaluate(() => document.fonts.ready.then(() => new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r)))));
      await page.locator('#pad-card').screenshot({path: path.join(out, code + '.png')});
      await page.close();
      console.log(code, fs.statSync(path.join(out, code + '.png')).size, 'bytes');
    }
  } finally { await browser.close(); server.close(); }
})().catch(error => { console.error(error); process.exit(1); });
