// Original site tooling, MIT. The website is the one place player docs are written:
//   * the player guide in ten languages, from dev/i18n/guide/<code>.json
//     -> site/guide.html (English) and site/l/<code>-guide.html;
//   * the English reference (every shortcut with a search, comfort, known issues),
//     from dev/site-content/{controls,comfort,troubleshooting}.md -> site/reference.html.
// Shortcut tables come from mod/controls.json in the VR mod's own words
// (dev/controls-section.cjs), like the home pages and the in-VR cards.
//
//   node dev/build-guide.cjs           write the pages (dev/build-home.cjs runs it too)
//   node dev/build-guide.cjs --check   exit 1 if a translation is incomplete
// dev/check-site-sync.cjs checks that the written pages match their sources.
const fs = require('node:fs'), path = require('node:path');
const L = require('./site-layout.cjs'), pad = require('./controls-section.cjs');
const root = path.resolve(__dirname, '..'), site = path.join(root, 'site');
const SOURCES = path.join(__dirname, 'i18n/guide'), HOME = path.join(__dirname, 'i18n/home');
const CONTENT = path.join(__dirname, 'site-content');
const esc = L.escape, codes = L.languages.map(([code]) => code);
const TAG = /<(\/?)(strong|code|em|a)(\s[^>]*)?>/g, HREF = /href="([^"]*)"/g, FIELD = /\{(version|tag|game)\}/g;
const base = 'https://chronohaxx.github.io/wuwa-vr/';
const arrow = '<svg class="icon arrow" viewBox="0 0 24 24" width="18" height="18" aria-hidden="true"><path d="M5 12h13m0 0-5-5m5 5-5 5"/></svg>';

const load = code => {
  const file = path.join(SOURCES, code + '.json');
  return fs.existsSync(file) ? JSON.parse(fs.readFileSync(file, 'utf8')) : null;
};
const sorted = (text, re) => [...text.matchAll(re)].map(m => m[re === TAG ? 2 : 1]).sort().join(' ');

// The same rules as the other translated sources: every key, same fields, tags and links.
function problems() {
  const english = load('en').strings, out = [];
  for (const code of codes.slice(1)) {
    const data = load(code);
    if (!data) { out.push(`${code}: dev/i18n/guide/${code}.json is missing`); continue; }
    const strings = data.strings || {};
    for (const [key, source] of Object.entries(english)) {
      const value = strings[key];
      if (typeof value !== 'string' || !value.trim()) { out.push(`${code}.${key}: missing`); continue; }
      if (sorted(source, FIELD) !== sorted(value, FIELD)) out.push(`${code}.${key}: {version} / {tag} / {game} differ from English`);
      if (sorted(source, TAG) !== sorted(value, TAG)) out.push(`${code}.${key}: markup differs from English`);
      if (sorted(source, HREF) !== sorted(value, HREF)) out.push(`${code}.${key}: links differ from English`);
      if (/<(?!\/?(strong|code|em|a)[\s>])/.test(value)) out.push(`${code}.${key}: only <strong>, <code>, <em> and <a> are allowed`);
    }
    for (const key of Object.keys(strings)) if (!(key in english)) out.push(`${code}.${key}: not in English (stale)`);
  }
  return out;
}

const guideFile = code => code === 'en' ? 'guide.html' : `l/${code}-guide.html`;
// Links to this website become relative, so a local copy checks them and nothing leaves the site.
const local = (html, prefix) => html.replace(/href="https:\/\/chronohaxx\.github\.io\/wuwa-vr\/([^"]*)"/g,
  (_, rest) => `href="${prefix}${rest || 'index.html'}"`);
function webpSize(file) {
  const b = fs.readFileSync(path.join(site, file)), kind = b.toString('ascii', 12, 16);
  if (kind === 'VP8X') return {width: 1 + b.readUIntLE(24, 3), height: 1 + b.readUIntLE(27, 3)};
  if (kind === 'VP8 ') return {width: b.readUInt16LE(26) & 0x3fff, height: b.readUInt16LE(28) & 0x3fff};
  if (kind === 'VP8L') { const bits = b.readUInt32LE(21); return {width: 1 + (bits & 0x3fff), height: 1 + ((bits >> 14) & 0x3fff)}; }
  throw Error('Unknown WebP: ' + file);
}

