// Original site tooling, MIT. The controller sections of the home pages and the
// player guide, built from mod/controls.json and the mod's own catalogs
// (mod/localization/wuwa), so the website and the in-VR shortcut sheet use the
// same words in every language. Button names follow Xbox; data-ps carries the
// PlayStation name for the toggle.
const fs = require('node:fs'), path = require('node:path'), assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..');
const controls = JSON.parse(fs.readFileSync(path.join(root, 'mod/controls.json'), 'utf8'));
const catalogs = {};
const catalog = code => catalogs[code] ||= JSON.parse(fs.readFileSync(path.join(root, 'mod/localization/wuwa', code + '.json'), 'utf8')).strings;

const esc = s => String(s).replace(/[&<>"]/g, c => ({'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;'})[c]);
const PS = {A: '✕', B: '○', X: '□', Y: '△', LB: 'L1', RB: 'R1', LT: 'L2', RT: 'R2', View: 'Create', Menu: 'Options'};
const BUTTON = /(?<![A-Za-z])(L3|R3|LB|RB|LT|RT|View|Menu|F7|A|B|X|Y)(?![A-Za-z])/g;
const toPs = text => text.replace(BUTTON, name => PS[name] || name);
const pureButtons = part => part.split(' / ').every(p => /^(L3|R3|LB|RB|LT|RT|View|Menu|F7|A|B|X|Y)$/.test(p));
// Words in a key or action must be translated; button names and symbols need not be.
const needsWords = text => /[a-z]{2,}/.test(text.replace(BUTTON, ''));

// View and Menu are easier to find by their printed icons than by name.
const ICON = {
  View: '<svg class="kbd-icon" viewBox="0 0 16 16" aria-hidden="true" focusable="false"><path d="M2.5 4.5h7v6h-7zM6.5 10.5v1.5h7v-6h-4"/></svg>',
  Menu: '<svg class="kbd-icon" viewBox="0 0 16 16" aria-hidden="true" focusable="false"><path d="M3 4.5h10M3 8h10M3 11.5h10"/></svg>'
};
function chip(text) {
  const ps = toPs(text), face = /^[ABXY]$/.test(text) ? ` data-face="${text}"` : '';
  const label = ps !== text ? `<span data-ps="${esc(ps)}">${esc(text)}</span>` : esc(text);
  return `<kbd${face}>${ICON[text] || ''}${label}</kbd>`;
}
// "L3 + Y / X" -> chips joined by + and /; "Double R3" -> R3 ×2; worded parts stay one chip.
function combo(keys, t) {
  const double = keys.match(/^Double (L3|R3)$/);
  if (double) return `<bdi dir="ltr" class="combo" title="${esc(t(keys))}">${chip(double[1])}<span class="times">×2</span></bdi>`;
  const parts = t(keys).split(/\s*(\+|→)\s*/);  // some catalogs write RT+左 without spaces
  const html = parts.map((part, i) => i % 2 ? `<span class="op">${part}</span>` :
    pureButtons(part) ? part.split(' / ').map(chip).join('<span class="op">/</span>') : chip(part)).join('');
  return `<bdi dir="ltr" class="combo">${html}</bdi>`;
}
// Which drawn buttons an English key string uses, e.g. "RT + D-pad L / R" -> RT Left Right.
function buttonsOf(keys) {
  const k = keys.replace(/\(.*?\)/g, ''), ids = new Set();
  for (const [, name] of k.matchAll(/(?<![A-Za-z])(L3|R3|LB|RB|LT|RT|View|Menu|A|B|X|Y)(?![A-Za-z])/g)) ids.add(name);
  if (/left \/ right stick/i.test(k)) { ids.add('L3'); ids.add('R3'); }
  else if (/left stick/i.test(k)) ids.add('L3');
  else if (/right stick/i.test(k)) ids.add('R3');
  const dpad = k.match(/D-pad(?: (Up|Down|Left \/ Right|L \/ R|Left|Right))?/);
  if (dpad) {
    const dir = dpad[1];
    for (const arm of !dir ? ['Up', 'Down', 'Left', 'Right'] : /\//.test(dir) ? ['Left', 'Right'] : [dir]) ids.add(arm);
  }
  return [...ids];
}

// Original controller drawing (not an official asset). data-btn ties shapes to
// callouts and table rows. "lit" outlines the buttons a sheet page uses.
function pad({lit = [], modifier = true, defs = true, leaders = true} = {}) {
  const on = id => lit.includes(id) ? ' lit' : '';
  const face = (id, x, y) => `<g class="btn face-${id}${on(id)}" data-btn="${id}${id === 'Y' || id === 'X' ? ' YX' : ''}"><circle cx="${x}" cy="${y}" r="17"/><text x="${x}" y="${y + 6}" data-ps="${PS[id]}">${id}</text></g>`;
  const shoulder = (id, x, y, w, h, r, rot) => `<g class="btn${on(id)}" data-btn="${id}" transform="rotate(${rot} ${x + w / 2} ${y + h / 2})"><rect x="${x}" y="${y}" width="${w}" height="${h}" rx="${r}"/><text x="${x + w / 2}" y="${y + h / 2 + 5}" data-ps="${PS[id]}">${id}</text></g>`;
  const arm = (id, x, y, w, h) => `<rect x="${x}" y="${y}" width="${w}" height="${h}" rx="4" class="btn${on(id)}" data-btn="${id}"/>`;
  return `<svg class="pad-svg" viewBox="0 0 640 400" aria-hidden="true" focusable="false">
${defs ? '<defs><linearGradient id="pad-gold" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#f7e3a3"/><stop offset=".55" stop-color="#d6af5b"/><stop offset="1" stop-color="#9c7630"/></linearGradient></defs>' : ''}
${shoulder('LT', 140, 22, 84, 34, 14, -8)}${shoulder('RT', 416, 22, 84, 34, 14, 8)}
${shoulder('LB', 118, 62, 128, 24, 12, -8)}${shoulder('RB', 394, 62, 128, 24, 12, 8)}
<path class="pad-body" d="M210 92C250 82 390 82 430 92C505 104 548 132 572 196C600 280 606 352 566 372C532 389 500 368 478 334C460 306 438 292 404 292L236 292C202 292 180 306 162 334C140 368 108 389 74 372C34 352 40 280 68 196C92 132 135 104 210 92Z"/>
<circle class="pad-home" cx="320" cy="124" r="17"/>
<g class="btn${on('View')}" data-btn="View"><rect x="270" y="150" width="26" height="17" rx="6"/><path class="glyph" d="M277 155h7v6h-7zM281 158h7v6"/></g>
<g class="btn${on('Menu')}" data-btn="Menu"><rect x="344" y="150" width="26" height="17" rx="6"/><path class="glyph" d="M351 155h12M351 158.5h12M351 162h12"/></g>
<text class="pad-label" x="283" y="188" data-ps="Create">View</text><text class="pad-label" x="357" y="188" data-ps="Options">Menu</text>
<g class="btn${modifier ? ' modifier' : on('L3')}" data-btn="L3"><circle cx="200" cy="170" r="40" class="well"/><circle cx="200" cy="170" r="27"/><text x="200" y="176">L3</text></g>
<g class="dpad">${arm('Up', 253, 217, 18, 22)}${arm('Left', 235, 235, 22, 18)}${arm('Right', 267, 235, 22, 18)}${arm('Down', 253, 249, 18, 22)}</g>
<g class="btn${on('R3')}" data-btn="R3"><circle cx="388" cy="244" r="37" class="well"/><circle cx="388" cy="244" r="25"/><text x="388" y="250">R3</text></g>
${face('Y', 456, 134)}${face('X', 422, 168)}${face('B', 490, 168)}${face('A', 456, 202)}
${leaders ? '<g class="pad-leaders"></g>' : ''}
</svg>`;
}

const LEFT = ['LT', 'LB', 'View', 'Down'], RIGHT = ['RT', 'RB', 'Menu', 'YX', 'B', 'A', 'R3'];
const CHIP = {YX: 'Y / X'};

// Catalog lookups for one language; the build fails if a shortcut is untranslated.
function language(code) {
  const cat = catalog(code), en = catalog('en');
  const t = key => cat[key] ?? key;
  for (const row of controls.groups.flatMap(g => g.rows)) {
    assert(en[row.action] !== undefined, `mod/controls.json action missing from en catalog: ${row.action}`);
    if (code !== 'en') {
      assert(cat[row.action] !== undefined, `${code} catalog lacks action: ${row.action}`);
      if (needsWords(row.keys) && !/^Double (L3|R3)$/.test(row.keys)) assert(cat[row.keys] !== undefined, `${code} catalog lacks keys: ${row.keys}`);
    }
  }
  return t;
}
function toolbar(copy, image, code) {
  return `<div class="pad-toolbar"><div class="pad-switch" role="group" aria-label="${esc(copy.names)}" hidden><button type="button" data-pad-choice="xbox" aria-pressed="true">Xbox</button><button type="button" data-pad-choice="ps" aria-pressed="false">PlayStation</button></div><a class="pad-save" href="${image}" download="wuwa-vr-controls-${code}.png">${esc(copy.image)}</a></div>`;
}
// The "hold L3, then press" card: callouts either side of the drawing, then gestures.
function card({dir, copy, t}) {
  const rows = controls.groups.flatMap(g => g.rows);
  const byButton = Object.fromEntries(rows.filter(r => r.button).map(r => [r.button, r]));
  const callout = id => {
    const row = byButton[id];
    assert(row, 'No row for diagram button ' + id);
    const label = id === 'Down' ? `${t('D-pad')} ↓` : CHIP[id] || id;
    const keys = pureButtons(label) ? label.split(' / ').map(chip).join('<span class="op">/</span>') : chip(label);
    return `<li data-btn="${id}"><bdi dir="ltr" class="combo">${keys}</bdi><span dir="${dir}">${esc(t(row.action))}</span></li>`;
  };
  const gestures = rows.filter(r => r.gesture).map(r => `<li>${combo(r.keys, t)}<span>${esc(t(r.action))}</span></li>`).join('');
  // The L3 in the title is an input too, so it gets the same key chip.
  assert(/L3/.test(copy.hold), 'pad.hold must name L3');
  const hold = esc(copy.hold).replace('L3', `<bdi dir="ltr" class="combo">${chip('L3')}</bdi>`);
  return `<figure class="pad-card" id="pad-card"><figcaption class="pad-title"><span class="pad-brand">WuWa VR</span><span>${hold}</span></figcaption>
<div class="pad-diagram"><ul class="pad-list pad-left">${LEFT.map(callout).join('')}</ul>${pad()}<ul class="pad-list pad-right">${RIGHT.map(callout).join('')}</ul></div>
<div class="pad-more"><p class="pad-more-title">${esc(copy.more)}</p><ul class="pad-gestures">${gestures}</ul></div>
<p class="pad-url">chronohaxx.github.io/wuwa-vr</p></figure>`;
}
// Table rows for one sheet page, with a heading row whenever the context changes.
function tableRows(group, t, copy, {hover = false} = {}) {
  const when = {freecam: [copy.freecam, 'Double R3'], mouse: [copy.mouse, 'L3 + LB'], uevr: [copy.uevr, 'L3 + R3']};
  let context = null;
  return group.rows.map(r => {
    let head = '';
    if ((r.when || null) !== context) {
      context = r.when || null;
      if (context) head = `<tr class="when"><th colspan="2" scope="colgroup">${esc(when[context][0])} ${combo(when[context][1], t)}</th></tr>`;
    }
    const btns = hover ? ` data-btns="${buttonsOf(r.keys).join(' ')}"` : '';
    return head + `<tr${btns}><th scope="row">${combo(r.keys, t)}</th><td>${esc(t(r.action))}</td></tr>`;
  }).join('');
}

// Home page section, in every language.
function render({code, dir, copy, prefix, guideLink}) {
  const t = language(code);
  const tables = controls.groups.map(g => `<section class="pad-group" aria-labelledby="pad-${g.id}"><h3 id="pad-${g.id}">${esc(t(g.title).replace(/^\d+\s+/, ''))}</h3><div class="table-scroll"><table class="shortcut-table"><tbody>${tableRows(g, t, copy)}</tbody></table></div></section>`).join('');
  return `<section id="controls" class="section alt"><div class="wrap">
<div class="section-head"><h2>${esc(copy.title)}</h2><p>${esc(copy.intro)}</p></div>
${toolbar(copy, `${prefix}media/controls/${code}.png`, code)}
${card({dir, copy, t})}
<p class="pad-note small">${esc(copy.psNote)}</p>
<h3 class="pad-all">${esc(copy.all)}</h3><div class="pad-groups">${tables}</div>
<p class="section-foot">${guideLink}</p></div></section>`;
}

// Player guide (English): the card, then one card per in-VR sheet page with that
// page's buttons outlined; pointing at a row lights its buttons. No nested
// <section>: the release build replaces this section by pattern.
const PAGE_NOTES = {
  everyday: 'Shown on every page of the in-VR sheet: the shortcuts you use all the time.',
  cameras: 'View modes, first person, the fixed camera, Polar walk and freecam.',
  menus: 'Sheet pages, the V / Tab utility and HUD / mouse adjustment.',
  uevr: "Driving UEVR's own settings with a controller. L3 + R3 opens and closes them."
};
function renderGuide({copy}) {
  const t = language('en');
  const pages = controls.groups.map(g => {
    assert(PAGE_NOTES[g.id], 'No guide note for sheet page ' + g.id);
    const [, number, title] = t(g.title).match(/^(\d+)\s+(.*)$/);
    const used = [...new Set(g.rows.flatMap(r => buttonsOf(r.keys)))];
    return `<article class="pad-page" id="sheet-${g.id}" aria-labelledby="sheet-${g.id}-title"><header class="pad-page-head"><span class="step-num pad-page-num" aria-hidden="true">${number}</span><div><h3 id="sheet-${g.id}-title">${esc(title)}</h3><p>${esc(PAGE_NOTES[g.id])}</p></div></header>
<div class="pad-page-body">${pad({lit: used, modifier: false, defs: false, leaders: false})}<div class="table-scroll"><table class="shortcut-table"><tbody>${tableRows(g, t, copy, {hover: true})}</tbody></table></div></div></article>`;
  }).join('\n');
  return `<section id="visual-controls"><h2>Controller guide</h2><p>Every shortcut, laid out like the four pages of the in-VR shortcut sheet (<bdi dir="ltr" class="combo">${chip('L3')}<span class="op">+</span>${chip('Menu')}</bdi> in VR). Point at a row to light its buttons; the searchable text reference follows.</p>
${toolbar(copy, 'media/controls/en.png', 'en')}
${card({dir: 'ltr', copy, t})}
<p class="pad-note small">${esc(copy.psNote)}</p>
<div class="pad-pages">${pages}</div></section>`;
}

module.exports = {render, renderGuide, controls, catalog, buttonsOf};
