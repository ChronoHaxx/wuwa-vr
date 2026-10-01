// Builds every English page of the website. Original site tooling, MIT; see LICENSE.md.
//
// Sources: docs/*.md and the root CREDITS/SUPPORT/CONTRIBUTING files (prose), dev/site-status.json
// (the current download: change it once per release), release/site-media.json (approved clips)
// and the templates below. Edit those, never the generated site/*.html. Translations are built
// from these pages afterwards by dev/translate-site.py.
//
//   node dev/build-site.cjs && python dev/translate-site.py
const fs = require('node:fs'), path = require('node:path');
const {shell, escape} = require('./site-layout.cjs');
const root = path.resolve(__dirname, '..'), site = path.join(root, 'site');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');
const status = JSON.parse(read('dev/site-status.json'));
const repo = 'https://github.com/ChronoHaxx/wuwa-vr';
const releaseUrl = `${repo}/releases/tag/${status.tag}`;
const zipUrl = `${repo}/releases/download/${status.tag}/WuWa-VR-Launcher.zip`;

const modules = [root, process.env.WUWA_NODE_MODULES, path.join(root, 'dev-tools/node_modules'),
  path.join(process.env.USERPROFILE || process.env.HOME || '', '.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules')].filter(Boolean);

(async () => {
  const {marked} = await import(require('node:url').pathToFileURL(require.resolve('marked', {paths: modules})).href);
  // Markdown links between docs become links between site pages.
  const links = {'START-HERE.md': 'guide.html#start', 'CONTROLS.md': 'guide.html#controls', 'COMFORT.md': 'guide.html#comfort',
    'TROUBLESHOOTING.md': 'guide.html#recovery', 'CREDITS.md': 'credits.html', 'SUPPORT.md': 'support.html',
    'LICENSE.md': 'license.html', 'MIT.md': 'license.html#mit', 'RISK.md': 'risk.html', 'DEVELOPING.md': 'developers.html',
    'CONTRIBUTING.md': 'developers.html#contributing', 'UNDERSTANDING-WUWA-VR.md': 'understanding.html'};
  const markdown = (file, {demote = true, controls = false} = {}) => {
    let html = marked.parse(read(file).replace(/^# .+\r?\n/, ''), {gfm: true});
    html = html.replace(/href="([^"#]+\.md)(#[^"]*)?"/g, (_, target, anchor) => {
      if (/^https?:\/\//i.test(target)) return `href="${target}${anchor || ''}"`;
      const page = links[path.basename(target)];
      if (!page) throw Error(`Unmapped link ${target} in ${file}`);
      return `href="${page}${anchor && !page.includes('#') ? anchor : ''}"`;
    });
    html = html.replace(/<table>/g, '<div class="table-scroll"><table>').replace(/<\/table>/g, '</table></div>');
    if (demote) html = html.replace(/<(\/?)h2>/g, '<$1h3>');
    if (controls) html = html.replace(/<tbody>([\s\S]*?)<\/tbody>/g, (_, body) => '<tbody>' + body.replace(/<tr>/g, '<tr data-control-row>') + '</tbody>');
    return html;
  };
  const write = (file, title, description, body) => fs.writeFileSync(path.join(site, file), shell(title, description, body, {page: file}));
  const png = file => {
    const b = fs.readFileSync(path.join(site, file));
    if (b.toString('ascii', 12, 16) !== 'IHDR') throw Error('Not a PNG: ' + file);
    return {width: b.readUInt32BE(16), height: b.readUInt32BE(20)};
  };
  const risk = `<aside class="notice risk"><strong>Use at your own risk.</strong> This unofficial mod may be detected or blocked by anti-cheat; account restrictions or a ban are possible. It is not approved by Kuro Games. <a href="risk.html">Read the account-risk notice</a>.</aside>`;

  // Owner-approved gameplay clips only. A private provenance file, when present, wins.
  const privateMedia = path.join(root, 'release/media-provenance.json');
  const media = JSON.parse(fs.existsSync(privateMedia) ? fs.readFileSync(privateMedia, 'utf8') : read('release/site-media.json'));
  const clips = (media.media || []).filter(m => m.kind === 'gameplay-video' && m.ownerApproved === true);
  fs.writeFileSync(path.join(root, 'release/site-media.json'), JSON.stringify({schema: 1, media: clips.map(m => Object.fromEntries(
    ['kind', 'ownerApproved', 'file', 'poster', 'caption', 'build', 'recorded', 'headset'].filter(k => m[k] !== undefined).map(k => [k, m[k]])))}, null, 2) + '\n');
  const video = clips.map(c => {
    if (!/^site\/media\/[\w.-]+\.(mp4|webm)$/.test(c.file || '') || !fs.existsSync(path.join(root, c.file))) throw Error('Approved clip is missing or misnamed: ' + c.file);
    if (c.poster && (!/^site\/media\/[\w.-]+\.(png|jpg)$/.test(c.poster) || !fs.existsSync(path.join(root, c.poster)))) throw Error('Clip poster is missing: ' + c.poster);
    for (const key of ['caption', 'build', 'recorded', 'headset']) if (!c[key]) throw Error(`Approved clip needs "${key}": ` + c.file);
    const src = c.file.slice(5);
    return `<figure><video controls preload="none"${c.poster ? ` poster="${escape(c.poster.slice(5))}"` : ''}><source src="${escape(src)}" type="video/${src.endsWith('.webm') ? 'webm' : 'mp4'}"><a href="${escape(src)}">Download the gameplay clip</a></video><figcaption>${escape(c.caption)} Recorded ${escape(c.recorded)}, ${escape(c.headset)}, on an earlier build.</figcaption></figure>`;
  }).join('');

  // Real crops of the launcher page, one per numbered setup step.
  const launcherSteps = [
    ['1-risk', 'Open <strong>WuWa VR Launcher.exe</strong> from the extracted folder. Read the account-risk notice and tick the box.', 'Launcher step 1, Account risk: the risk notice and a ticked box, I have read the account-risk notice and accept the risk.'],
    ['2-build', 'Check the selected build. A download contains one build.', 'Launcher step 2, Choose a build, from an earlier package that contained two builds.'],
    ['3-game', 'Choose how you start the game. The official launcher is the tested route; Steam and Epic are untested.', 'Launcher step 3, How do you start the game: the official launcher is selected; Steam/Epic startup is marked experimental.'],
    ['4-start', 'Start your headset software, then <strong>Apply &amp; launch</strong>. Accept the Windows permission prompt and press Play.', 'Launcher step 4, Start: Apply and launch, Check readiness and Apply without launching buttons, and a Ready message.']];
  const visualSteps = `<ol class="visual-steps">${launcherSteps.map(([id, text, alt]) => {
    const size = png(`media/launcher-step-${id}.png`);
    return `<li><figure><a href="media/launcher-step-${id}.png" aria-label="Open launcher step ${id[0]} at full size"><img src="media/launcher-step-${id}.png" width="${Math.round(size.width / 2)}" height="${Math.round(size.height / 2)}" loading="lazy" alt="${escape(alt)}"></a><figcaption>${text}</figcaption></figure></li>`;
  }).join('')}</ol><p class="small">Launcher screenshots from September 2026; your page may look slightly different.</p>`;
  const withVisualSteps = html => {
    const next = html.replace(/(<h3>Install and launch<\/h3>[\s\S]*?<\/ol>)/, '$1' + visualSteps);
    if (next === html) throw Error('START-HERE.md needs an "Install and launch" list for the launcher pictures');
    return next;
  };

  // Home
  write('index.html', 'WuWa VR', 'Free fan-made VR mod for Wuthering Waves on PC headsets: download, controller guide and known issues.',
`<h1>WuWa VR</h1><p class="intro">Play Wuthering Waves in VR on a PC headset. A free, fan-made mod built on <a href="https://uevr.io/">praydog's UEVR</a>, maintained by ChronoHaxx.</p>
<p><a class="button primary" href="${zipUrl}">Download · ${escape(status.label)} · Windows ZIP</a> <a href="${releaseUrl}">Release notes &amp; source</a></p>
<p class="small">For game version ${escape(status.game)} · about ${status.zipMB} MB · portable, nothing to install</p>
<p><strong>New:</strong> ${escape(status.news)}</p>
<nav class="page-links" aria-label="Start here"><a href="guide.html">Player guide</a><a href="guide.html#recovery">Known issues</a><a href="feedback.html">Report a bug or request a feature</a></nav>${risk}
<section id="gameplay"><h2>See it in play</h2><p>Short clips from headset recordings; nothing autoplays.</p><div class="feature-clips">${video}</div><details><summary>More stereo views</summary><figure><img src="media/rooftop-stereo.png" width="970" height="499" loading="lazy" alt="Left and right eye views from a rooftop above the city at dusk."><figcaption>Rooftop, left and right eye. OpenXR Simulator, 24 September 2026. The HUD is on a separate layer and is not shown.</figcaption></figure><figure><img src="media/coast-stereo.png" width="970" height="499" loading="lazy" alt="Left and right eye views of a character beside the coast under a blue sky."><figcaption>The coast, same capture method.</figcaption></figure></details></section>
<section id="get-started"><h2>How it starts</h2>${visualSteps}<p>In VR, <kbd>L3</kbd> + <kbd>R3</kbd> opens settings and <kbd>L3</kbd> + <kbd>Menu</kbd> shows the shortcut sheet. <a href="guide.html#start">Full setup, update and removal steps</a>.</p></section>
<section><h2>Features</h2><ul>
<li><strong>Stereo VR with head tracking</strong>, using the game's own HUD and menus.</li>
<li><strong>Xbox controls</strong> with an in-VR shortcut sheet and an adjustable HUD.</li>
<li><strong>First person</strong> with the body visible and the head hidden, plus a comfort mode.</li>
<li><strong>Freecam</strong>: fly, drone, plane and acro cameras.</li>
<li><strong>Optional 6DOF window</strong>, adapted from Elliott Tate's work.</li>
<li><strong>Matching eyes</strong>: distant trees, props and lighting, and character-screen reflections, look the same in both eyes.</li>
<li><strong>Recording</strong> of side-by-side stereo video from the launcher.</li>
</ul><a href="guide.html#visual-controls">See the controller guide</a></section>
<section><h2>Credits</h2><p>Built on work by praydog and the UEVR contributors, mirudo2, polar, SannpoKun, Indath, endlessfalls and Elliott Tate. <a href="credits.html">Contributions and licenses</a>.</p><p>Forks and improvements are welcome where the licenses allow. Please keep it free and credit the creators. <a href="license.html">License and sharing</a>.</p></section>`);

  // Player guide
  const chapters = [['start', 'Setup', 'docs/START-HERE.md'], ['controls', 'All Xbox shortcuts', 'docs/CONTROLS.md'],
    ['comfort', 'Comfort and settings', 'docs/COMFORT.md'], ['recovery', 'Problems and recovery', 'docs/TROUBLESHOOTING.md']];
  const filter = '<div class="control-filter" hidden><label for="control-search">FIND A SHORTCUT</label><input id="control-search" type="search" placeholder="HUD, camera, acro, L3…" autocomplete="off"><button id="clear-search" class="reset-button" type="button">Clear</button></div><p id="control-count" role="status" aria-live="polite"></p>';
  const sheets = [['everyday', 'Everyday shortcuts', 'L3 + R3 opens settings; L3 + B shows or hides the game UI; L3 + A recenters; L3 + Menu toggles this sheet.'],
    ['camera', 'Camera controls', 'The camera page changes with the freecam style. This image shows Polar fly; the text below also covers drone, plane and acro.'],
    ['hud', 'HUD and mouse adjustment', 'Enable Xbox mouse shortcuts, close UEVR, press L3 + LB once and release. This image shows adjustment on; press L3 + LB again for normal game controls.'],
    ['uevr', 'Using UEVR settings', 'Open UEVR with L3 + R3 first. Release RT, then use the D-pad and A; the left stick scrolls the focused pane.']];
  const sheetImages = `<section id="visual-controls"><h2>Controller guide</h2><p>Renders of the in-VR shortcut sheet (L3 + Menu). Open an image at full size, or search the text reference below.</p>${sheets.map(([id, title, caption]) =>
    `<figure><h3>${title}</h3><a href="media/shortcuts-${id}.png" aria-label="Open ${title.toLowerCase()} at full size"><img src="media/shortcuts-${id}.png" width="1600" height="900" loading="lazy" alt="${escape(title)}. ${escape(caption)}"></a><figcaption>${escape(caption)}</figcaption></figure>`).join('')}</section>`;
  const toc = [['start', 'Setup'], ['visual-controls', 'Controller images'], ...chapters.slice(1).map(([id, title]) => [id, title])];
  write('guide.html', 'Player guide', 'Setup, illustrated Xbox controls, comfort settings and recovery for WuWa VR.',
`<h1>Player guide</h1><p>Install, control and fix WuWa VR. Current download: <a href="${releaseUrl}">${escape(status.build)}</a>, for game version ${escape(status.game)}.</p><button class="print-button" id="print-guide" type="button" hidden>Print / save this guide</button><div class="guide-grid"><nav class="toc" aria-label="Guide chapters">${toc.map(([id, title]) => `<a href="#${id}">${title}</a>`).join('')}</nav><div class="guide-content">${chapters.map(([id, title, file], i) =>
    `<section id="${id}"><h2>${title}</h2>${id === 'controls' ? filter : ''}${id === 'start' ? withVisualSteps(markdown(file)) : markdown(file, {controls: id === 'controls'})}</section>${i === 0 ? sheetImages : ''}`).join('')}</div></div>`);

  // Explainer
  let explainer = markdown('docs/UNDERSTANDING-WUWA-VR.md', {demote: false});
  const headings = [];
  explainer = explainer.replace(/<h2>(.*?)<\/h2>/g, (_, t) => {
    const id = t.toLowerCase().replace(/<[^>]*>/g, '').replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '');
    headings.push([id, t]);
    return `<h2 id="${id}">${t}</h2>`;
  });
  const explainerVideo = `<figure><video controls preload="metadata" poster="media/explainer-poster.png" style="width:100%;aspect-ratio:16/9"><source src="media/explainer-60s.mp4?v=20260928-headset" type="video/mp4"><track kind="captions" src="media/explainer-captions.vtt?v=20260928-headset" srclang="en" label="English"><a href="media/explainer-60s.mp4?v=20260928-headset">Download the 60-second explainer</a></video><figcaption>A 60-second visual overview from 28 September, before the game 3.7 update and the October stereo fixes; the page below is current. <a href="media/explainer-transcript.md">Transcript</a>.</figcaption></figure>`;
  write('understanding.html', 'Understand WuWa VR', 'How WuWa VR works, the stereo bugs it fixes and a map of the code.',
`<h1>Understand WuWa VR</h1><p class="intro">How the mod works, what was broken in stereo and why, and where to find things in the code.</p>${explainerVideo}<details open><summary>Jump to a section</summary><ul>${headings.map(([id, t]) => `<li><a href="#${id}">${t}</a></li>`).join('')}</ul></details><article class="guide-content">${explainer}</article>`);

  // Developers
  write('developers.html', 'Code and contributions', 'Source layout, building and contributing to WuWa VR.',
`<h1>Code and contributions</h1><nav class="page-links" aria-label="GitHub"><a href="${repo}">Browse GitHub</a><a href="${repo}/fork">Fork the repository</a><a href="${repo}/issues">Search issues</a><a href="feedback.html">Prepare a report</a></nav><div class="guide-content">${markdown('docs/DEVELOPING.md')}<section id="contributing"><h2>Contributing</h2>${markdown('CONTRIBUTING.md')}</section></div>`);

  // Credits, support, risk
  write('credits.html', 'Credits', 'Contributors and component-specific licensing.', `<h1>Credits</h1><div class="guide-content">${markdown('CREDITS.md', {demote: false})}</div>`);
  write('support.html', 'Optional support', 'Optional support for a spare-time hobby, without promised maintenance.',
    `<h1>Optional support</h1><div class="guide-content">${markdown('SUPPORT.md', {demote: false})}<p><a class="button" href="https://ko-fi.com/chronohax" rel="noopener noreferrer">Support on Ko-fi</a></p><p class="small">You will continue to Ko-fi. This website collects no payment details.</p></div>`);
  write('risk.html', 'Account risk and disclaimer', 'Unofficial modification, anti-cheat and account restrictions.',
    `<h1>Account risk and disclaimer</h1><div class="guide-content">${markdown('docs/RISK.md', {demote: false})}</div>`);

  // License
  write('license.html', 'License and sharing', 'What our MIT license covers and what it does not.',
`<h1>License and sharing</h1><p class="intro">This is a free, unofficial fan project. Our original independent work uses MIT; each upstream component keeps its own terms.</p>
<section><h2>What our MIT license covers</h2><p>Original text in the README, player docs and support, contribution and credit pages; original website HTML, CSS, JavaScript and translations; the abstract mark; and the site generators and checks. Quoted third-party material is excluded. Keep copyright and license notices.</p>
<h2>What it does not cover</h2><p>The UEVR backend, native adaptations and patches, community scripts, game code, assets, screenshots and trademarks, and other owners' work. The generated controller illustration is not an official Xbox asset. The mod as a whole is <strong>not</strong> MIT. See <a href="credits.html">component credits</a>; a published download does not change component terms or imply upstream endorsement.</p>
<h2>A community request</h2><p>Please keep access free, credit creators, share improvements and avoid paid repacks or paywalled forks. This is a request, not an extra MIT condition: MIT permits commercial use and does not require forks to publish source.</p></section>
<section id="mit"><h2>MIT license text</h2><pre class="license-text">${escape(read('LICENSES/MIT.md'))}</pre></section>`);

  // Feedback
  const form = {
    pending: 'No destination is connected. Copy your draft; nothing has been sent.',
    ready: 'Your draft is ready. You choose whether to open GitHub and submit it.',
    long: 'This draft is too long for a reliable link. Copy it, open GitHub, then paste it into the issue.',
    copied: 'Report copied. Nothing has been submitted.',
    fallback: 'Select and copy the draft below. Nothing has been submitted.',
    empty: 'Add a short summary and description first.'};
  write('feedback.html', 'Feedback and requests', 'Report a bug, suggest a feature or request a language.',
`<h1>Feedback and requests</h1><p class="intro">Report a bug, suggest a feature or request a language, in whichever language you prefer.</p>
<section id="feedback"><h2>Prepare a request</h2><div class="report-intro"><p>Prepare a report here, review it on GitHub, then submit it yourself; this site never posts anything. Submitting needs a GitHub account; copying the draft does not.</p><p><a href="${repo}/issues">Search existing issues</a> first.</p></div>
<form class="report-form" id="report-form" hidden data-lang="en" data-pending="${escape(form.pending)}" data-ready="${escape(form.ready)}" data-long="${escape(form.long)}" data-copied="${escape(form.copied)}" data-fallback="${escape(form.fallback)}" data-empty="${escape(form.empty)}">
<label for="report-kind">What would you like to send?</label><select id="report-kind" name="kind"><option value="bug">Bug report</option><option value="feature">Feature or accessibility request</option><option value="language">Language or translation request</option></select>
<label for="report-title">Short summary</label><input id="report-title" name="title" required maxlength="140" dir="auto" autocomplete="off">
<label for="report-details">What happened, or what would help?</label><textarea id="report-details" name="details" rows="5" required maxlength="10000" dir="auto" aria-describedby="report-privacy"></textarea>
<label for="report-version">Build, headset and runtime (optional)</label><input id="report-version" name="version" maxlength="300" dir="auto" autocomplete="off">
<label for="report-language">Requested language or translation correction (optional)</label><input id="report-language" name="language" maxlength="160" dir="auto" autocomplete="off">
<p class="small" id="report-privacy">Reports are public on GitHub. Leave out account IDs, personal paths, passwords and unreviewed logs.</p><button type="submit">Prepare report</button>
</form><section class="report-preview" id="report-preview" hidden><h3 id="draft-heading" tabindex="-1">Your draft: review before sharing</h3><textarea id="report-draft" rows="12" readonly dir="auto" aria-labelledby="draft-heading"></textarea><p id="report-status" role="status" aria-live="polite"></p><div class="actions"><a id="issue-link" class="button" hidden target="_blank" rel="noopener noreferrer">Review on GitHub</a><button id="copy-report" type="button">Copy report</button></div></section>
<noscript><p>This form needs JavaScript. You can also <a href="${repo}/issues/new/choose">open a GitHub issue</a> directly.</p></noscript></section>
<p>Maintained in spare time; replies and fixes are not guaranteed. <a href="support.html">Support expectations</a>.</p>`);

  // Languages
  const {languages} = require('./site-layout.cjs');
  write('languages.html', 'Languages', 'The WuWa VR website in ten languages.',
`<h1>Languages</h1><p class="intro">The whole site is available in ten languages. The game, UEVR's own menus and some build descriptions stay in their original language.</p><div class="language-grid">${languages.map(([code, name]) =>
    `<a class="language-card" href="${code === 'en' ? 'index.html' : `l/${code}/index.html`}" lang="${code}" hreflang="${code}" dir="${code === 'ar' ? 'rtl' : 'ltr'}" data-language="${code}"><strong>${escape(name)}</strong></a>`).join('')}</div><p>Translations are machine-assisted drafts; corrections are welcome. <a href="feedback.html?kind=language">Correct a translation or request a language</a>.</p>`);

  // The old test-notes page moved; keep its URL working.
  write('testing.html', 'Page moved', 'This page has moved.', `<h1>This page has moved</h1><p>Build and testing notes now live in the <a href="guide.html">player guide</a> and the <a href="understanding.html">explainer</a>. Every download is listed on <a href="${repo}/releases">GitHub Releases</a>.</p>`);

  // Keep the README's download line in step with the site.
  const readme = read('README.md');
  const block = `<!-- status:start (generated by dev/build-site.cjs from dev/site-status.json) -->\n**[Download the beta · ${status.label} · game ${status.game}](${releaseUrl})** · [Website and player guide](https://chronohaxx.github.io/wuwa-vr/) · [Report an issue](${repo}/issues)\n\n${status.news}\n<!-- status:end -->`;
  if (!/<!-- status:start[\s\S]*?<!-- status:end -->/.test(readme)) throw Error('README.md needs <!-- status:start --> … <!-- status:end --> markers');
  fs.writeFileSync(path.join(root, 'README.md'), readme.replace(/<!-- status:start[\s\S]*?<!-- status:end -->/, block));

  console.log('Built the English site for', status.tag);
})().catch(e => { console.error(e); process.exitCode = 1; });