function guidePage(code) {
  const data = load(code), english = load('en').strings, home = JSON.parse(fs.readFileSync(path.join(HOME, code + '.json'), 'utf8'));
  const status = L.status(), values = {version: status.appVersion, tag: status.tag, game: status.game};
  const prefix = code === 'en' ? '' : '../', dir = L.rtl(code) ? 'rtl' : 'ltr';
  const t = key => local((data.strings[key] || english[key]).replace(FIELD, (_, k) => values[k]), prefix);
  const plain = key => t(key).replace(/<[^>]*>/g, '');
  const chrome = home.chrome;
  const shot = `media/launcher/${code}-ready.webp`, size = webpSize(shot);
  const sections = [['start', t('start.title')], ['controls', t('controls.title')], ['updates', t('updates.title')],
    ['recovery', t('recovery.title')], ['recording', t('recording.title')]];
  const body = `<header class="page-hero"><p class="eyebrow"><span class="pill">${esc(chrome.guide)}</span><span>${t('subtitle')}</span></p>
<h1>${t('title')}</h1><p class="lede">${t('intro')}</p>
<nav class="chips" aria-label="${esc(plain('nav.label'))}">${sections.map(([id, title]) => `<a href="#${id}">${title}</a>`).join('')}</nav></header>
<aside class="notice risk"><p>${t('notice')}</p></aside>
<section id="start" class="doc-section"><h2>${t('start.title')}</h2>
<div class="doc-split"><div><p>${t('screen.body')}</p><ul class="doc-list"><li>${t('screen.game')}</li><li>${t('screen.mod')}</li><li>${t('screen.headset')}</li></ul><p class="small">${t('screen.parts')}</p></div>
<figure class="doc-shot"><a href="${prefix}${shot}"><img src="${prefix}${shot}" width="${size.width}" height="${size.height}" loading="lazy" decoding="async" alt="${esc(home.start.shots[2])}"></a></figure></div>
<p>${t('launch.steps')}</p><p>${t('launch.close')}</p><p>${t('launch.flat')}</p></section>
<section id="controls" class="doc-section"><h2>${t('controls.title')}</h2><p>${t('controls.intro')}</p>
${pad.block({code, dir, copy: home.pad, prefix})}
<p>${t('controls.keyboard')}</p><p id="playstation">${t('controls.playstation')}</p><p>${t('controls.views')}</p>
<details><summary>${t('optional.summary')}</summary><p>${t('optional.walk')}</p><p>${t('optional.share')}</p><p>${t('optional.diorama')}</p></details></section>
<section id="updates" class="doc-section"><h2>${t('updates.title')}</h2><p>${t('updates.check')}</p><p>${t('updates.release')}</p><p>${t('updates.blocked')}</p>
<details><summary>${t('portable.summary')}</summary><p>${t('portable.body')}</p></details></section>
<section id="recovery" class="doc-section"><h2>${t('recovery.title')}</h2><p>${t('recovery.fixes')}</p><p>${t('recovery.more')}</p>
<ul class="doc-list">${['stuck', 'close', 'controller', 'hud', 'menu', 'report'].map(k => `<li>${t('recovery.' + k)}</li>`).join('')}</ul>
<details><summary>${t('uninstall.summary')}</summary><p>${t('uninstall.body')}</p></details></section>
<section id="recording" class="doc-section"><h2>${t('recording.title')}</h2><p>${t('recording.body')}</p><p>${t('recording.limits')}</p></section>
<p class="section-foot"><a class="card-link" href="${prefix}reference.html" hreflang="en"><strong>${t('reference.link')}</strong>${arrow}</a></p>`;
  const languageHref = other => other === 'en' ? `${prefix}guide.html` : `${prefix}l/${other}-guide.html`;
  const links = [[code === 'en' ? 'index.html' : `${prefix}l/${code}.html`, chrome.home], [languageHref(code), chrome.guide],
    [`${prefix}record.html`, chrome.capture], [`${prefix}feedback.html`, chrome.feedback]];
  const head = L.head({lang: code, title: `${plain('title')} — WuWa VR`, description: plain('intro'), prefix});
  return `<!doctype html><html lang="${code}" dir="${dir}">${head}<body><a class="skip" href="#main">${esc(chrome.skip)}</a>` +
    `${L.header({lang: code, prefix, labels: chrome, links, languageHref})}<main class="wrap page doc-page" id="main" data-guide="${code}">${body}</main>` +
    `${L.footer({prefix, labels: chrome})}</body></html>\n`;
}

