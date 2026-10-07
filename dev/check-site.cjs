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
  const types={'.html':'text/html; charset=utf-8','.js':'text/javascript; charset=utf-8','.css':'text/css; charset=utf-8','.png':'image/png','.svg':'image/svg+xml','.webp':'image/webp','.jpg':'image/jpeg','.mp4':'video/mp4'};
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
    for(const file of ['index.html','guide.html','record.html','credits.html','support.html','risk.html','license.html','languages.html','feedback.html','testing.html','developers.html']){
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
      if(['index.html','support.html'].includes(file))await page.screenshot({path:path.join(output,'desktop-'+file.replace('.html','.png')),fullPage:true});
      checks.push('desktop links, assets and overflow: '+file);
    }
    await page.goto(base+'guide.html#controls');
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
    for(const file of ['index.html','guide.html','record.html','credits.html','support.html','risk.html','license.html','languages.html','feedback.html','testing.html']){
      await page.goto(base+file);
      assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'Mobile page overflows: '+file);
      if(['index.html','support.html'].includes(file))await page.screenshot({path:path.join(output,'mobile-'+file.replace('.html','.png')),fullPage:true});
      checks.push('mobile overflow: '+file);
    }
    await page.goto(base+'guide.html#controls');await page.locator('#controls').scrollIntoViewIfNeeded();
    await page.screenshot({path:path.join(output,'mobile-controls.png')});
    const plain=await browser.newContext({javaScriptEnabled:false,viewport:{width:390,height:844}});
    const nojs=await plain.newPage();await nojs.goto(base+'guide.html');
    assert(await nojs.locator('table').count()>5);assert(await nojs.locator('[data-control-row]:visible').count()>30);
    await nojs.goto(base+'support.html');assert(await nojs.locator('#support-pending').isVisible());assert(await nojs.locator('#support-link').isHidden());
    await plain.close();checks.push('no-JavaScript guide and disconnected support remain usable');
    await page.goto(base+'guide.html');await page.emulateMedia({media:'print'});
    await page.pdf({path:path.join(output,'player-guide.pdf'),format:'A4',printBackground:true,margin:{top:'14mm',bottom:'14mm',left:'12mm',right:'12mm'}});
    checks.push('print guide rendered to PDF');
    await page.emulateMedia({media:'screen'});await page.setViewportSize({width:1280,height:900});
    for(const file of ['index.html','guide.html']){
      await page.goto(base+file);
      const home=file==='index.html';
      const steps=page.locator(home?'#start .launcher-steps > li':'#start > ol:first-of-type > li');
      assert.equal(await steps.count(),3,'Three installed-launcher steps expected on '+file);
      // Home step titles match the launcher's own card titles; the guide keeps its numbered prose.
      for(const [index,label] of (home?['Find Wuthering Waves','Install VR','Headset or simulator']:['01 · Game.','02 · Install VR.','03 · Headset or simulator.']).entries()){
        const text=home?await steps.nth(index).locator('h3').innerText():await steps.nth(index).innerText();
        assert(home?text===label:text.startsWith(label),'Launcher steps out of order on '+file+': '+text.slice(0,40));
      }
      if(home){
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
      assert.equal(await page.locator('#release-download a.primary').getAttribute('href'),asset+release.installer,'Installer must be the primary download');
      assert.equal(await page.locator('#release-download a').filter({hasText:'Advanced: portable ZIP'}).getAttribute('href'),asset+'WuWa-VR-Launcher.zip','Portable fallback must use the current release');
      assert((await steps.nth(2).innerText()).includes('Launch in VR'),'Final step must explain launching');
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
    checks.push('three ordered launcher steps with native screenshots that switch per step; current installer/portable links; no text under 14px; keyboard order starts at skip link with visible focus');
    assert.deepEqual(errors,[]);
    fs.writeFileSync(path.join(output,'verification.json'),JSON.stringify({checkedAt:new Date().toISOString(),basePath:'/wuwa-vr/',checks,errors,scope:'Local browser and static presentation only; no game/headset acceptance.'},null,2));
    console.log(JSON.stringify({passed:checks.length,output,errors}));
  }finally{await browser.close();await new Promise(r=>server.close(r));}
})().catch(e=>{console.error(e);server.close();process.exitCode=1;});
