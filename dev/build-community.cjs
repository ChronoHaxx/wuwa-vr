// Original site tooling, MIT; see LICENSE.md for component boundaries.
// Node standard library only. Output remains usable without JavaScript.
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const {shell,escape:esc,languageLinks}=require('./site-layout.cjs');
const root=path.resolve(__dirname,'..'),site=path.join(root,'site');
const codes=['en','zh-Hans','ja','ko','es','pt-BR','fr','de','ru','ar'];
const all=codes.map(code=>require(path.join(site,'languages',code+'.js'))),en=all[0];
const keys=['L3 + R3','L3 + B','L3 + A','L3 + Menu','L3 + LB','L3 + View','Double R3','L3 + RB'];
for(const t of all){
  assert.deepEqual(Object.keys(t).sort(),Object.keys(en).sort(),'Missing locale keys: '+t.lang);
  for(const key of Object.keys(en)){
    if(Array.isArray(en[key]))assert.equal(t[key].length,en[key].length,t.lang+': '+key);
    else assert.equal(typeof t[key],'string',t.lang+': '+key);
    assert(!JSON.stringify(t[key]).includes('<'),t.lang+': translations are text, not markup');
  }
}
function report(t){
  return `<div class="report-intro"><p>${esc(t.reportIntro)}</p><p class="notice">${esc(t.pending)}</p><p lang="en"><a data-project-link="issues" hidden rel="noopener noreferrer">Search existing GitHub issues</a></p></div>
<form class="report-form" id="report-form" hidden data-lang="${t.lang}" data-pending="${esc(t.pending)}" data-ready="${esc(t.ready)}" data-long="${esc(t.tooLong)}" data-copied="${esc(t.copied)}" data-fallback="${esc(t.copyFallback)}" data-empty="${esc(t.empty)}">
<label for="report-kind">${esc(t.kind)}</label><select id="report-kind" name="kind">${['bug','feature','language'].map((id,i)=>`<option value="${id}">${esc(t.kinds[i])}</option>`).join('')}</select>
<label for="report-title">${esc(t.titleLabel)}</label><input id="report-title" name="title" required maxlength="140" dir="auto" autocomplete="off">
<label for="report-details">${esc(t.detailsLabel)}</label><textarea id="report-details" name="details" rows="5" required maxlength="10000" dir="auto" aria-describedby="report-privacy"></textarea>
<label for="report-version">${esc(t.versionLabel)}</label><input id="report-version" name="version" maxlength="300" dir="auto" autocomplete="off">
<label for="report-language">${esc(t.languageLabel)}</label><input id="report-language" name="language" maxlength="160" dir="auto" autocomplete="off">
<p class="small" id="report-privacy">${esc(t.privacy)}</p><button type="submit">${esc(t.prepare)}</button>
</form><section class="report-preview" id="report-preview" hidden><h3 id="draft-heading" tabindex="-1">${esc(t.preview)}</h3><textarea id="report-draft" rows="12" readonly dir="auto" aria-labelledby="draft-heading"></textarea><p id="report-status" role="status" aria-live="polite"></p><div class="actions"><a id="issue-link" class="button" hidden target="_blank" rel="noopener noreferrer">${esc(t.openIssue)}</a><button id="copy-report" type="button">${esc(t.copy)}</button></div></section>
<noscript><p>${esc(t.privacy)}</p><p>${esc(t.pending)}</p><p>${esc(t.titleLabel)} · ${esc(t.detailsLabel)} · ${esc(t.versionLabel)} · ${esc(t.languageLabel)}</p></noscript>`;
}
fs.mkdirSync(path.join(site,'l'),{recursive:true});
for(const t of all){
  const body=`<h1>${esc(t.title)}</h1><p class="intro">${esc(t.intro)}</p><p class="notice">${esc(t.scope)}</p>${languageLinks('../')}<aside class="notice risk"><strong>${esc(t.riskTitle)}</strong> ${esc(t.riskText)} <a href="../risk.html" lang="en">Full account-risk notice (English)</a></aside><div class="guide-content locale-guide"><section id="start"><h2>${esc(t.start)}</h2><ol class="steps">${t.steps.map(v=>`<li>${esc(v)}</li>`).join('')}</ol></section><section id="controls"><h2>${esc(t.controls)}</h2><p>${esc(t.keyIntro)}</p><div class="table-scroll"><table><tbody>${keys.map((key,i)=>`<tr><th scope="row"><bdi dir="ltr">${esc(key)}</bdi></th><td>${esc(t.keyActions[i])}</td></tr>`).join('')}</tbody></table></div><a href="../guide.html#visual-controls" hreflang="en">${esc(t.detailed)}</a></section><section id="comfort"><h2>${esc(t.comfort)}</h2><p>${esc(t.comfortText)}</p><h3>${esc(t.limits)}</h3><p>${esc(t.limitsText)}</p></section><section id="community"><h2>${esc(t.license)}</h2><p>${esc(t.licenseText)}</p><p>${esc(t.sharing)}</p><a href="../credits.html" hreflang="en">${esc(t.credits)}</a></section><section id="support"><h2>${esc(t.support)}</h2><p>${esc(t.supportText)}</p><a href="../support.html" hreflang="en">${esc(t.supportLink)}</a></section><section id="feedback"><h2>${esc(t.feedback)}</h2><p>${esc(t.review)}</p>${report(t)}</section></div>`;
  fs.writeFileSync(path.join(site,'l',t.lang+'.html'),shell(t.title,t.intro,body,{lang:t.lang,dir:t.dir,prefix:'../',labels:{guide:t.detailed,feedback:t.feedback,credits:t.credits,language:t.choose,skip:t.start}}));
}
const write=(file,title,body)=>fs.writeFileSync(path.join(site,file),shell(title,title+' for the WuWa VR fan project.',body));
write('languages.html','Languages',`<h1>Languages</h1><p class="intro">Setup, everyday Xbox controls and feedback in ten languages.</p><div class="language-grid">${all.map(t=>`<a class="language-card" href="l/${t.lang}.html" lang="${t.lang}" hreflang="${t.lang}" dir="${t.dir}" data-language="${t.lang}"><strong>${esc(t.name)}</strong><span>${esc(t.title)}</span></a>`).join('')}</div><p>These are initial translations. The full technical reference and native UEVR menu are still English. Community language review is welcome.</p><a href="feedback.html?kind=language">Request a language or correct a translation</a>`);
write('feedback.html','Feedback and requests',`<h1>Feedback and requests</h1><p class="intro">Report a bug, suggest a feature or request a language. Write in whichever language you prefer; no donation is required.</p><a href="languages.html">Use translated form labels</a><section id="feedback"><h2>Prepare a request</h2>${report(en)}</section><p>Maintained in spare time. Replies, fixes and continued support are not guaranteed. <a href="support.html">Support expectations</a>.</p>`);
const mit=fs.readFileSync(path.join(root,'LICENSES/MIT.md'),'utf8');
write('license.html','License and sharing',`<h1>License and sharing</h1><p class="intro">This is a free, unofficial fan project. Our original independent work uses MIT. Each upstream component keeps its own terms.</p><section><h2>What our MIT grant covers</h2><p>Original text in the README, support/contribution/credit pages and player docs; original website HTML, CSS, JavaScript and translations; the abstract mark; the site generators and checks; original issue templates and website export README/workflow. Quoted third-party material is excluded. Preserve copyright and license notices.</p><h2>What it does not cover</h2><p>UEVR backend, native adaptations/patches, community scripts and adaptations, game code/assets/screenshots/trademarks, and other owners' work. The generated controller illustration is not an official Xbox asset or an assertion of exclusive copyright. The whole mod is <strong>not</strong> MIT. See <a href="credits.html">component credits</a>; public combined-mod redistribution remains pending clarification.</p><h2>A community request</h2><p>${esc(en.sharing)} MIT does not require forks to publish source. We encourage freely accessible improvements and contributions where each component's terms allow.</p><h2>Optional support</h2><p>${esc(en.supportText)}</p></section><section id="mit"><h2>MIT license text</h2><pre class="license-text">${esc(mit)}</pre></section>`);
console.log('Generated 10 localized starter pages, feedback, languages and license pages.');