// English reference: Markdown rendered with GitHub-style heading ids (first use wins).
async function referencePage() {
  const candidates = [root, process.env.WUWA_NODE_MODULES,
    path.join(process.env.USERPROFILE || process.env.HOME || '', '.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules')].filter(Boolean);
  const {marked} = await import(require('node:url').pathToFileURL(require.resolve('marked', {paths: candidates})).href);
  const links = {'START-HERE.md': 'guide.html#start', 'controls.md': 'reference.html#controls', 'CONTROLS.md': 'reference.html#controls',
    'comfort.md': 'reference.html#comfort', 'COMFORT.md': 'reference.html#comfort', 'troubleshooting.md': 'reference.html#recovery',
    'TROUBLESHOOTING.md': 'reference.html#recovery', 'risk.md': 'risk.html', 'RISK.md': 'risk.html', 'CHECKPOINTS.md': 'testing.html#checkpoints',
    'LICENSE.md': 'license.html', 'SUPPORT.md': 'support.html'};
  const used = new Set(['main', 'visual-controls', 'controls', 'comfort', 'recovery', 'control-search', 'control-count', 'clear-search', 'pad-card']);
  const slug = text => {
    let id = text.replace(/<[^>]*>/g, '').replace(/&[a-z#0-9]+;/gi, '').toLowerCase().trim().replace(/[^\w\- ]+/g, '').replace(/\s/g, '-');
    for (let n = 1, plain = id; used.has(id); n++) id = `${plain}-${n}`;
    used.add(id); return id;
  };
  function render(file, controls = false) {
    let html = marked.parse(fs.readFileSync(path.join(CONTENT, file), 'utf8').replace(/^# .+\r?\n/, ''), {gfm: true});
    html = html.replace(/href="([^"#]+\.md)(#[^"]*)?"/g, (match, target, anchor) => {
      if (/^https?:/.test(target)) return match;
      const page = links[path.basename(target)]; if (!page) throw Error(`Unmapped link in ${file}: ${target}`);
      return `href="${page}${anchor && !page.includes('#') ? anchor : ''}"`;
    }).replace(/<h2>([\s\S]*?)<\/h2>/g, (_, title) => `<h3 id="${slug(title)}">${title}</h3>`)
      .replace(/<h3>([\s\S]*?)<\/h3>/g, (_, title) => `<h4 id="${slug(title)}">${title}</h4>`)
      .replace(/<table>/g, '<div class="table-scroll"><table>').replace(/<\/table>/g, '</table></div>');
    if (controls) {
      let context = '';
      html = html.replace(/<(h[3-6])(?:\s[^>]*)?>([\s\S]*?)<\/\1>|<tbody>([\s\S]*?)<\/tbody>/g, (match, heading, text, rows) => {
        if (heading) { context = text.replace(/<[^>]*>/g, '').replace(/"/g, '&quot;'); return match; }
        return '<tbody>' + rows.replace(/<tr>/g, `<tr data-control-row data-context="${context}">`) + '</tbody>';
      });
    }
    return local(html, '');
  }
  const copy = JSON.parse(fs.readFileSync(path.join(HOME, 'en.json'), 'utf8')).pad;
  const filter = '<div class="control-filter" hidden><label for="control-search">FIND A SHORTCUT</label><input id="control-search" type="search" placeholder="HUD, camera, acro, L3…" autocomplete="off"><button id="clear-search" class="reset-button" type="button">Clear</button></div><p id="control-count" role="status" aria-live="polite"></p>';
  const chapters = [['visual-controls', 'Controller images'], ['controls', 'All shortcuts'], ['comfort', 'HUD and camera comfort'], ['recovery', 'Known issues and recovery']];
  const body = `<header class="page-hero"><p class="eyebrow"><span class="pill">Reference</span><span>English</span></p><h1>Reference</h1>
<p class="lede">Every shortcut with a search, the comfort settings and known issues, in more depth than the <a href="guide.html">player guide</a>, which is in ten languages.</p>
<nav class="chips" aria-label="Reference sections">${chapters.map(([id, title]) => `<a href="#${id}">${title}</a>`).join('')}</nav></header>
<div class="guide-content">${pad.renderGuide({copy})}
<section id="controls"><h2>All shortcuts</h2>${filter}${render('controls.md', true)}</section>
<section id="comfort"><h2>HUD and camera comfort</h2>${render('comfort.md')}</section>
<section id="recovery"><h2>Known issues and recovery</h2>${render('troubleshooting.md')}</section></div>`;
  return L.shell('Reference', 'Every WuWa VR shortcut with a search, HUD and camera comfort settings, and known issues with what to do.', body);
}

async function build() {
  fs.mkdirSync(path.join(site, 'l'), {recursive: true});
  for (const code of codes) fs.writeFileSync(path.join(site, guideFile(code)), guidePage(code));
  fs.writeFileSync(path.join(site, 'reference.html'), await referencePage());
  return codes.length + 1;
}

module.exports = {build, problems, guideFile, pages: [...codes.map(guideFile), 'reference.html']};
if (require.main === module) {
  if (process.argv.includes('--check')) {
    const found = problems();
    console.log(found.length ? found.join('\n') : `OK: player guide, ${codes.length} languages, ${Object.keys(load('en').strings).length} parts each`);
    process.exitCode = found.length ? 1 : 0;
  } else build().then(n => console.log(`Built the player guide in ${codes.length} languages and the English reference (${n} pages).`))
    .catch(e => { console.error(e); process.exitCode = 1; });
}
