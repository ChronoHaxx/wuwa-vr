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
const video=`<figure><video controls preload="metadata" poster="media/explainer-poster.png" style="width:100%;aspect-ratio:16/9"><source src="media/explainer-60s.mp4" type="video/mp4"><track kind="captions" src="media/explainer-captions.vtt" srclang="en" label="English"><a href="media/explainer-60s.mp4">Download the 60-second explainer</a></video><figcaption>60 seconds · silent, with visible captions · made with Claude Opus 5.5 and reviewed by Codex. Diagrams explain the system; gameplay is labelled earlier-build footage. <a href="media/explainer-transcript.md">Transcript</a>.</figcaption></figure>`;
const body=`<h1>Understand WuWa VR</h1><p class="intro">A timeline, code map and honest next steps. Read this before changing rendering settings or continuing the investigation.</p>${video}<p><a class="button primary" href="https://github.com/ChronoHaxx/wuwa-vr/releases/tag/experimental-2026-09-28-1628">Experimental download · 28 Sep 16:28</a> <a href="https://github.com/ChronoHaxx/wuwa-vr/blob/main/docs/UNDERSTANDING-WUWA-VR.md">Read on GitHub</a></p><details open><summary>Jump to a section</summary><ul>${headings.map(([id,t])=>`<li><a href="#${id}">${t}</a></li>`).join('')}</ul></details><article class="guide-content">${html}</article>`;
fs.writeFileSync(path.join(root,'site/understanding.html'),shell('Understand WuWa VR','Timeline, beginner code map, experiments and a 60-second explanation.',body));
for(const name of ['index.html','developers.html','guide.html']){
const p=path.join(root,'site',name);let s=fs.readFileSync(p,'utf8');
if(!s.includes('28 Sep update:'))s=s.replace('<main class="wrap" id="main">','<main class="wrap" id="main"><aside class="notice"><strong>28 Sep update:</strong> <a href="https://github.com/ChronoHaxx/wuwa-vr/releases/tag/experimental-2026-09-28-1628">Experimental 16:28 package</a> · <a href="understanding.html">60-second explainer, timeline and code guide</a>. Select the 16:27 camera + trigger candidate. Rendering fixes remain unconfirmed. Older beta guidance below describes its own build.</aside>');
fs.writeFileSync(p,s);
}
})();
