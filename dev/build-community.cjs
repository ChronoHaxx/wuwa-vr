// Original site tooling, MIT; see LICENSE.md for component boundaries.
// Node standard library only. Output remains usable without JavaScript.
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const {shell,escape:esc}=require('./site-layout.cjs'),{report}=require('./site-report.cjs');
// Home pages, l/*.html and languages.html now come from dev/build-home.cjs.
const root=path.resolve(__dirname,'..'),site=path.join(root,'site');
const codes=['en','zh-Hans','ja','ko','es','pt-BR','fr','de','ru','ar'];
const all=codes.map(code=>require(path.join(site,'languages',code+'.js'))),en=all[0];
for(const t of all){
  assert.deepEqual(Object.keys(t).sort(),Object.keys(en).sort(),'Missing locale keys: '+t.lang);
  for(const key of Object.keys(en)){
    if(Array.isArray(en[key]))assert.equal(t[key].length,en[key].length,t.lang+': '+key);
    else assert.equal(typeof t[key],'string',t.lang+': '+key);
    assert(!JSON.stringify(t[key]).includes('<'),t.lang+': translations are text, not markup');
  }
}
const write=(file,title,body)=>fs.writeFileSync(path.join(site,file),shell(title,title+' for the WuWa VR fan project.',body));
write('feedback.html','Feedback and requests',`<h1>Feedback and requests</h1><p class="intro">Report a bug, suggest a feature or request a language. Write in whichever language you prefer; no donation is required.</p><a href="languages.html">Use translated form labels</a><section id="feedback"><h2>Prepare a request</h2>${report(en)}</section><p>Maintained in spare time. Replies, fixes and continued support are not guaranteed. <a href="support.html">Support expectations</a>.</p>`);
const mit=fs.readFileSync(path.join(root,'LICENSES/MIT.md'),'utf8');
write('license.html','License and sharing',`<h1>License and sharing</h1><p class="intro">This is a free, unofficial fan project. Our original independent work uses MIT. Each upstream component keeps its own terms.</p><section><h2>What our MIT grant covers</h2><p>Original text in the README, support/contribution/credit pages and player docs; original website HTML, CSS, JavaScript and translations; the abstract mark; the site generators and checks; original issue templates and website export README/workflow. Quoted third-party material is excluded. Preserve copyright and license notices.</p><h2>What it does not cover</h2><p>UEVR backend, native adaptations/patches, community scripts and adaptations, game code/assets/screenshots/trademarks, and other owners' work. The generated controller illustration is not an official Xbox asset or an assertion of exclusive copyright. The whole mod is <strong>not</strong> MIT. See <a href="credits.html">component credits</a>; public combined-mod redistribution remains pending clarification.</p><h2>A community request</h2><p>${esc(en.sharing)} MIT does not require forks to publish source. We encourage freely accessible improvements and contributions where each component's terms allow.</p><h2>Optional support</h2><p>${esc(en.supportText)}</p></section><section id="mit"><h2>MIT license text</h2><pre class="license-text">${esc(mit)}</pre></section>`);
console.log('Checked 10 locale string sets; generated feedback and license pages.');
