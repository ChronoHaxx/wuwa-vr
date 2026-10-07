// Behavioral checks for localization, accessibility and local-only issue drafts.
const fs=require('node:fs'),path=require('node:path'),http=require('node:http'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..'),site=path.join(root,'site');
const modules=process.env.WUWA_NODE_MODULES||path.join(process.env.USERPROFILE||process.env.HOME||'','.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
const {chromium}=require(require.resolve('playwright',{paths:[root,modules]}));
const output=path.resolve(process.argv[2]||path.join(root,'extracted/accessibility-20260925/community-browser'));
fs.mkdirSync(output,{recursive:true});
const codes=['en','zh-Hans','ja','ko','es','pt-BR','fr','de','ru','ar'];
const server=http.createServer((req,res)=>{
  const url=new URL(req.url,'http://localhost'),file=path.resolve(site,'.'+decodeURIComponent(url.pathname));
  if(!file.startsWith(site+path.sep)||!fs.existsSync(file)||!fs.statSync(file).isFile()){res.writeHead(404);return res.end();}
  res.setHeader('Content-Type',({'.html':'text/html; charset=utf-8','.js':'text/javascript; charset=utf-8','.css':'text/css; charset=utf-8','.png':'image/png','.svg':'image/svg+xml','.webp':'image/webp','.jpg':'image/jpeg','.mp4':'video/mp4'})[path.extname(file)]||'text/plain');
  res.end(fs.readFileSync(file));
});
(async()=>{
  await new Promise(r=>server.listen(0,'127.0.0.1',r));
  const base=`http://127.0.0.1:${server.address().port}`;
  const browser=await chromium.launch({headless:true,...(process.env.WUWA_CHROMIUM?{executablePath:process.env.WUWA_CHROMIUM}:{})});
  const checks=[],errors=[];let sent=0;
  try{
    const page=await browser.newPage({viewport:{width:1280,height:900}});
    // Exercise the unconfigured fallback independently of the owner's real URL.
    await page.route('**/config.js',r=>r.fulfill({contentType:'text/javascript',body:'window.WUWA_SITE={repositoryUrl:"",supportUrl:""};'}));
    page.on('pageerror',e=>errors.push(String(e)));
    page.on('request',r=>{if(r.method()!=='GET')sent++;if(!r.url().startsWith(base+'/'))errors.push('Unexpected external request '+r.url());});
    page.on('response',r=>{if(r.status()>=400)errors.push(r.status()+' '+r.url());});
    for(const code of codes){
      await page.goto(base+'/l/'+code+'.html');
      assert.equal(await page.locator('html').getAttribute('lang'),code);
      assert.equal(await page.locator('html').getAttribute('dir'),code==='ar'?'rtl':'ltr');
      assert.equal(await page.locator('h1').count(),1);
      assert.equal(await page.locator('#controls bdi[dir=ltr]').count(),8);
      assert(await page.locator('#report-form').isVisible());
      assert.equal(await page.locator('#report-form').getAttribute('data-lang'),code);
      assert(await page.evaluate(()=>document.querySelector('.risk').compareDocumentPosition(document.querySelector('#start'))&Node.DOCUMENT_POSITION_FOLLOWING));
      const duplicate=await page.evaluate(()=>{const ids=[...document.querySelectorAll('[id]')].map(e=>e.id);return ids.filter((v,i)=>ids.indexOf(v)!==i);});
      assert.deepEqual(duplicate,[]);
      for(const href of await page.locator('a[href]').evaluateAll(a=>a.map(x=>x.getAttribute('href')))){
        if(/^https?:/.test(href))continue;
        const target=new URL(href,base+'/l/'+code+'.html'),file=path.join(site,target.pathname);
        assert(fs.existsSync(file),'Missing '+href);
        if(target.hash)assert(fs.readFileSync(file,'utf8').includes(`id="${target.hash.slice(1)}"`),'Missing anchor '+href);
      }
      await page.setViewportSize({width:320,height:720});
      assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'Narrow overflow: '+code);
      await page.setViewportSize({width:1280,height:900});
      if(code==='ar')await page.screenshot({path:path.join(output,'arabic-desktop.png')});
      checks.push('locale, risk placement, links, form and 320px reflow: '+code);
    }
    await page.setViewportSize({width:390,height:844});await page.goto(base+'/l/ar.html');
    await page.screenshot({path:path.join(output,'arabic-mobile.png')});
    await page.goto(base+'/feedback.html?kind=feature');
    assert.equal(await page.locator('#report-kind').inputValue(),'feature');
    await page.keyboard.press('Tab');assert.equal(await page.evaluate(()=>document.activeElement.className),'skip');
    await page.keyboard.press('Enter');assert.equal(new URL(page.url()).hash,'#main');
    await page.locator('#report-title').fill('   ');await page.locator('#report-details').fill('   ');
    await page.locator('#report-form button').click();assert(await page.locator('#report-preview').isHidden());
    assert.equal(new URL(page.url()).search,'?kind=feature');
    await page.locator('#report-title').fill('菜单 & HUD <request>');await page.locator('#report-details').fill('خطأ\nA & B = ? # % <script>');
    await page.locator('#report-form button').click();
    assert(await page.locator('#report-preview').isVisible());assert(await page.locator('#issue-link').isHidden());
    assert.equal(await page.evaluate(()=>document.activeElement.id),'draft-heading');
    assert((await page.locator('#report-draft').inputValue()).includes('<script>'));
    assert.equal(await page.locator('#report-preview script').count(),0);
    await page.evaluate(()=>Object.defineProperty(navigator,'clipboard',{configurable:true,value:{writeText:async()=>{throw Error('blocked');}}}));
    await page.locator('#copy-report').click();
    assert(await page.locator('#report-status').innerText().then(s=>s.includes('Nothing has been submitted')));
    assert(await page.locator('#report-draft').evaluate(e=>e.selectionEnd===e.value.length));
    await page.evaluate(()=>Object.defineProperty(navigator,'clipboard',{configurable:true,value:{writeText:async text=>{window.copiedDraft=text;}}}));
    await page.locator('#copy-report').click();assert.equal(await page.evaluate(()=>window.copiedDraft),await page.locator('#report-draft').inputValue());
    checks.push('keyboard skip link, whitespace validation, Unicode plain-text draft and both clipboard paths');
    const configure=async value=>{await page.unroute('**/config.js');await page.route('**/config.js',r=>r.fulfill({contentType:'text/javascript',body:'window.WUWA_SITE='+JSON.stringify({repositoryUrl:value})+';'}));await page.goto(base+'/feedback.html?kind=language');};
    const fill=async()=>{await page.locator('#report-title').fill('日本語 & العربية');await page.locator('#report-details').fill('a+b? = <test>\nSecond line');await page.locator('#report-form button').click();};
    for(const value of ['javascript:alert(1)','https://github.com.evil.test/owner/repo','https://user:pw@github.com/owner/repo','https://github.com/owner/repo?redirect=x','http://github.com/owner/repo']){
      await configure(value);await fill();assert(await page.locator('#issue-link').isHidden());
      assert.equal(await page.locator('[data-project-link]:visible').count(),0);
    }
    await configure('https://github.com/ExampleOwner/example-repo.git');await fill();
    const url=new URL(await page.locator('#issue-link').getAttribute('href'));
    assert.equal(url.origin,'https://github.com');assert.equal(url.pathname,'/ExampleOwner/example-repo/issues/new');
    assert.equal(url.searchParams.get('template'),'language.md');assert.equal(url.searchParams.get('title'),'[Language] 日本語 & العربية');
    assert(url.searchParams.get('body').includes('a+b? = <test>\nSecond line'));
    assert.equal(await page.locator('[data-project-link=issues]').first().getAttribute('href'),'https://github.com/ExampleOwner/example-repo/issues');
    await page.locator('#report-details').fill('changed');assert(await page.locator('#report-preview').isHidden());assert.equal(await page.locator('#issue-link').getAttribute('href'),null);
    const long='界'.repeat(9990);await page.locator('#report-details').fill(long);await page.locator('#report-form button').click();
    const longURL=new URL(await page.locator('#issue-link').getAttribute('href'));assert.equal(longURL.searchParams.get('body'),null);
    assert((await page.locator('#report-draft').inputValue()).includes(long));
    checks.push('unsafe destinations rejected, exact Unicode GitHub draft, stale-draft invalidation and long-report fallback without truncation');
    await page.goto(base+'/developers.html');
    assert.equal(await page.locator('[data-project-link=fork]').getAttribute('href'),'https://github.com/ExampleOwner/example-repo/fork');
    assert(await page.locator('[data-repository-pending]').isHidden());
    for(const link of await page.locator('[data-project-link=code]').all())assert.equal(await link.getAttribute('href'),'https://github.com/ExampleOwner/example-repo');
    await page.goto(base+'/feedback.html?build='+encodeURIComponent('26 Sep 15:39 (applied) / SteamVR <local>'));
    assert.equal(await page.locator('#report-version').inputValue(),'26 Sep 15:39 (applied) / SteamVR <local>');
    await fill();assert((await page.locator('#report-draft').inputValue()).includes('26 Sep 15:39 (applied) / SteamVR <local>'));
    checks.push('one GitHub destination for source/fork/issues and build context retained as plain text');
    await page.route('**/feedback.html*',r=>r.fulfill({contentType:'text/html',body:fs.readFileSync(path.join(site,'feedback.html'),'utf8').replace(/<p class="notice">[^<]*<\/p>/,'').replace(/data-empty="[^"]*"/,'')}));
    await configure('https://github.com/ExampleOwner/example-repo');await fill();
    assert(await page.locator('#issue-link').isVisible());checks.push('missing optional notice does not break submission interception');
    const plain=await browser.newContext({javaScriptEnabled:false,viewport:{width:320,height:720}});
    const nojs=await plain.newPage();await nojs.goto(base+'/l/ar.html');assert(await nojs.locator('#report-form').isHidden());
    assert(await nojs.locator('noscript').isVisible());assert(await nojs.locator('.risk').isVisible());assert.equal(await nojs.locator('#controls tr').count(),8);
    await plain.close();checks.push('without JavaScript: readable risk, controls and manual feedback instructions; no unhandled form');
    assert.equal(sent,0);assert.deepEqual(errors,[]);
    fs.writeFileSync(path.join(output,'verification.json'),JSON.stringify({checkedAt:new Date().toISOString(),checks,errors,networkPosts:sent,scope:'Local browser fixtures only; no GitHub submission, payment, game or headset acceptance.'},null,2));
    console.log(JSON.stringify({passed:checks.length,errors,networkPosts:sent,output}));
  }finally{await browser.close();await new Promise(r=>server.close(r));}
})().catch(e=>{console.error(e);server.close();process.exitCode=1;});
