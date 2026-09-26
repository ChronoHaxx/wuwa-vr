// Original project tooling; licence scope: see LICENSE.md. Renders site/media/mark.svg into a Windows icon
// (PNG-compressed ICO entries) with headless Chromium. Run only when the mark
// changes: node dev/portable/make-icon.cjs
const fs=require('node:fs'),path=require('node:path');
const root=path.resolve(__dirname,'../..');
const modules=process.env.WUWA_NODE_MODULES||path.join(process.env.USERPROFILE,'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
const {chromium}=require(require.resolve('playwright',{paths:[modules]}));
(async()=>{
  const svg=fs.readFileSync(path.join(root,'site/media/mark.svg'),'utf8');
  const browser=await chromium.launch({headless:true,...(process.env.WUWA_CHROMIUM?{executablePath:process.env.WUWA_CHROMIUM}:{})});
  const images=[];
  try{
    for(const size of [256,48,32,16]){
      const page=await browser.newPage({viewport:{width:size,height:size},deviceScaleFactor:1});
      await page.setContent(`<html><body style="margin:0;background:transparent">${svg.replace('<svg ',`<svg width="${size}" height="${size}" `)}</body></html>`);
      images.push({size,png:await page.screenshot({omitBackground:true,clip:{x:0,y:0,width:size,height:size}})});
      await page.close();
    }
  }finally{await browser.close();}
  const header=Buffer.alloc(6);header.writeUInt16LE(0,0);header.writeUInt16LE(1,2);header.writeUInt16LE(images.length,4);
  let offset=6+16*images.length;const entries=[];
  for(const {size,png} of images){
    const e=Buffer.alloc(16);e.writeUInt8(size===256?0:size,0);e.writeUInt8(size===256?0:size,1);
    e.writeUInt16LE(1,4);e.writeUInt16LE(32,6);e.writeUInt32LE(png.length,8);e.writeUInt32LE(offset,12);
    entries.push(e);offset+=png.length;
  }
  const out=path.join(__dirname,'wuwa-vr.ico');
  fs.writeFileSync(out,Buffer.concat([header,...entries,...images.map(i=>i.png)]));
  console.log(JSON.stringify({icon:out,sizes:images.map(i=>i.size),bytes:fs.statSync(out).size}));
})().catch(e=>{console.error(e);process.exitCode=1;});
