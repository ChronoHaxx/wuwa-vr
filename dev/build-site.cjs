// Original independent tooling: MIT. Uses marked only when regenerating pages.
const fs=require('node:fs'),path=require('node:path');
const {shell,escape}=require('./site-layout.cjs');
const root=path.resolve(__dirname,'..'),site=path.join(root,'site');
// Head-only maintenance preserves the published shell/body and protected files.
if (process.argv.includes('--metadata-only')) {
  console.log(`Refreshed metadata for ${require('./site-metadata.cjs').apply(site)} public content pages.`);
  return;
}
// Refresh the reviewed published pages without regenerating unrelated legacy content.
if (process.argv.includes('--launcher-release')) {
  require('./i18n/build-launcher-release.cjs').build().catch(e=>{console.error(e);process.exitCode=1;});
  return;
}
const candidates=[root,process.env.WUWA_NODE_MODULES,path.join(root,'dev-tools/node_modules'),path.join(process.env.USERPROFILE||process.env.HOME||'','.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules')].filter(Boolean);
(async()=>{
  const {marked}=await import(require('node:url').pathToFileURL(require.resolve('marked',{paths:candidates})).href);
  const links={'START-HERE.md':'guide.html#start','CONTROLS.md':'guide.html#controls','COMFORT.md':'guide.html#comfort',
    'TROUBLESHOOTING.md':'guide.html#recovery','NEXT-SESSION.md':'testing.html#next-test','CHECKPOINTS.md':'testing.html#checkpoints',
    'RELEASING.md':'credits.html#distribution','CREDITS.md':'credits.html','SUPPORT.md':'support.html','LICENSE.md':'license.html',
    'MIT.md':'license.html#mit','RISK.md':'risk.html','DISTRIBUTION-PLAN.md':'guide.html#packaging',
    'DEVELOPING.md':'developers.html','CONTRIBUTING.md':'developers.html#contributing'};
  function render(file,controls=false){
    let html=marked.parse(fs.readFileSync(path.join(root,file),'utf8').replace(/^# .+\r?\n/,''),{gfm:true});
    html=html.replace(/href="([^"#]+\.md)(#[^"]*)?"/g,(_,target,anchor)=>{
      if(/^https?:\/\//i.test(target))return 'href="'+target+(anchor||'')+'"';
      const next=links[path.basename(target)];if(!next)throw Error('Unmapped link '+target);
      return `href="${next}${anchor&&!next.includes('#')?anchor:''}"`;
    }).replace('href="site/support.html"','href="support.html"');
    html=html.replace(/<table>/g,'<div class="table-scroll"><table>').replace(/<\/table>/g,'</table></div>').replace(/<(\/?)h2>/g,'<$1h3>');
    if(controls)html=html.replace(/<tbody>([\s\S]*?)<\/tbody>/g,(_,body)=>'<tbody>'+body.replace(/<tr>/g,'<tr data-control-row>')+'</tbody>');
    return html;
  }
  const write=(file,title,description,body)=>fs.writeFileSync(path.join(site,file),shell(title,description,body));
  // Only owner-approved gameplay clips are shown; nothing appears until one exists.
  const privateProvenance=path.join(root,'release/media-provenance.json');
  const provenance=JSON.parse(fs.readFileSync(fs.existsSync(privateProvenance)?privateProvenance:path.join(root,'release/site-media.json'),'utf8'));
  // The public source export must regenerate without private capture paths.
  const publicMedia=(provenance.media||[]).filter(m=>m.kind==='gameplay-video'&&m.ownerApproved===true).map(m=>Object.fromEntries(['kind','ownerApproved','file','poster','caption','build','recorded','headset'].filter(k=>m[k]!==undefined).map(k=>[k,m[k]])));
  fs.writeFileSync(path.join(root,'release/site-media.json'),JSON.stringify({schema:1,media:publicMedia},null,2)+'\n');
  const chapters=[['start','Setup','docs/START-HERE.md'],['controls','All Xbox shortcuts','docs/CONTROLS.md'],['comfort','HUD and camera comfort','docs/COMFORT.md'],['recovery','Problems and recovery','docs/TROUBLESHOOTING.md']];
  const filter='<div class="control-filter" hidden><label for="control-search">FIND A SHORTCUT</label><input id="control-search" type="search" placeholder="HUD, camera, acro, L3…" autocomplete="off"><button id="clear-search" class="reset-button" type="button">Clear</button></div><p id="control-count" role="status" aria-live="polite"></p>';
  const sheets=[['everyday','Everyday shortcuts','L3 + R3 opens settings; L3 + B shows or hides the game UI; L3 + A recenters; L3 + Menu toggles this sheet.'],['camera','Camera controls','The camera sheet changes with the active movement mode. This image shows Polar fly; the text reference below includes drone, plane and acro.'],['hud','HUD and mouse adjustment','Enable controller mouse shortcuts, close UEVR, then press L3 + LB once and release. This image shows adjustment ON; press L3 + LB again for normal game controls.'],['uevr','Using UEVR settings','Open UEVR with L3 + R3 first. Release RT, then use the D-pad and A; the left stick scrolls the focused pane.']];
  const visual=`<section id="visual-controls"><h2>Controller guide</h2><p>Actual renders of the supplied in-VR shortcut sheet, not game screenshots. The new HUD toggle is a candidate awaiting headset checks. Open an image at full size or use the searchable text below.</p>${sheets.map(([id,title,caption])=>`<figure><h3>${title}</h3><a href="media/shortcuts-${id}.png" aria-label="Open ${title.toLowerCase()} at full size"><img src="media/shortcuts-${id}.png" width="1600" height="900" loading="lazy" alt="${escape(title)}. ${escape(caption)}"></a><figcaption>${escape(caption)}</figcaption></figure>`).join('')}</section>`;
  const toc=[['start','Setup'],['visual-controls','Controller images'],...chapters.slice(1).map(([id,title])=>[id,title]),['packaging','Portable launcher']];
  write('guide.html','Player guide','Visual Xbox controls, setup, comfort and recovery for WuWa VR.',`<h1>Player guide</h1><p>Setup for the portable beta, illustrated controls and ways to recover when a setting feels wrong. <a href="languages.html">Getting started in 10 languages</a>.</p><button class="print-button" id="print-guide" type="button" hidden>Print / save this guide</button><div class="guide-grid"><nav class="toc" aria-label="Guide chapters">${toc.map(([id,title])=>`<a href="#${id}">${title}</a>`).join('')}</nav><div class="guide-content">${chapters.map(([id,title,file],i)=>`<section id="${id}"><h2>${title}</h2>${id==='controls'?filter:''}${id==='start'?render(file):render(file,id==='controls')}</section>${i===0?visual:''}`).join('')}<section id="packaging"><h2>Portable launcher</h2><p>The private test package is a ZIP with a folder and <strong>WuWa VR Launcher.exe</strong>. It needs no Python, terminal or GitHub account: it carries its own copy of Python and opens a page served only on your PC. Settings, backups and logs are kept in <code>%LOCALAPPDATA%\\WuWa VR Launcher</code>.</p><p>It is not a public release. The UEVR backend's upstream notice is "All rights reserved", so public downloads wait for permission. Game injection still needs Windows permission because the game runs as administrator. The launcher leaves game files alone. Use headset / Use simulator changes the global OpenXR runtime only when you click it, with the game and injector closed; Windows asks for permission. Use headset restores the previously saved runtime. Do not disable Windows security or anti-cheat. <a href="credits.html#distribution">Distribution status</a>.</p></section></div></div>`);
  write('testing.html','Local test notes','Local checkpoints and next-session comparisons; not public release acceptance.',`<h1>Local test notes</h1><p>For the prepared development installation. These notes do not establish public release or headset acceptance.</p><div class="guide-content"><section id="next-test"><h2>Next session</h2>${render('docs/NEXT-SESSION.md')}</section><section id="checkpoints"><h2>Checkpoint identities</h2>${render('docs/CHECKPOINTS.md')}</section></div>`);
  write('credits.html','Credits','Contributors and component-specific licensing.',`<h1>Credits</h1><div class="guide-content">${render('CREDITS.md')}<section id="distribution"><h2>Distribution status</h2><p>No public combined-mod binary is available yet. A private launcher package exists for local testing only; it keeps each component's notices and is not for re-sharing. Upstream redistribution permissions and a clean installation on another PC still need review. A scoped MIT license for our website and guides does not relicense the backend or game content.</p></section></div>`);
  // Read the maintained support text so the important disclaimer cannot drift.
  write('support.html','Optional support','Optional support for a spare-time hobby, without promised maintenance.',`<h1>Optional support</h1><div class="guide-content">${render('SUPPORT.md')}<p id="support-pending" class="notice">No payment details are collected on this site.</p><a id="support-link" class="button" rel="noopener noreferrer" hidden>Support the projects</a><p id="support-external" class="small" hidden>You will continue to an external payment provider. This website does not collect payment details.</p></div>`);
  write('risk.html','Account risk and disclaimer','Unofficial modification, anti-cheat and account restrictions.',`<h1>Account risk and disclaimer</h1><div class="guide-content">${render('docs/RISK.md')}</div>`);
  write('developers.html','Code and contributions','Source map, known rendering problems and ways to contribute to WuWa VR.',`<h1>Code and contributions</h1><nav class="page-links" aria-label="GitHub collaboration"><a data-project-link="code" hidden rel="noopener noreferrer">Browse GitHub</a><a data-project-link="fork" hidden rel="noopener noreferrer">Fork the repository</a><a data-project-link="issues" hidden rel="noopener noreferrer">Search issues</a><a href="feedback.html">Prepare a report</a></nav><p class="notice" data-repository-pending>The public repository has not been connected yet. The source map below explains the current review bundle; feedback drafts can still be copied.</p><div class="guide-content">${render('docs/DEVELOPING.md')}<section id="contributing"><h2>Contributing</h2>${render('CONTRIBUTING.md')}</section></div>`);
  require('./build-community.cjs');
  await require('./i18n/build-launcher-release.cjs').build();
  console.log('Generated guide, risk/support pages and local test notes; home pages come from build-home.');
})().catch(e=>{console.error(e);process.exitCode=1;});

// The published experimental banner is maintained in site/; the deep guide has its own generator.
