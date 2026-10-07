// Original website layout. MIT; see LICENSE.md for the scope.
// One header/footer for every page: the home builder writes it into new pages,
// and refreshChrome() swaps it into pages that other generators maintain.
const fs = require('node:fs'), path = require('node:path');
const root = path.resolve(__dirname, '..');
const escape = s => String(s).replace(/[&<>"']/g, c => ({'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'}[c]));
const languages = [['en', 'English'], ['zh-Hans', '简体中文'], ['ja', '日本語'], ['ko', '한국어'], ['es', 'Español'],
  ['pt-BR', 'Português (Brasil)'], ['fr', 'Français'], ['de', 'Deutsch'], ['ru', 'Русский'], ['ar', 'العربية']];
const rtl = code => code === 'ar';
const status = () => JSON.parse(fs.readFileSync(path.join(root, 'dev/site-status.json'), 'utf8'));
const repo = 'https://github.com/ChronoHaxx/wuwa-vr';
const installerUrl = () => `${repo}/releases/download/${status().tag}/${status().installer}`;
// English chrome; localized home pages pass their own labels.
const english = {
  skip: 'Skip to content', nav: 'Site', home: 'Home', guide: 'Guide', capture: 'Capture', feedback: 'Feedback',
  install: 'Install', features: 'Features', faq: 'FAQ', download: 'Download', language: 'Language',
  tagline: 'Free, unofficial fan project. Not affiliated with Kuro Games.', explore: 'Explore', project: 'Project',
  languages: 'All languages', code: 'Code & contribute', releases: 'Releases', risk: 'Account risk & disclaimer',
  license: 'License scope', support: 'Optional support', credits: 'Credits',
  mit: 'Original independent website and guide work: MIT. Upstream components keep their own terms.'
};
const globe = '<svg class="icon" viewBox="0 0 24 24" width="18" height="18" aria-hidden="true"><circle cx="12" cy="12" r="9"/><path d="M3 12h18M12 3c2.5 2.6 3.8 5.6 3.8 9s-1.3 6.4-3.8 9c-2.5-2.6-3.8-5.6-3.8-9S9.5 5.6 12 3z"/></svg>';
// href(code) gives each language's page; the current one is marked for assistive tech.
function languageMenu({lang = 'en', prefix = '', label = english.language, href} = {}) {
  const target = href || (code => `${prefix}l/${code}.html`);
  const current = (languages.find(([code]) => code === lang) || languages[0])[1];
  return `<details class="lang-menu"><summary aria-label="${escape(label)}: ${escape(current)}">${globe}<span>${escape(current)}</span></summary>` +
    `<nav aria-label="${escape(label)}"><ul>${languages.map(([code, name]) => `<li><a href="${target(code)}" lang="${code}" hreflang="${code}" dir="${rtl(code) ? 'rtl' : 'ltr'}" data-language="${code}"${code === lang ? ' aria-current="page"' : ''}>${escape(name)}</a></li>`).join('')}</ul></nav></details>`;
}
// Kept for older generators: a plain list of language links.
function languageLinks(prefix = '') {
  return `<nav class="language-list" aria-label="Languages">${languages.map(([code, name]) => `<a href="${prefix}l/${code}.html" lang="${code}" hreflang="${code}" dir="${rtl(code) ? 'rtl' : 'ltr'}" data-language="${code}">${escape(name)}</a>`).join('')}</nav>`;
}
function header({lang = 'en', prefix = '', labels = {}, links, languageHref} = {}) {
  const t = {...english, ...labels};
  const items = links || [[`${prefix}index.html`, t.home], [`${prefix}guide.html`, t.guide], [`${prefix}record.html`, t.capture], [`${prefix}feedback.html`, t.feedback]];
  return `<header class="site-header"><div class="wrap header-row">` +
    `<a class="brand" href="${prefix}index.html" lang="en" dir="ltr"><img src="${prefix}media/mark.svg" width="32" height="32" alt=""><span>WuWa VR</span></a>` +
    `<nav class="site-nav" aria-label="${escape(t.nav)}">${items.map(([href, label]) => `<a href="${href}">${escape(label)}</a>`).join('')}</nav>` +
    `<div class="header-actions">${languageMenu({lang, prefix, label: t.language, href: languageHref})}` +
    `<a class="button primary header-download" href="${installerUrl()}">${escape(t.download)}</a></div></div></header>`;
}
function footer({prefix = '', labels = {}} = {}) {
  const t = {...english, ...labels};
  const link = (href, label, extra = '') => `<a href="${href}"${extra}>${escape(label)}</a>`;
  return `<footer class="site-footer"><div class="wrap footer-grid">` +
    `<div class="footer-brand"><a class="brand" href="${prefix}index.html" lang="en" dir="ltr"><img src="${prefix}media/mark.svg" width="28" height="28" alt=""><span>WuWa VR</span></a><p>${escape(t.tagline)}</p></div>` +
    `<nav aria-label="${escape(t.explore)}"><p class="footer-title">${escape(t.explore)}</p>${link(prefix + 'guide.html', t.guide)}${link(prefix + 'record.html', t.capture)}${link(prefix + 'feedback.html', t.feedback)}${link(prefix + 'languages.html', t.languages)}</nav>` +
    `<nav aria-label="${escape(t.project)}"><p class="footer-title">${escape(t.project)}</p>${link(prefix + 'developers.html', t.code)}<a data-project-link="code" hidden rel="noopener noreferrer">GitHub</a><a data-project-link="releases" hidden rel="noopener noreferrer">${escape(t.releases)}</a>${link(prefix + 'risk.html', t.risk)}${link(prefix + 'license.html', t.license)}${link(prefix + 'support.html', t.support)}${link(prefix + 'credits.html', t.credits)}</nav>` +
    `</div><div class="wrap footer-note"><p>${escape(t.mit)}</p></div></footer>`;
}
function head({lang = 'en', title, description, prefix = '', extra = ''}) {
  return `<head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="description" content="${escape(description)}"><meta name="theme-color" content="#0b0b0c"><title>${escape(title)}</title><link rel="icon" href="${prefix}media/mark.svg" type="image/svg+xml"><link rel="stylesheet" href="${prefix}style.css"><script src="${prefix}config.js" defer></script><script src="${prefix}app.js" defer></script>${extra}</head>`;
}
function shell(title, description, body, {lang = 'en', dir = 'ltr', prefix = '', labels = {}} = {}) {
  return `<!doctype html><html lang="${lang}" dir="${dir}">${head({lang, title: `${title} — WuWa VR`, description, prefix})}<body><a class="skip" href="#main">${escape({...english, ...labels}.skip)}</a>${header({lang, prefix, labels})}<main class="wrap page" id="main">${body}</main>${footer({prefix, labels})}</body></html>\n`;
}
// Swap the shared chrome into English pages kept by other generators, leaving their bodies alone.
function refreshChrome(site, files) {
  let changed = 0;
  for (const file of files) {
    const full = path.join(site, file), html = fs.readFileSync(full, 'utf8');
    const prefix = file.includes('/') ? '../' : '';
    const next = html
      .replace(/<header class="(?:header wrap|site-header)"[\s\S]*?<\/header>/, () => header({prefix}))
      .replace(/<footer class="(?:footer wrap|site-footer)"[\s\S]*?<\/footer>/, () => footer({prefix}))
      .replace(/<main class="wrap" id="main">/, '<main class="wrap page" id="main">')
      .replace(/<meta name="theme-color" content="[^"]*">/, '<meta name="theme-color" content="#0b0b0c">');
    if (!/<header class="site-header"/.test(next) || !/<footer class="site-footer"/.test(next)) throw Error('Missing page chrome: ' + file);
    if (next !== html) { fs.writeFileSync(full, next); changed++; }
  }
  return changed;
}
module.exports = {shell, head, header, footer, escape, languageLinks, languageMenu, languages, rtl, refreshChrome, english, installerUrl, repo, status};
