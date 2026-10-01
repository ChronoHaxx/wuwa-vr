// Original website layout. MIT; see LICENSE.md for the scope.
const escape = s => String(s).replace(/[&<>"']/g, c => ({'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'}[c]));
const languages = [['en', 'English'], ['zh-Hans', '简体中文'], ['ja', '日本語'], ['ko', '한국어'], ['es', 'Español'], ['pt-BR', 'Português (Brasil)'], ['fr', 'Français'], ['de', 'Deutsch'], ['ru', 'Русский'], ['ar', 'العربية']];
const repo = 'https://github.com/ChronoHaxx/wuwa-vr';

// Link to `page` in another language, from an English page at the site root.
// dev/translate-site.py rewrites these for pages under l/<lang>/.
const languageHref = (code, page, prefix = '') => code === 'en' ? prefix + page : `${prefix}l/${code}/${page}`;

function languageLinks(page = 'index.html', prefix = '') {
  return `<details class="language-picker"><summary>Language / 语言 / اللغة</summary><nav aria-label="Languages">${languages.map(([code, name]) =>
    `<a href="${languageHref(code, page, prefix)}" lang="${code}" hreflang="${code}" dir="${code === 'ar' ? 'rtl' : 'ltr'}" data-language="${code}">${name}</a>`).join('')}</nav></details>`;
}

function shell(title, description, body, {page = 'index.html'} = {}) {
  const alternates = languages.map(([code]) => `<link rel="alternate" hreflang="${code}" href="https://chronohaxx.github.io/wuwa-vr/${languageHref(code, page)}">`).join('');
  return `<!doctype html><html lang="en" dir="ltr"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="description" content="${escape(description)}"><meta name="theme-color" content="#17191c"><title>${escape(title)} — WuWa VR</title>${alternates}<link rel="icon" href="media/mark.svg" type="image/svg+xml"><link rel="stylesheet" href="style.css"><script src="config.js" defer></script><script src="app.js" defer></script></head><body><a class="skip" href="#main">Skip to content</a><header class="header wrap"><a class="wordmark" href="index.html" lang="en" dir="ltr">WuWa VR</a><nav aria-label="Site"><a href="guide.html">Guide</a><a href="feedback.html">Feedback</a><a href="credits.html">Credits</a><a href="languages.html">Languages</a></nav>${languageLinks(page)}</header><main class="wrap" id="main">${body}</main><footer class="footer wrap"><p>Free, unofficial fan project. Not affiliated with Kuro Games.</p><nav aria-label="Project information"><a href="developers.html">Code &amp; contribute</a><a href="${repo}">GitHub</a><a href="${repo}/releases">Releases</a><a href="risk.html">Account risk &amp; disclaimer</a><a href="license.html">License</a><a href="support.html">Optional support</a></nav><p class="small">Original website and guide work: MIT. Upstream components keep their own terms.</p></footer></body></html>\n`;
}
module.exports = {shell, escape, languageLinks, languageHref, languages, repo};
