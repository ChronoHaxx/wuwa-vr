// Original site tooling, MIT. Metadata only: never rewrite the page body.
const fs = require('node:fs'), path = require('node:path');
const {escape} = require('./site-layout.cjs');
const baseUrl = 'https://chronohaxx.github.io/wuwa-vr/';
// Explicit content inventory excludes ownership-verification and update-feed files.
const pages = [
  'credits.html', 'developers.html', 'feedback.html', 'guide.html', 'index.html',
  ...['ar', 'de', 'en', 'es', 'fr', 'ja', 'ko', 'pt-BR', 'ru', 'zh-Hans'].map(code => `l/${code}.html`),
  'languages.html', 'license.html', 'record.html', 'risk.html', 'support.html', 'testing.html', 'understanding.html'
];
const entryPages = {
  'index.html': {
    title: 'Wuthering Waves VR mod for Windows | WuWa VR community beta',
    description: 'Download the free, unofficial Wuthering Waves VR community beta for Windows. Find the installer, controller shortcuts, headset setup, known issues and account-risk notice.'
  },
  'guide.html': {
    title: 'Wuthering Waves VR player guide | WuWa VR',
    description: 'Set up the unofficial Wuthering Waves VR community beta on Windows. Read installation steps, controller shortcuts, comfort settings, recovery advice and account risks.'
  },
  'l/en.html': {
    title: 'Wuthering Waves VR setup for Windows | WuWa VR beta',
    description: 'Start the free, unofficial Wuthering Waves VR community beta on Windows: install the launcher, choose a runtime, use controller shortcuts and review known risks.'
  },
  'l/zh-Hans.html': {
    title: '鸣潮 VR 安装与手柄指南 | WuWa VR Windows 社区测试版',
    description: '免费的非官方《鸣潮》VR Windows 社区测试版。简体中文指南介绍启动器安装与更新、头显运行时选择、手柄快捷键、已知问题及账号风险。'
  }
};
const sharePoster = 'media/run-poster.jpg';
function canonicalUrl(file) {
  if (!pages.includes(file)) throw Error('Not a public content page: ' + file);
  return baseUrl + (file === 'index.html' ? '' : file);
}
function attributes(tag) {
  const result = {};
  for (const match of tag.matchAll(/([\w:-]+)\s*=\s*(["'])(.*?)\2/g)) result[match[1].toLowerCase()] = match[3];
  return result;
}
function decode(text) {
  const named = {amp: '&', lt: '<', gt: '>', quot: '"', apos: "'", '#39': "'"};
  return text.replace(/&(amp|lt|gt|quot|apos|#39);/g, (_, key) => named[key]);
}
function metadataFor(file, head) {
  if (entryPages[file]) return entryPages[file];
  const title = /<title\b[^>]*>([\s\S]*?)<\/title>/i.exec(head);
  const description = [...head.matchAll(/<meta\b[^>]*>/gi)].map(match => attributes(match[0]))
    .find(attrs => attrs.name?.toLowerCase() === 'description');
  if (!title || !description?.content) throw Error('Missing existing page metadata: ' + file);
  return {title: decode(title[1]), description: decode(description.content)};
}
function updateHtml(file, html) {
  const match = /<head\b[^>]*>([\s\S]*?)<\/head>/i.exec(html);
  if (!match) throw Error('Missing page head: ' + file);
  let head = match[1];
  const meta = metadataFor(file, head), canonical = canonicalUrl(file);
  // Replace just these owned tags. Verification, robots, styles and scripts survive.
  head = head.replace(/<title\b[^>]*>[\s\S]*?<\/title>/gi, '');
  head = head.replace(/<(?:meta|link)\b[^>]*>/gi, tag => {
    const attrs = attributes(tag), name = (attrs.name || attrs.property || '').toLowerCase();
    return name === 'description' || name.startsWith('og:') || name.startsWith('twitter:') ||
      (attrs.rel || '').toLowerCase().split(/\s+/).includes('canonical') ? '' : tag;
  });
  const tags = [
    `<title>${escape(meta.title)}</title>`,
    `<meta name="description" content="${escape(meta.description)}">`,
    `<link rel="canonical" href="${canonical}">`,
    '<meta property="og:type" content="website">',
    '<meta property="og:site_name" content="WuWa VR">',
    `<meta property="og:title" content="${escape(meta.title)}">`,
    `<meta property="og:description" content="${escape(meta.description)}">`,
    `<meta property="og:url" content="${canonical}">`,
    `<meta name="twitter:card" content="${entryPages[file] ? 'summary_large_image' : 'summary'}">`,
    `<meta name="twitter:title" content="${escape(meta.title)}">`,
    `<meta name="twitter:description" content="${escape(meta.description)}">`
  ];
  if (entryPages[file]) {
    const alt = file === 'l/zh-Hans.html'
      ? '在跑步机上以第一人称穿越索拉里斯-3 的实机画面：天空中的环形平台。'
      : 'First-person gameplay from a treadmill walk across Solaris-3: a ring platform above the clouds.';
    tags.push(`<meta property="og:image" content="${baseUrl + sharePoster}">`,
      `<meta property="og:image:alt" content="${escape(alt)}">`,
      `<meta name="twitter:image" content="${baseUrl + sharePoster}">`,
      `<meta name="twitter:image:alt" content="${escape(alt)}">`);
  }
  const replacement = match[0].replace(match[1], head + tags.join(''));
  return html.slice(0, match.index) + replacement + html.slice(match.index + match[0].length);
}
function apply(site) {
  const root = path.dirname(site);
  const media = JSON.parse(fs.readFileSync(path.join(root, 'release/site-media.json'), 'utf8')).media || [];
  if (!media.some(item => item.kind === 'gameplay-video' && item.ownerApproved === true && item.poster === 'site/' + sharePoster) ||
      !fs.existsSync(path.join(site, sharePoster))) throw Error('Share poster must remain available and owner-approved');
  // Prepare all content first, so an unexpected input fails before any page write.
  const updates = pages.map(file => {
    const target = path.join(site, file), old = fs.readFileSync(target, 'utf8');
    return {target, old, next: updateHtml(file, old)};
  });
  const sitemap = '<?xml version="1.0" encoding="UTF-8"?>\n' +
    '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n' +
    pages.map(file => `  <url><loc>${canonicalUrl(file)}</loc></url>`).join('\n') + '\n</urlset>\n';
  for (const {target, old, next} of updates) if (old !== next) fs.writeFileSync(target, next);
  const sitemapPath = path.join(site, 'sitemap.xml');
  if (!fs.existsSync(sitemapPath) || fs.readFileSync(sitemapPath, 'utf8') !== sitemap) fs.writeFileSync(sitemapPath, sitemap);
  return pages.length;
}
module.exports = {apply, updateHtml, canonicalUrl, baseUrl, pages};
