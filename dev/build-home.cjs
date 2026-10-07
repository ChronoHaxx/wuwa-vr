// Original site tooling, MIT. Builds the home page in ten languages, the
// language index and the capture guide from dev/i18n/home/<code>.json, then
// refreshes the shared header/footer and metadata on the other public pages.
// Node standard library only: node dev/build-home.cjs
const fs = require('node:fs'), path = require('node:path'), assert = require('node:assert/strict');
const L = require('./site-layout.cjs'), {report} = require('./site-report.cjs');
const esc = L.escape, root = path.resolve(__dirname, '..'), site = path.join(root, 'site');
const status = L.status(), codes = L.languages.map(([code]) => code);
const youtube = {id: 'N3yqYROPPww', watch: 'https://www.youtube.com/watch?v=N3yqYROPPww', length: '1:20'};
const releaseUrl = `${L.repo}/releases/tag/${status.tag}`, asset = name => `${L.repo}/releases/download/${status.tag}/${name}`;
const baseUrl = 'https://chronohaxx.github.io/wuwa-vr/';
const fill = (text, values = {}) => text.replace(/\{(\w+)\}/g, (match, key) => key in values ? values[key] : match);
const vars = {app: status.appVersion, game: status.game, previous: status.previousTag.replace(/^beta-/, '').replace(/-/g, '.')};
const keys = ['L3 + R3', 'L3 + B', 'L3 + A', 'L3 + Menu', 'L3 + LB', 'L3 + View', 'Double R3', 'L3 + RB'];
const shotStates = ['fresh', 'found', 'ready'];

// Every locale must have exactly the English shape, as text (never markup).
function load(code) {
  const home = JSON.parse(fs.readFileSync(path.join(__dirname, 'i18n/home', code + '.json'), 'utf8'));
  const strings = require(path.join(site, 'languages', code + '.js'));
  return {code, dir: L.rtl(code) ? 'rtl' : 'ltr', name: L.languages.find(([c]) => c === code)[1], ...home, strings};
}
function shape(value, reference, where) {
  if (Array.isArray(reference)) {
    assert(Array.isArray(value) && value.length === reference.length, 'Wrong list length: ' + where);
    reference.forEach((item, i) => shape(value[i], item, `${where}[${i}]`));
  } else if (reference && typeof reference === 'object') {
    assert.deepEqual(Object.keys(value || {}).sort(), Object.keys(reference).sort(), 'Wrong keys: ' + where);
    for (const key of Object.keys(reference)) shape(value[key], reference[key], `${where}.${key}`);
  } else {
    assert.equal(typeof value, 'string', 'Not text: ' + where);
    assert(!/[<>]/.test(value), 'Markup in translation: ' + where);
  }
}
const locales = codes.map(load), en = locales[0];
const copy = ({strings, code, dir, name, ...text}) => text;
for (const t of locales) shape(copy(t), copy(en), t.code);

