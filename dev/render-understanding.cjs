const fs=require('node:fs'),path=require('node:path');
const root=path.resolve(__dirname,'..');
const {shell}=require(path.join(root,'dev/site-layout.cjs'));
(async()=>{
const {marked}=await import(require('node:url').pathToFileURL(require.resolve('marked',{paths:[root,path.join(root,'dev-tools/node_modules'),path.join(process.env.USERPROFILE,'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules')]})).href);
let text=fs.readFileSync(path.join(root,'docs/UNDERSTANDING-WUWA-VR.md'),'utf8');
let html=marked.parse(text.replace(/^# .+\n/,''));
const headings=[];
html=html.replace(/<h2>(.*?)<\/h2>/g,(_,t)=>{const id=t.toLowerCase().replace(/<[^>]*>/g,'').replace(/[^a-z0-9]+/g,'-').replace(/^-|-$/g,'');headings.push([id,t]);return `<h2 id="${id}">${t}</h2>`;});
html=html.replace(/<table>/g,'<div class="table-scroll"><table>').replace(/<\/table>/g,'</table></div>');
const video=`<figure><video controls preload="metadata" poster="media/explainer-poster.png" style="width:100%;aspect-ratio:16/9"><source src="media/explainer-60s.mp4?v=20260928-headset" type="video/mp4"><track kind="captions" src="media/explainer-captions.vtt?v=20260928-headset" srclang="en" label="English"><a href="media/explainer-60s.mp4?v=20260928-headset">Download the 60-second explainer</a></video><figcaption>60 seconds · silent, with visible captions · original design by Claude Opus 5.5; updated by Codex after headset confirmation on 28 Sep. Diagrams explain the system; gameplay is labelled earlier-build footage. <a href="media/explainer-transcript.md">Transcript</a>.</figcaption></figure>`;
const body=`<h1>Understand WuWa VR</h1><p class="intro">A timeline, code map and honest next steps. Read this before changing rendering settings or continuing the investigation.</p>${video}<p><a class="button primary" href="https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-2026-10-01-1817">Beta download · 1 Oct 18:17 BST · game 3.7</a> <a href="https://github.com/ChronoHaxx/wuwa-vr/blob/main/docs/UNDERSTANDING-WUWA-VR.md">Read on GitHub</a></p><details open><summary>Jump to a section</summary><ul>${headings.map(([id,t])=>`<li><a href="#${id}">${t}</a></li>`).join('')}</ul></details><article class="guide-content">${html}</article>`;
fs.writeFileSync(path.join(root,'site/understanding.html'),shell('Understand WuWa VR','Timeline, beginner code map, experiments and a 60-second explanation.',body));
for(const name of ['index.html','developers.html','guide.html']){
const p=path.join(root,'site',name);let s=fs.readFileSync(p,'utf8');
s=s.replace(/<aside class="notice"><strong>\d+ \w+ update:<\/strong>[\s\S]*?<\/aside>/g,'');
s=s.replace('<main class="wrap" id="main">','<main class="wrap" id="main"><aside class="notice"><strong>1 Oct update:</strong> <a href="https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-2026-10-01-1817">Beta for game 3.7 · 1 Oct 18:17 BST</a> · <a href="understanding.html">60-second explainer, timeline and code guide</a>. Far trees, props and far lighting now match between the eyes, and the Resonators and team-screen reflections are back in place. Weapon and Echo submenu reflections are deferred.</aside>');
fs.writeFileSync(p,s);
}
// Ownership files must remain unchanged and are not indexable content pages.
const site=path.join(root,'site'), base='https://chronohaxx.github.io/wuwa-vr/';
function pages(dir){return fs.readdirSync(dir,{withFileTypes:true}).flatMap(e=>e.isDirectory()?pages(path.join(dir,e.name)):[path.join(dir,e.name)]);}
const urls=pages(site).filter(p=>p.endsWith('.html')&&!/^google[a-f0-9]+\.html$/.test(path.basename(p))).sort().map(p=>base+path.relative(site,p).split(path.sep).map(encodeURIComponent).join('/'));
fs.writeFileSync(path.join(site,'sitemap.xml'),'<?xml version="1.0" encoding="UTF-8"?>\n<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n'+urls.map(u=>'  <url><loc>'+u+'</loc></url>').join('\n')+'\n</urlset>\n');
})();
