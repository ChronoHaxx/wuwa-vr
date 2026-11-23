// Real headless browser checks against only a loopback static copy of site/.
const fs=require('node:fs'),path=require('node:path'),http=require('node:http'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..'), site=path.join(root,'site');
const release=JSON.parse(fs.readFileSync(path.join(root,'dev/site-status.json'),'utf8'));
const modules=process.env.WUWA_NODE_MODULES || path.join(process.env.USERPROFILE||process.env.HOME||'','.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
const {chromium}=require(require.resolve('playwright',{paths:[root,modules]}));
const output=path.resolve(process.argv[2] || path.join(root,'extracted/presentation-20260925/browser'));
fs.mkdirSync(output,{recursive:true});
const server=http.createServer((req,res)=>{
  const route=decodeURIComponent(new URL(req.url,'http://localhost').pathname);
  if(!route.startsWith('/wuwa-vr/')){res.writeHead(404);res.end();return;}
  const file=path.resolve(site,route.slice('/wuwa-vr/'.length)||'index.html');
  if(!file.startsWith(site+path.sep)||!fs.existsSync(file)||!fs.statSync(file).isFile()){res.writeHead(404);res.end();return;}
  const types={'.html':'text/html; charset=utf-8','.js':'text/javascript; charset=utf-8','.css':'text/css; charset=utf-8','.png':'image/png','.svg':'image/svg+xml','.webp':'image/webp','.jpg':'image/jpeg','.mp4':'video/mp4','.woff2':'font/woff2'};
  res.writeHead(200,{'Content-Type':types[path.extname(file)]||'text/plain'});res.end(fs.readFileSync(file));
});
(async()=>{
  await new Promise(r=>server.listen(0,'127.0.0.1',r));
  const base=`http://127.0.0.1:${server.address().port}/wuwa-vr/`;
  const browser=await chromium.launch({headless:true,...(process.env.WUWA_CHROMIUM?{executablePath:process.env.WUWA_CHROMIUM}:{})});
  const errors=[], checks=[];
  try{
    const page=await browser.newPage({viewport:{width:1440,height:1000},deviceScaleFactor:1});
    page.on('pageerror',e=>errors.push(String(e)));
    page.on('response',r=>{if(r.status()>=400)errors.push(`${r.status()} ${r.url()}`);});
    const guides=['guide.html',...['zh-Hans','ja','ko','es','pt-BR','fr','de','ru','ar'].map(code=>`l/${code}-guide.html`)];
    // Nothing may contact YouTube before a visitor chooses a video.
    const youtube=[];page.on('request',r=>{if(/youtube|ytimg|googlevideo/.test(r.url()))youtube.push(r.url());});
    for(const file of ['index.html',...guides,'reference.html','record.html','credits.html','support.html','risk.html','license.html','languages.html','feedback.html','testing.html','developers.html','privacy.html','l/ar-privacy.html','l/ja-privacy.html']){
      await page.goto(base+file);await page.locator('h1').waitFor();
      assert.equal(await page.locator('h1').count(),1);
      assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'Desktop page overflows: '+file);
      for(const img of await page.locator('img').all()){await img.evaluate(i=>{i.loading='eager';return i.decode();});}
      const bad=await page.evaluate(()=>[...document.images].filter(i=>!i.complete||!i.naturalWidth).map(i=>i.src));assert.deepEqual(bad,[]);
      const targets=await page.locator('a[href]').evaluateAll(nodes=>nodes.map(a=>a.getAttribute('href')));
      for(const target of targets){
        if(/^(https?:|mailto:)/.test(target))continue;
        const parsed=new URL(target,base+file),local=path.join(site,parsed.pathname.slice('/wuwa-vr/'.length));
        assert(fs.existsSync(local),'Broken local link '+target);
        if(parsed.hash){const html=fs.readFileSync(local,'utf8');assert(html.includes(`id="${decodeURIComponent(parsed.hash.slice(1))}"`),'Missing anchor '+target);}
      }
      if(['index.html','support.html','guide.html','reference.html'].includes(file))await page.screenshot({path:path.join(output,'desktop-'+file.replace('.html','.png')),fullPage:true});
      checks.push('desktop links, assets and overflow: '+file);
    }
    assert.deepEqual(youtube,[],'A page contacted YouTube before any video was chosen');
    checks.push('no request to YouTube on any page until a video is chosen');
    // Each language's guide: its own language and direction, every section, the controls card and tables.
    for(const file of guides){
      await page.goto(base+file);
      const code=file==='guide.html'?'en':file.slice(2,-11);
      assert.equal(await page.evaluate(()=>document.documentElement.lang),code,file);
      assert.equal(await page.evaluate(()=>document.documentElement.dir),code==='ar'?'rtl':'ltr',file);
      for(const id of ['start','controls','updates','recovery','recording'])assert.equal(await page.locator('#'+id).count(),1,`${file} lacks #${id}`);
      assert(await page.locator('#controls .pad-card').count()===1&&await page.locator('#controls table.shortcut-table').count()>=5,file+': controls card or tables missing');
      assert(!/\b0[123] ·|step 0[123]/i.test(await page.locator('main').innerText()),file+': old numbered launcher steps');
      assert.equal(await page.locator('a[href*="reference.html"]').count()>0,true,file+': no link to the reference');
    }
    checks.push('player guide in 10 languages: lang/dir, five sections, controls card and tables, no old step numbers, reference link');
    // Watch: posters only; spoilers closed; one chosen card becomes the privacy-enhanced player, the others stay posters.
    await page.route(/youtube-nocookie\.com/,r=>r.fulfill({contentType:'text/html',body:'<title>player</title>'}));
    await page.goto(base+'index.html#watch');
    assert(!(await page.locator('#watch .spoiler-reveal').evaluate(d=>d.open)),'Spoiler videos must start hidden');
    assert(await page.locator('#watch .video-grid.story').isHidden());
    await page.locator('#watch .spoiler-reveal summary').click();
    assert(await page.locator('#watch .video-grid.story .video-card').first().isVisible());
    const cards=await page.locator('#watch .video-card').count();
    assert(cards>=6,'Expected the featured and story videos');
    await page.locator('#watch .video-grid.story .video-card').nth(1).locator('.video-play').click();
    const frame=page.locator('#watch iframe');
    await frame.waitFor();
    assert.equal(await frame.count(),1,'Exactly one player after one choice');
    assert(/^https:\/\/www\.youtube-nocookie\.com\/embed\/[\w-]{11}\?/.test(await frame.getAttribute('src')),'Player must use youtube-nocookie.com');
    assert.equal(await page.locator('#watch .video-card img').count(),cards-1,'Other cards keep their posters');
    await page.unroute(/youtube-nocookie\.com/);
    checks.push('watch section: spoilers hidden until opened; a chosen card loads only its youtube-nocookie player');
    await page.goto(base+'reference.html#controls');
    await page.getByLabel('FIND A SHORTCUT').fill('HUD');
    assert(await page.locator('[data-control-row]:visible').count()>0);
    assert(await page.locator('[data-control-row]:visible').count()<await page.locator('[data-control-row]').count());
    await page.getByLabel('FIND A SHORTCUT').fill('nothing-matches-zz');
    await page.getByRole('status').filter({hasText:'No matching shortcuts'}).waitFor();
    await page.getByRole('button',{name:'Clear',exact:true}).click();
    assert.equal(await page.locator('[data-control-row]:visible').count(),await page.locator('[data-control-row]').count());
    assert(await page.getByLabel('FIND A SHORTCUT').evaluate(e=>e===document.activeElement));
    checks.push('control search, empty result, reset and focus recovery');
    await page.locator('#controls').scrollIntoViewIfNeeded();
    await page.screenshot({path:path.join(output,'desktop-controls.png')});
    await page.goto(base+'support.html');
    const configuredSupport=await page.evaluate(()=>window.WUWA_SITE?.supportUrl||'');
    if(configuredSupport){
      assert(await page.locator('#support-link').isVisible());
      assert.equal(await page.locator('#support-link').getAttribute('href'),configuredSupport);
      assert(await page.locator('#support-pending').isHidden());
      checks.push('owner-configured support link displayed without navigation');
    }
    await page.route('**/config.js',r=>r.fulfill({contentType:'text/javascript',body:'window.WUWA_SITE={supportUrl:""};'}));
    await page.reload();
    assert(await page.locator('#support-link').isHidden());assert(await page.locator('#support-pending').isVisible());
    await page.unroute('**/config.js');
    await page.route('**/config.js',r=>r.fulfill({contentType:'text/javascript',body:'window.WUWA_SITE={supportUrl:"javascript:alert(1)"};'}));
    await page.reload();assert(await page.locator('#support-link').isHidden());
    await page.unroute('**/config.js');
    await page.route('**/config.js',r=>r.fulfill({contentType:'text/javascript',body:'window.WUWA_SITE={supportUrl:"https://example.com/owner-test",supportLabel:"Fixture support"};'}));
    await page.reload();assert.equal(await page.locator('#support-link').getAttribute('href'),'https://example.com/owner-test');
    assert(await page.locator('#support-pending').isHidden());
    await page.unroute('**/config.js');checks.push('empty support URL stays disconnected; unsafe scheme refused; valid fixture displayed without navigation');
    await page.setViewportSize({width:390,height:844});
    for(const file of ['index.html','guide.html','l/ar.html','l/ar-guide.html','l/ja-guide.html','reference.html','record.html','credits.html','support.html','risk.html','license.html','languages.html','feedback.html','testing.html','privacy.html','l/ar-privacy.html']){
      await page.goto(base+file);
      assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'Mobile page overflows: '+file);
      if(['index.html','support.html'].includes(file))await page.screenshot({path:path.join(output,'mobile-'+file.replace('.html','.png')),fullPage:true});
      checks.push('mobile overflow: '+file);
    }
    await page.goto(base+'guide.html#controls');await page.locator('#controls').scrollIntoViewIfNeeded();
    await page.screenshot({path:path.join(output,'mobile-controls.png')});
    const plain=await browser.newContext({javaScriptEnabled:false,viewport:{width:390,height:844}});
    const nojs=await plain.newPage();await nojs.goto(base+'reference.html');
    assert(await nojs.locator('table').count()>5);assert(await nojs.locator('[data-control-row]:visible').count()>30);
    await nojs.goto(base+'support.html');assert(await nojs.locator('#support-pending').isVisible());assert(await nojs.locator('#support-link').isHidden());
    // Without JavaScript a video card is a plain link to YouTube, never an embedded player.
    await nojs.goto(base+'index.html');
    assert.equal(await nojs.locator('#watch iframe').count(),0);
    assert(/^https:\/\/www\.youtube\.com\/watch\?v=[\w-]{11}$/.test(await nojs.locator('#watch .video-play').first().getAttribute('href')));
    await plain.close();checks.push('no-JavaScript reference, disconnected support and plain video links remain usable');
    await page.goto(base+'guide.html');await page.emulateMedia({media:'print'});
    await page.pdf({path:path.join(output,'player-guide.pdf'),format:'A4',printBackground:true,margin:{top:'14mm',bottom:'14mm',left:'12mm',right:'12mm'}});
    checks.push('print guide rendered to PDF');
    await page.emulateMedia({media:'screen'});await page.setViewportSize({width:1280,height:900});
    for(const file of ['index.html','guide.html']){
      await page.goto(base+file);
      const home=file==='index.html';
      const steps=page.locator('#start .launcher-steps > li');
      if(home){
        assert.equal(await steps.count(),3,'Three install steps expected on '+file);
        for(const [index,label] of ['Install','Set up once','Launch in VR'].entries()){
          const text=await steps.nth(index).locator('h3').innerText();
          assert(text===label,'Install steps out of order: '+text.slice(0,40));
        }
        // Each step shows the matching native launcher render, in order.
        const shots=await page.locator('.tour-shot img').evaluateAll(images=>images.map(i=>i.getAttribute('src')));
        assert.deepEqual(shots,['media/launcher/en-fresh.webp','media/launcher/en-found.webp','media/launcher/en-ready.webp']);
        await steps.nth(2).locator('.tour-select').click();
        assert(await page.locator('.tour-shot[data-shot="2"]').isVisible()&&await page.locator('.tour-shot[data-shot="0"]').isHidden(),'Tour step did not switch its screenshot');
        assert.equal(await steps.nth(2).locator('.tour-select').getAttribute('aria-pressed'),'true');
        await page.reload();
      }
      assert.equal(await page.locator('.visual-steps img').count(),0,'Old browser screenshots must not represent the installed app');
      const asset=`https://github.com/ChronoHaxx/wuwa-vr/releases/download/${release.tag}/`;
      assert.equal(await page.locator('.header-download').getAttribute('href'),asset+release.installer,'Header download must be the current installer');
      if(home){
        assert.equal(await page.locator('#release-download a.primary').getAttribute('href'),asset+release.installer,'Installer must be the primary download');
        assert.equal(await page.locator('#release-download a').filter({hasText:'Advanced: portable ZIP'}).getAttribute('href'),asset+'WuWa-VR-Launcher.zip','Portable fallback must use the current release');
        assert((await steps.nth(2).innerText()).includes('Launch in VR'),'Final step must explain launching');
      } else assert((await page.locator('#start').innerText()).includes('Launch in VR'),'The guide must explain launching');
      // Readable type: no visible body text below 14px.
      const small=await page.evaluate(()=>[...document.querySelectorAll('p,li,td,th,figcaption,summary,a,label,button')]
        .filter(e=>e.offsetParent&&e.textContent.trim()&&parseFloat(getComputedStyle(e).fontSize)<14).map(e=>e.textContent.trim().slice(0,40)));
      assert.deepEqual(small,[],'Text below 14px on '+file);
      // Keyboard: skip link first, then every stop visible with a focus outline.
      await page.keyboard.press('Tab');
      assert.equal(await page.evaluate(()=>document.activeElement.className),'skip');
      for(let i=0;i<25;i++){
        await page.keyboard.press('Tab');
        const stop=await page.evaluate(()=>{const e=document.activeElement,s=getComputedStyle(e),r=e.getBoundingClientRect();
          return {tag:e.tagName,text:(e.textContent||e.getAttribute('aria-label')||'').trim().slice(0,30),visible:r.width>0&&r.height>0,outline:s.outlineStyle!=='none'&&parseFloat(s.outlineWidth)>0};});
        if(stop.tag==='BODY')break;
        assert(stop.visible&&stop.outline,`Keyboard stop without visible focus on ${file}: ${JSON.stringify(stop)}`);
      }
    }
    checks.push('three ordered install steps with native screenshots that switch per step; current installer/portable links; no text under 14px; keyboard order starts at skip link with visible focus');
    // Usage counts (docs/TRACKING-PLAN.md): the published address served from site/, a fixture collector,
    // and navigator.webdriver masked so the live path runs. Real visitors' browsers are never automated.
    {
      const live='https://chronohaxx.github.io/wuwa-vr/',collector='https://collector.test/e',token='0123456789abcdef0123456789abcdef';
      const session=async({config=true,gpc=false}={})=>{
        const context=await browser.newContext({viewport:{width:1280,height:900}}),beacons=[],cloudflare=[];
        await context.addInitScript(gpc=>{Object.defineProperty(Navigator.prototype,'webdriver',{get:()=>false});if(gpc)Object.defineProperty(Navigator.prototype,'globalPrivacyControl',{get:()=>true});},gpc);
        await context.route('**/*',route=>{
          const url=new URL(route.request().url());
          if(url.href.startsWith(collector)){beacons.push(JSON.parse(route.request().postData()));return route.fulfill({status:204});}
          if(url.hostname==='static.cloudflareinsights.com'){cloudflare.push(url.href);return route.fulfill({contentType:'text/javascript',body:''});}
          if(url.origin!=='https://chronohaxx.github.io'||!url.pathname.startsWith('/wuwa-vr/'))return route.abort();
          if(config&&url.pathname==='/wuwa-vr/config.js')return route.fulfill({contentType:'text/javascript',body:`window.WUWA_SITE=${JSON.stringify({repositoryUrl:'https://github.com/ChronoHaxx/wuwa-vr',analyticsEndpoint:collector,webAnalyticsToken:token})};`});
          const file=path.join(site,url.pathname.slice('/wuwa-vr/'.length)||'index.html');
          return fs.existsSync(file)?route.fulfill({path:file}):route.fulfill({status:404});
        });
        const tab=await context.newPage();
        const events=()=>beacons.flatMap(b=>b.events);
        const leave=()=>tab.evaluate(()=>dispatchEvent(new Event('pagehide')));
        return {context,tab,beacons,cloudflare,events,leave};
      };
      const s=await session();
      await s.tab.goto(live+'index.html',{waitUntil:'load'});await s.tab.waitForTimeout(200);
      // Keep the test on the page: the tracker's click listener runs first (capture), then this cancels navigation.
      await s.tab.evaluate(()=>document.addEventListener('click',e=>{if(e.target.closest('a'))e.preventDefault();}));
      await s.tab.locator('.header-download').click();
      await s.tab.locator('#release-download a').filter({hasText:'Advanced: portable ZIP'}).click();
      await s.tab.locator('.faq summary').first().click();await s.tab.waitForTimeout(100);
      await s.leave();await s.tab.waitForTimeout(200);
      assert.deepEqual(s.beacons.map(b=>[b.app,b.schema]),s.beacons.map(()=>['wuwa-vr',1]));
      assert.deepEqual(s.events(),[
        {name:'page_viewed',props:{page:'/index.html',lang:'en'}},
        {name:'download_clicked',props:{page:'/index.html',lang:'en',asset:'installer',placement:'header'}},
        {name:'download_clicked',props:{page:'/index.html',lang:'en',asset:'portable_zip',placement:'hero'}},
        {name:'faq_opened',props:{page:'/index.html',lang:'en',question:'q1'}}]);
      assert.equal(s.cloudflare.length,1,'Cloudflare Web Analytics loads once, after the page');
      assert.deepEqual(await s.context.cookies(),[],'No cookies');
      assert.deepEqual(await s.tab.evaluate(()=>Object.keys(localStorage)),[],'Counting stores nothing');
      await s.tab.goto(live+'l/ja-guide.html',{waitUntil:'load'});
      for(const id of ['controls','updates','recovery','recording']){await s.tab.locator('#'+id).scrollIntoViewIfNeeded();await s.tab.waitForTimeout(100);}
      await s.leave();await s.tab.waitForTimeout(200);
      const reached=s.events().filter(e=>e.name==='guide_section_reached').map(e=>[e.props.page,e.props.lang,e.props.section]);
      assert.deepEqual(reached.map(r=>r[2]).sort(),['controls','recording','recovery','start','updates']);
      assert(reached.every(r=>r[0]==='/l/ja-guide.html'&&r[1]==='ja'));
      // The privacy page switch: off stores one value and stops both the beacon and Cloudflare.
      await s.tab.goto(live+'privacy.html',{waitUntil:'load'});
      assert.equal(await s.tab.locator('#analytics-state').innerText(),'Counting is on for this browser.');
      await s.tab.locator('#analytics-switch').click();
      assert.equal(await s.tab.locator('#analytics-state').innerText(),'Counting is off for this browser.');
      assert.equal(await s.tab.locator('#analytics-switch').innerText(),'Count my visits again');
      assert.deepEqual(await s.tab.evaluate(()=>({...localStorage})),{'wuwa-analytics':'off'});
      const before=[s.beacons.length,s.cloudflare.length];
      await s.tab.goto(live+'index.html',{waitUntil:'load'});await s.tab.waitForTimeout(200);await s.leave();await s.tab.waitForTimeout(200);
      assert.deepEqual([s.beacons.length,s.cloudflare.length],before,'Nothing is sent after opting out');
      await s.context.close();
      // Global Privacy Control, and the published config before the collector exists: nothing at all.
      for(const options of [{gpc:true},{config:false}]){
        const quiet=await session(options);
        await quiet.tab.goto(live+'index.html',{waitUntil:'load'});await quiet.tab.waitForTimeout(200);await quiet.leave();await quiet.tab.waitForTimeout(200);
        assert.deepEqual([quiet.beacons.length,quiet.cloudflare.length],[0,0],'Nothing may be sent: '+JSON.stringify(options));
        if(options.gpc){await quiet.tab.goto(live+'l/de-privacy.html',{waitUntil:'load'});
          assert.equal(await quiet.tab.locator('#analytics-state').innerText(),'Das Datenschutzsignal deines Browsers schaltet das Zählen bereits aus.');
          assert(await quiet.tab.locator('#analytics-switch').isDisabled());}
        await quiet.context.close();
      }
      checks.push('usage counts on the published address: page view, download and FAQ events with allowed fields only, guide sections in Japanese, Cloudflare beacon after load, no cookies or storage; opt-out switch, GPC and an empty config send nothing');
    }
    assert.deepEqual(errors,[]);
    fs.writeFileSync(path.join(output,'verification.json'),JSON.stringify({checkedAt:new Date().toISOString(),basePath:'/wuwa-vr/',checks,errors,scope:'Local browser and static presentation only; no game/headset acceptance.'},null,2));
    console.log(JSON.stringify({passed:checks.length,output,errors}));
  }finally{await browser.close();await new Promise(r=>server.close(r));}
})().catch(e=>{console.error(e);server.close();process.exitCode=1;});
