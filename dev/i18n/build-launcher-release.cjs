// Scoped release refresh for the published site's guide and reference pages.
// Home pages come from dev/build-home.cjs; verification files stay untouched.
const fs = require('node:fs'), path = require('node:path');
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
function downloads(t) {
  return `<div id="release-download"><p><a class="button primary" href="${asset(status.installer)}">${t.download}</a> <a href="${release}">${t.notes}</a></p><p class="small">${status.appVersion} beta · WuWa ${status.game} · Windows x64</p><p><a href="${asset('WuWa-VR-Launcher.zip')}">${t.fallback}</a> · <a href="${repo}/releases/tag/${status.previousTag}">${t.previous}</a></p></div>`;
}
function steps(t) { return `<p>${t.install}</p><ol class="steps launcher-steps">${t.steps.map(s => `<li>${s}</li>`).join('')}</ol>`; }
function details(t, heading = 'h2') {
  const recovery = t === locales.en ? 'Recovery and runtime details' : '恢复与运行时详情';
  return `<section id="launcher-updates"><${heading}>${t.updatesTitle}</${heading}><p>${t.updates}</p><details><summary>${recovery}</summary><p>${t.updateFallback}</p><p>${t.recovery}</p><p>${t.runtime}</p></details></section><section id="launcher-beta"><${heading}>${t.betaTitle}</${heading}><p>${t.accepted}</p><p>${t.limits}</p><details><summary>${t === locales.en ? 'Rendering and cinematic caveats' : '渲染与过场说明'}</summary><p>${t.rim}</p><p>${t.cutscenes}</p></details></section>`;
}
function sourceAndChecks() {
  const record = JSON.parse(fs.readFileSync(path.join(root, 'release/security-reports.json'), 'utf8'));
  const reports = record.releases.find(r => r.tag === status.tag);
  const rows = reports ? reports.files.filter(f => f.status === 'reported').map(f => {
    if (!/^[a-f0-9]{64}$/.test(f.sha256) || !/^[A-Za-z0-9 ._-]+$/.test(f.name)) throw Error('Invalid scan identity');
    return `<li><a href="https://www.virustotal.com/gui/file/${f.sha256}">${f.name}: VirusTotal report</a></li>`;
  }).join('') : '';
  return `<section id="source-and-checks"><h2>Open-source launcher. Public mod source.</h2><p>The <a href="${repo}/tree/main/launcher/native">desktop launcher is MIT open source</a>. Our <a href="${repo}/tree/main/mod">mod changes, scripts and build instructions</a> are public too. Upstream and community components keep their own licences; <a href="license.html">the combined package has mixed licence terms</a>.</p><p><a href="${release}">Matching source and SHA-256 checksums</a> accompany every release. Check the exact file and version when comparing a download.</p>${rows ? `<ul>${rows}</ul><p class="small">Scan reports describe those exact file hashes. Detections can change; a clean report does not guarantee safety or account compatibility.</p>` : '<p class="small">VirusTotal reports have not yet been verified for this release. No clean-scan or safety certification is claimed.</p>'}</section>`;
}
function banner(text) {
  const value = `<aside class="notice" data-launcher-release><strong>Community beta:</strong> <a href="${release}">WuWa VR ${status.appVersion} · game ${status.game}</a>. ${locales.en.banner}</aside>`;
  return text.replace(/(<main[^>]+id="main"[^>]*>)\s*(?:<aside class="notice"(?: data-launcher-release)?>([\s\S]*?)<\/aside>)?/, (_, main) => main + value);
}
async function build() {
  const candidates = [root, process.env.WUWA_NODE_MODULES,
    path.join(process.env.USERPROFILE || process.env.HOME || '', '.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules')].filter(Boolean);
  const {marked} = await import(require('node:url').pathToFileURL(require.resolve('marked', {paths: candidates})).href);
  const links = {'START-HERE.md': 'guide.html#start', 'RISK.md': 'risk.html',
    'TROUBLESHOOTING.md': 'guide.html#recovery', 'CONTROLS.md': 'guide.html#controls',
    'COMFORT.md': 'guide.html#comfort', 'CHECKPOINTS.md': 'testing.html#checkpoints',
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
  update('guide.html', text => {
    text = banner(text);
    const setup = render('docs/START-HERE.md');
    text = replace(text, /<section id="start">[\s\S]*?<\/section>/, `<section id="start"><h2>Install, launch and update</h2>${setup}</section>`, 'guide setup');
    // Keep the existing accessible search UI while refreshing the actual reference.
    const filter = /<div class="control-filter"[\s\S]*?<p id="control-count"[^>]*><\/p>/.exec(text);
    if (!filter) throw Error('Missing guide controls filter');
    text = replace(text, /<section id="controls">[\s\S]*?<\/section>/,
      `<section id="controls"><h2>Controller shortcuts</h2>${filter[0]}${render('docs/CONTROLS.md', true)}</section>`, 'guide controls');
    text = replace(text, /<section id="recovery">[\s\S]*?<\/section>/,
      `<section id="recovery"><h2>Problems and recovery</h2><p>${locales.en.cutscenes}</p>${render('docs/TROUBLESHOOTING.md')}</section>`, 'guide recovery');
    text = replace(text, /(<h1>Player guide<\/h1>)<p>[\s\S]*?<\/p>/,
      '<h1>Player guide</h1><p>Setup and updates for the installed desktop app. <a href="l/zh-Hans.html">简体中文安装指南</a>. The portable browser launcher remains an advanced fallback.</p>', 'guide intro');
    return replace(text, /<section id="packaging">[\s\S]*?<\/section>/,
      `<section id="packaging"><h2>Portable fallback</h2><p>${locales.en.portable}</p>${downloads(locales.en)}</section>`, 'portable section');
  });
  update('risk.html', text => replace(text, /<div class="guide-content">[\s\S]*?<\/div>/,
    `<div class="guide-content">${render('docs/RISK.md')}</div>`, 'risk notice'));
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
    `<section id="checkpoints"><h2>Checkpoint identities</h2><p><a href="${release}">6 Oct startup compatibility beta ${status.appVersion} (game ${status.game}): files, source and checksums</a>, VR build <strong>${status.buildId}</strong>. ${locales.en.accepted}</p><p>The report does not establish Windows 11 as the cause. The retained cinematic correction was accepted in a simulator replay with matching letterbox heights; headset comfort remains pending. Automatic cinema stays off and unverified. See the release receipt for build/package checks. <a href="${repo}/releases/tag/${status.previousTag}">Previous 6 Oct beta</a> and <a href="${repo}/releases/tag/beta-2026-09-26-230213">26 Sep beta</a> remain historical checkpoints. Keep the build name, timestamp and backend hash with a report.</p></section>`, 'test checkpoints'));
  // Both full generation and this scoped refresh end here; keep one metadata policy.
  // The home builder owns index.html and l/*.html, and applies the shared chrome and metadata.
  require('../build-home.cjs');
  console.log('Refreshed the guide and release references; rebuilt the home pages in ten languages.');
}
module.exports = {build};
