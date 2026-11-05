// Scoped release refresh for the published site's reference pages (risk, licence, credits,
// explainer, test notes). The player guide and reference come from dev/build-guide.cjs.
// Home pages come from dev/build-home.cjs; verification files stay untouched.
const fs = require('node:fs'), path = require('node:path'), controlsSection = require('../controls-section.cjs');
const root = path.resolve(__dirname, '../..'), site = path.join(root, 'site');
const status = JSON.parse(fs.readFileSync(path.join(root, 'dev/site-status.json'), 'utf8'));
const repo = 'https://github.com/ChronoHaxx/wuwa-vr';
const release = `${repo}/releases/tag/${status.tag}`;
const asset = name => `${repo}/releases/download/${status.tag}/${name}`;
const locales = Object.fromEntries(['en', 'zh-Hans'].map(code => [code,
  JSON.parse(fs.readFileSync(path.join(__dirname, `launcher-release.${code}.json`), 'utf8'))]));
function replace(text, expression, value, label) {
  if (!expression.test(text)) throw Error(`Missing release target: ${label}`);
  return text.replace(expression, () => value);
}
function banner(text) {
  const value = `<aside class="notice" data-launcher-release><strong>Community beta:</strong> <a href="${release}">WuWa VR ${status.appVersion} · game ${status.game}</a>. ${locales.en.banner}</aside>`;
  return text.replace(/(<main[^>]+id="main"[^>]*>)\s*(?:<aside class="notice"(?: data-launcher-release)?>([\s\S]*?)<\/aside>)?/, (_, main) => main + value);
}
async function build() {
  const candidates = [root, process.env.WUWA_NODE_MODULES,
    path.join(process.env.USERPROFILE || process.env.HOME || '', '.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules')].filter(Boolean);
  const {marked} = await import(require('node:url').pathToFileURL(require.resolve('marked', {paths: candidates})).href);
  const links = {'START-HERE.md': 'guide.html#start', 'RISK.md': 'risk.html', 'risk.md': 'risk.html',
    'TROUBLESHOOTING.md': 'reference.html#recovery', 'CONTROLS.md': 'reference.html#controls',
    'COMFORT.md': 'reference.html#comfort', 'CHECKPOINTS.md': 'testing.html#checkpoints',
    'LICENSE.md': 'license.html', 'SUPPORT.md': 'support.html'};
  function render(file, controls = false) {
    let html = marked.parse(fs.readFileSync(path.join(root, file), 'utf8').replace(/^# .+\r?\n/, ''), {gfm: true})
      .replaceAll('href="../launcher/native/PlayerGuide.html"', 'href="guide.html#start"');
    html = html.replace(/href="([^"#]+\.md)(#[^"]*)?"/g, (match, target, anchor) => {
      if (/^https?:/.test(target)) return match;
      const page = links[path.basename(target)]; if (!page) throw Error(`Unmapped guide link in ${file}: ${target}`);
      return `href="${page}${anchor && !page.includes('#') ? anchor : ''}"`;
    }).replace(/<(\/?)h2>/g, '<$1h3>')
      .replace(/<table>/g, '<div class="table-scroll"><table>').replace(/<\/table>/g, '</table></div>');
    if (controls) {
      let context = '';
      html = html.replace(/<(h[3-6])(?:\s[^>]*)?>([\s\S]*?)<\/\1>|<tbody>([\s\S]*?)<\/tbody>/g,
        (match, heading, text, body) => {
          // Markdown already escapes entities; preserve them in this attribute.
          if (heading) { context = text.replace(/<[^>]*>/g, '').replace(/"/g, '&quot;'); return match; }
          return '<tbody>' + body.replace(/<tr>/g, `<tr data-control-row data-context="${context}">`) + '</tbody>';
        });
    }
    return html;
  }
  const update = (name, transform) => {
    const file = path.join(site, name), old = fs.readFileSync(file, 'utf8'), next = transform(old);
    if (old !== next) fs.writeFileSync(file, next);
  };
  update('risk.html', text => replace(text, /<div class="guide-content">[\s\S]*?<\/div>/,
    `<div class="guide-content">${render('dev/site-content/risk.md')}</div>`, 'risk notice'));
  update('license.html', text => {
    text = text.replace(/<section id="launcher-license">[\s\S]*?<\/section>/, '');
    return text.replace('<section><h2>What our MIT grant covers</h2>', `<section id="launcher-license"><h2>Open-source desktop launcher</h2><p>The <a href="${repo}/tree/main/launcher/native">desktop manager source</a> is MIT open source under <a href="${repo}/blob/main/launcher/native/LICENSE.txt">its own licence</a>. The mod source changes are public, with <a href="${repo}/blob/main/mod/BUILD.md">pinned build instructions</a>. Downloaded mod components keep their own terms; public source availability does not make the combined package uniformly open source.</p></section><section><h2>What our MIT grant covers</h2>`);
  });
  // Refresh current-download references without rewriting historical technical notes.
  update('developers.html', banner);
  update('understanding.html', text => {
    let body = render('docs/UNDERSTANDING-WUWA-VR.md');
    const headings = [];
    body = body.replace(/<h3>(.*?)<\/h3>/g, (_, title) => {
      const id = title.toLowerCase().replace(/<[^>]*>/g, '').replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '');
      headings.push([id, title]); return `<h2 id="${id}">${title}</h2>`;
    });
    text = replace(text, /<article class="guide-content">[\s\S]*?<\/article>/, `<article class="guide-content">${body}</article>`, 'explainer article');
    text = replace(text, /<details open><summary>Jump to a section<\/summary>[\s\S]*?<\/details>/, `<details open><summary>Jump to a section</summary><ul>${headings.map(([id, title]) => `<li><a href="#${id}">${title}</a></li>`).join('')}</ul></details>`, 'explainer contents');
    text = text.replace('60 seconds · silent, with visible captions', 'Archive explainer · 28 September 2026 · silent, with visible captions');
    return replace(text,
    /<a class="button primary" href="https:\/\/github\.com\/ChronoHaxx\/wuwa-vr\/releases\/(?:tag|download)\/[^\"]+">(?:Beta download|Download beta installer)[\s\S]*?<\/a>/,
    `<a class="button primary" href="${asset(status.installer)}">Download beta installer · ${status.appVersion} · game ${status.game}</a>`, 'explainer download');
  });
  update('credits.html', text => replace(text, /<section id="distribution">[\s\S]*?<\/section>/,
    `<section id="distribution"><h2>Distribution status</h2><p><a href="${release}">The current public beta</a> provides <a href="${asset(status.installer)}">WuWa-VR-Setup.exe</a> for desktop app ${status.appVersion}, plus the advanced <a href="${asset('WuWa-VR-Launcher.zip')}">portable ZIP</a>, matching source and checksums. The thin installer downloads the separate VR mod on first installation.</p><p>VR build <strong>${status.buildId}</strong> targets game ${status.game}. ${locales.en.accepted}</p><p>${locales.en.limits}</p><p>${locales.en.cutscenes}</p><p>The optional NPC rim workaround remains off by default and also removes intended nearby rim lighting; the underlying stereo fault remains unresolved.</p><p>Keep native source, profiles, component notices and hashes paired. Publication does not change upstream licence terms or imply endorsement. Preserve older releases for rollback and keep this experimental release labelled beta.</p></section>`, 'distribution release'));
  update('testing.html', text => replace(text, /<section id="checkpoints">[\s\S]*?<\/section>/,
    `<section id="checkpoints"><h2>Checkpoint identities</h2><p><a href="${release}">Current beta ${status.appVersion} (game ${status.game}): files, source and checksums</a>, VR build <strong>${status.buildId}</strong>. ${locales.en.accepted}</p><p>The retained cinematic correction was accepted in a simulator replay with matching letterbox heights; headset comfort remains pending. Automatic cinema stays off and unverified. See the release receipt for build/package checks. <a href="${repo}/releases/tag/${status.previousTag}">Previous beta</a> and <a href="${repo}/releases/tag/beta-2026-09-26-230213">26 Sep beta</a> remain historical checkpoints. Keep the build name, timestamp and backend hash with a report.</p></section>`, 'test checkpoints'));
  // Both full generation and this scoped refresh end here; keep one metadata policy.
  // The home builder owns index.html and l/*.html, then builds the guide and reference,
  // applies the shared chrome and metadata, and writes the README and docs pointers.
  await require('../build-home.cjs').ready;
  console.log('Refreshed the release references; rebuilt the home pages, guide and reference.');
}
module.exports = {build};