const icon = {
  download: '<svg class="icon" viewBox="0 0 24 24" width="20" height="20" aria-hidden="true"><path d="M12 4v11m0 0-4.5-4.5M12 15l4.5-4.5M5 19h14"/></svg>',
  play: '<svg class="icon" viewBox="0 0 24 24" width="20" height="20" aria-hidden="true"><path class="fill" d="M8 5.5v13l10.5-6.5z"/></svg>',
  pause: '<svg class="icon" viewBox="0 0 24 24" width="18" height="18" aria-hidden="true"><path class="fill" d="M7 5h3.5v14H7zM13.5 5H17v14h-3.5z"/></svg>',
  arrow: '<svg class="icon arrow" viewBox="0 0 24 24" width="18" height="18" aria-hidden="true"><path d="M5 12h13m0 0-5-5m5 5-5 5"/></svg>',
  check: '<svg class="icon" viewBox="0 0 24 24" width="18" height="18" aria-hidden="true"><path d="M5 12.5 10 17 19 7.5"/></svg>',
  alert: '<svg class="icon" viewBox="0 0 24 24" width="20" height="20" aria-hidden="true"><path d="M12 4 2.8 19.5h18.4zM12 10v4.5m0 2.7v.1"/></svg>'
};
// Simple line icons for the six feature cards, in order.
const featureIcons = [
  '<path d="M3 9.5C3 8 4.2 7 5.7 7h12.6C19.8 7 21 8 21 9.5v4.6c0 1.6-1.2 2.9-2.8 2.9h-2.1c-1 0-1.8-.6-2.2-1.5l-.4-.9c-.4-.9-2.6-.9-3 0l-.4.9c-.4.9-1.2 1.5-2.2 1.5H5.8C4.2 17 3 15.7 3 14.1z"/>',
  '<path d="M7.5 8h9a4.5 4.5 0 0 1 4.4 5.4l-.8 3.6a2 2 0 0 1-3.4 1L15 16H9l-1.7 2a2 2 0 0 1-3.4-1l-.8-3.6A4.5 4.5 0 0 1 7.5 8zM8 11v3M6.5 12.5h3M15.5 11.5h.1M17.5 13.5h.1"/>',
  '<circle cx="12" cy="6" r="2.6"/><path d="M12 9.5v6m0 0-3.5 5m3.5-5 3.5 5M6.5 11.5 12 10l5.5 1.5"/>',
  '<path d="M4 8.5h3l1.5-2.5h7L17 8.5h3v10H4z"/><circle cx="12" cy="13.2" r="3.3"/>',
  '<path d="M12 3 20 7.5v9L12 21l-8-4.5v-9zM12 12l8-4.5M12 12v9M12 12 4 7.5"/>',
  '<path d="M2.5 12S6 6 12 6s9.5 6 9.5 6-3.5 6-9.5 6-9.5-6-9.5-6z"/><circle cx="12" cy="12" r="2.8"/>'
];
const svg = paths => `<svg class="icon" viewBox="0 0 24 24" width="26" height="26" aria-hidden="true">${paths}</svg>`;
const kbd = combo => `<bdi dir="ltr">${combo.split(' + ').map(k => `<kbd>${esc(k)}</kbd>`).join(' + ')}</bdi>`;
const imageSize = file => { const b = fs.readFileSync(path.join(site, file)); assert.equal(b.toString('ascii', 0, 4), 'RIFF', file);
  // VP8L/VP8/VP8X headers; ffmpeg's libwebp output is VP8 or VP8X.
  const kind = b.toString('ascii', 12, 16);
  if (kind === 'VP8X') return {width: 1 + b.readUIntLE(24, 3), height: 1 + b.readUIntLE(27, 3)};
  if (kind === 'VP8 ') return {width: b.readUInt16LE(26) & 0x3fff, height: b.readUInt16LE(28) & 0x3fff};
  if (kind === 'VP8L') { const bits = b.readUInt32LE(21); return {width: 1 + (bits & 0x3fff), height: 1 + ((bits >> 14) & 0x3fff)}; }
  throw Error('Unknown WebP: ' + file); };

function alternates() {
  return codes.map(code => `<link rel="alternate" hreflang="${code}" href="${code === 'en' ? baseUrl : `${baseUrl}l/${code}.html`}">`).join('') +
    `<link rel="alternate" hreflang="x-default" href="${baseUrl}">`;
}
function home(t, prefix) {
  const s = t.strings, english = t.english, toEnglish = label => esc(label) + (english ? `<span class="en-note">${esc(english)}</span>` : '');
  const languageHref = code => code === 'en' ? `${prefix}index.html` : `${prefix}l/${code}.html`;
  const labels = t.chrome;
  const links = [['#start', labels.install], ['#features', labels.features], ['#faq', labels.faq], [prefix + 'guide.html', labels.guide], [prefix + 'record.html', labels.capture]];
  const hero = `<section class="hero"><div class="wrap hero-grid"><div class="hero-copy">
<p class="eyebrow"><span class="pill">${esc(t.hero.eyebrow)}</span><span>${esc(status.appVersion)} · Wuthering Waves ${esc(status.game)}</span></p>
<h1>${esc(t.hero.title)}</h1><p class="lede">${esc(t.hero.lede)}</p>
<div id="release-download"><div class="cta-row"><a class="button primary big" href="${asset(status.installer)}">${icon.download}<span>${esc(t.hero.download)}</span></a></div>
<p class="cta-meta">${esc(fill(t.hero.meta, vars))}</p>
<p class="cta-links"><a href="${asset('WuWa-VR-Launcher.zip')}">${esc(t.hero.portable)}</a><a href="${releaseUrl}">${esc(t.hero.notes)}</a><a href="${L.repo}/releases/tag/${status.previousTag}">${esc(fill(t.hero.previous, vars))}</a></p></div></div>
<figure class="hero-media"><div class="video-frame" id="run-video" data-youtube="${youtube.id}" data-youtube-title="${esc(t.hero.watch)}">
<video muted loop playsinline preload="none" poster="${prefix}media/run-poster.jpg" width="960" height="540" aria-label="${esc(t.hero.caption)}"><source src="${prefix}media/run-loop.mp4" type="video/mp4"></video>
<button class="video-toggle" type="button" hidden data-play="${esc(t.hero.play)}" data-pause="${esc(t.hero.pause)}" aria-label="${esc(t.hero.play)}">${icon.play}${icon.pause}</button>
<a class="video-play" href="${youtube.watch}" data-youtube-open rel="noopener noreferrer">${icon.play}<span>${esc(t.hero.watch)}</span><span class="button-meta">${esc(t.hero.watchMeta)}</span></a></div>
<figcaption>${esc(t.hero.caption)} <span class="small">${esc(t.hero.youtube)}</span></figcaption></figure></div></section>`;
  const risk = `<div class="wrap"><aside class="notice risk">${icon.alert}<p><strong>${esc(s.riskTitle)}</strong> ${esc(s.riskText)} <a href="${prefix}risk.html" hreflang="en">${toEnglish(t.risk.link)}</a></p></aside></div>`;
  const shots = shotStates.map((state, i) => {
    const file = `media/launcher/${t.code}-${state}.webp`, size = imageSize(file);
    return `<figure class="tour-shot${i === 0 ? ' is-active' : ''}" data-shot="${i}"><a href="${prefix}${file}"><img src="${prefix}${file}" width="${size.width}" height="${size.height}" loading="lazy" decoding="async" alt="${esc(t.start.shots[i])}"></a></figure>`;
  }).join('');
  const start = `<section id="start" class="section"><div class="wrap"><div class="section-head"><h2>${esc(t.start.title)}</h2><p class="lede">${esc(t.start.lede)}</p></div>
<div class="tour"><ol class="launcher-steps">${t.start.steps.map((text, i) => `<li class="tour-step${i === 0 ? ' is-active' : ''}"><span class="step-num" aria-hidden="true">0${i + 1}</span><div><h3 id="step-${i + 1}">${esc(t.start.names[i])}</h3><p>${esc(text)}</p></div><button class="tour-select" type="button" hidden data-shot="${i}" aria-labelledby="step-${i + 1}" aria-pressed="${i === 0}"></button></li>`).join('')}</ol>
<div class="tour-shots">${shots}<p class="small tour-caption">${esc(t.start.caption)}</p></div></div>
<p class="section-foot">${esc(t.start.controls)} <a href="${prefix}guide.html#start" hreflang="en">${toEnglish(t.start.guide)}</a></p></div></section>`;
  const features = `<section id="features" class="section alt"><div class="wrap"><div class="section-head"><h2>${esc(t.features.title)}</h2></div><ul class="feature-grid">${t.features.items.map(([title, text], i) => `<li class="feature">${svg(featureIcons[i])}<h3>${esc(title)}</h3><p>${esc(text)}</p></li>`).join('')}</ul></div></section>`;
  const firstPerson = `<section id="first-person" class="section"><div class="wrap split"><figure class="clip"><video controls preload="none" playsinline poster="${prefix}media/feature-first-person.jpg" width="960" height="720"><source src="${prefix}media/feature-first-person.mp4" type="video/mp4"><a href="${prefix}media/feature-first-person.mp4">${esc(t.firstPerson.title)}</a></video><figcaption>${esc(t.firstPerson.caption)}</figcaption></figure>
<div class="split-copy"><h2>${esc(t.firstPerson.title)}</h2><p>${esc(t.firstPerson.text)}</p><a class="card-link" href="${prefix}record.html" hreflang="en"><strong>${toEnglish(t.firstPerson.cta)}</strong><span>${esc(t.firstPerson.ctaText)}</span>${icon.arrow}</a></div></div></section>`;
  const controls = `<section id="controls" class="section alt"><div class="wrap"><div class="section-head"><h2>${esc(s.controls)}</h2><p>${esc(s.keyIntro)}</p></div><div class="table-scroll"><table class="shortcut-table"><tbody>${keys.map((key, i) => `<tr><th scope="row">${kbd(key)}</th><td>${esc(s.keyActions[i])}</td></tr>`).join('')}</tbody></table></div>
<p class="section-foot"><a href="${prefix}guide.html#controls" hreflang="en">${esc(s.detailed)}</a></p></div></section>`;
  const faq = `<section id="faq" class="section"><div class="wrap narrow"><div class="section-head"><h2>${esc(t.faq.title)}</h2></div><div class="faq">${t.faq.items.map(([q, a]) => `<details><summary>${esc(q)}</summary><p>${esc(a)}</p></details>`).join('')}</div></div></section>`;
  const limits = `<section id="limits" class="section alt"><div class="wrap narrow"><div class="section-head"><h2>${esc(t.limits.title)}</h2></div><ul class="limit-list">${t.limits.items.map(item => `<li>${esc(item)}</li>`).join('')}</ul><p class="section-foot"><a href="${prefix}guide.html#recovery" hreflang="en">${toEnglish(t.limits.recovery)}</a></p></div></section>`;
  const open = `<section id="open" class="section"><div class="wrap narrow"><div class="section-head"><h2>${esc(t.open.title)}</h2><p>${esc(t.open.text)}</p></div><div class="link-row"><a class="button" href="${L.repo}/tree/main/launcher/native">${esc(t.open.launcher)}</a><a class="button" href="${L.repo}/tree/main/mod">${esc(t.open.mod)}</a><a class="button" href="${releaseUrl}">${esc(t.open.release)}</a></div><p class="small">${esc(scanNote(t))}</p></div></section>`;
  const feedback = `<section id="feedback" class="section alt"><div class="wrap narrow"><div class="section-head"><h2>${esc(s.feedback)}</h2></div>${report(s)}</div></section>`;
  const credits = `<section id="credits" class="section"><div class="wrap narrow credits"><h2>${esc(t.credits.title)}</h2><p>${esc(t.credits.text)}</p><p class="link-row"><a href="${prefix}credits.html" hreflang="en">${toEnglish(t.credits.more)}</a><a href="${prefix}support.html" hreflang="en">${esc(t.credits.support)}</a></p></div></section>`;
  const body = `<a class="skip" href="#main">${esc(labels.skip)}</a>${L.header({lang: t.code, prefix, labels, links, languageHref})}${t.code === 'en' ? `<p class="language-suggest wrap" id="language-suggest" hidden data-options="${esc(JSON.stringify(Object.fromEntries(locales.filter(l => l.code !== 'en').map(l => [l.code, [l.chrome.suggest, `l/${l.code}.html`]]))))}"></p>` : ''}<main id="main" class="home" data-home="${t.code}">${hero}${risk}${start}${features}${firstPerson}${controls}${faq}${limits}${open}${feedback}${credits}</main>${L.footer({prefix, labels})}`;
  const headExtra = alternates() + `<link rel="preload" as="image" href="${prefix}media/run-poster.jpg">`;
  return `<!doctype html><html lang="${t.code}" dir="${t.dir}">${L.head({lang: t.code, title: t.meta.title, description: t.meta.description, prefix, extra: headExtra})}<body>${body}</body></html>\n`;
}
function scanNote(t) {
  const record = JSON.parse(fs.readFileSync(path.join(root, 'release/security-reports.json'), 'utf8'));
  const reports = (record.releases.find(r => r.tag === status.tag) || {files: []}).files.filter(f => f.status === 'reported');
  // Reported scans are listed on the release; the page only claims what was verified.
  return reports.length ? '' : t.open.scan;
}
function languagesPage() {
  const t = en.languagesPage;
  const cards = locales.map(l => `<a class="language-card" href="${l.code === 'en' ? 'index.html' : `l/${l.code}.html`}" lang="${l.code}" hreflang="${l.code}" dir="${l.dir}" data-language="${l.code}"><strong>${esc(l.name)}</strong><span>${esc(l.hero.title)}</span></a>`).join('');
  return L.shell(t.title, t.intro, `<h1>${esc(t.title)}</h1><p class="intro">${esc(t.intro)}</p><div class="language-grid">${cards}</div><p><a href="feedback.html?kind=language">${esc(t.request)}</a></p>`);
}
// English capture guide for first-person showcase footage.
function recordPage() {
  const step = (n, title, body) => `<section class="guide-step" id="${['before', 'first-person', 'clean', 'sharper', 'record', 'export'][n]}"><span class="step-num" aria-hidden="true">0${n + 1}</span><div><h2>${title}</h2>${body}</div></section>`;
  const body = `<p class="eyebrow"><span class="pill">Capture guide</span></p><h1>Record first-person clips</h1>
<p class="intro">Show what WuWa VR feels like: your character's hands, outfit and idle animation at full scale. This guide uses the tools that ship with WuWa VR; no extra capture software is needed for the basic route.</p>
<div class="guide-steps">
${step(0, 'Before you start', `<ul><li>Install and launch through the WuWa VR app as usual. See <a href="index.html#start">the three setup steps</a>.</li><li>The built-in recorder captures the game's two eye views from <strong>SteamVR</strong> or the bundled <strong>OpenXR Simulator</strong>. It does not record the desktop or audio. VDXR is not supported.</li><li>Recording costs performance. Close other heavy apps, and expect a lower frame rate than normal play.</li><li>Recordings stop after five minutes, when the game closes or when the scene changes.</li></ul>`)}
${step(1, 'Get into first person', `<p>Press ${kbd('L3 + View')} to enter first person. In UEVR settings (${kbd('L3 + R3')}), open <strong>VR → WuWa Controls → First person</strong> and choose to hide supported head bones so the body stays visible.</p><p>If the view clips into the body, raise or lower it with ${kbd('L3 + Y')} / ${kbd('L3 + X')}. For a livelier idle shot, ${kbd('L3 + D-pad Down')} toggles full animation follow. It moves the view with the character's head, so it is more intense; keep takes short or sit down.</p>`)}
${step(2, 'Clean up the frame', `<ul><li>Close UEVR settings with ${kbd('L3 + R3')}.</li><li>Hide the shortcut sheet with ${kbd('L3 + Menu')}.</li><li>Hide the game UI with ${kbd('L3 + B')}. Press it again to bring the UI back.</li><li>Optional: <strong>WuWa Controls → Streamer privacy: cover player IDs</strong> masks your UID. Still review clips for names before sharing.</li></ul><p>Then stand still and let the idle animation play. Move your head slowly: look at your hands, then down at the outfit, then out at the scenery.</p>`)}
${step(3, 'Make it sharper (optional)', `<p>In UEVR settings, open <strong>OpenXR Options → Resolution Scale</strong> and raise it above 1.0 (try 1.3–1.5). The game renders more pixels per eye and the recording looks cleaner, at a GPU cost. Lower it again if the frame rate drops. If nothing changes, restart the game.</p><p>Raising in-game graphics quality helps too. Lighting and far detail already match between the eyes.</p>`)}
${step(4, 'Record', `<ol><li>With the game running, select <strong>Developer tools</strong> in the WuWa VR app's footer. The advanced launcher opens in your browser.</li><li>Under <strong>Record gameplay</strong>, set <strong>Purpose</strong> to <strong>Content creation: clean video only</strong>.</li><li>Pick <strong>Resolution per eye: 1280 px</strong>. Use <strong>30 fps</strong> to start; try 45 or 60 only if your PC has headroom. The recorder cannot add frames the game never rendered.</li><li>Select <strong>Start recording</strong>, return to the game and play. Select <strong>Stop recording</strong> when done, then <strong>Open recordings</strong>.</li></ol><p class="small">Simulator capture: keep the simulator preview open with both eyes side by side and full render off. Its video resolution is limited by the preview window size.</p>`)}
${step(5, 'Export one eye', `<p>The recorder saves <code>clean-sbs.mp4</code>, a side-by-side stereo video. For a normal flat video, keep one eye. <code>replay.html</code> in the same folder can save single-eye stills. To crop the video with <a href="https://ffmpeg.org/">FFmpeg</a>, run this in PowerShell from the recording folder:</p><pre><code>ffmpeg -i clean-sbs.mp4 -vf "crop=iw/2:ih:0:0" -c:v libx264 -crf 18 -preset slow left-eye.mp4</code></pre><p>Need more than 1280 px? Open SteamVR's <strong>Display VR View</strong>, show one eye, maximize the window and record it with OBS. That captures the mirror at your monitor's resolution, including any resolution scale you set.</p>`)}
</div>
<aside class="notice"><p>Share what you make. A clip of first-person idle animation in your favourite outfit is a great way to show people the mod. Please credit WuWa VR and keep the account-risk notice in mind when posting gameplay.</p></aside>`;
  return L.shell('Record first-person clips', 'Capture clean first-person WuWa VR clips: first person, a clean frame, sharper rendering, the built-in recorder and one-eye export.', body);
}

fs.mkdirSync(path.join(site, 'l'), {recursive: true});
for (const t of locales) {
  if (t.code === 'en') fs.writeFileSync(path.join(site, 'index.html'), home(t, ''));
  fs.writeFileSync(path.join(site, 'l', t.code + '.html'), home(t, '../'));
}
fs.writeFileSync(path.join(site, 'languages.html'), languagesPage());
fs.writeFileSync(path.join(site, 'record.html'), recordPage());
const kept = ['credits.html', 'developers.html', 'feedback.html', 'guide.html', 'license.html', 'risk.html', 'support.html', 'testing.html', 'understanding.html'];
const changed = L.refreshChrome(site, kept);
require('./site-metadata.cjs').apply(site);
console.log(`Built ${locales.length} home pages, languages and capture guide; refreshed chrome on ${changed} pages.`);
module.exports = {locales};
