"use strict";
(() => {
  const picker=document.getElementById('launcherLanguage');
  if (!picker) return;
  const status=document.getElementById('launcherLanguageStatus');
  const setup=document.getElementById('launcherSetup');
  const skip='script,style,pre,code,kbd,samp,textarea,input,[contenteditable="true"],#launcherLanguage,[data-no-translate],[data-launcher-guide]';
  const original=new WeakMap(), attributes=new WeakMap();
  let catalogs={}, code='en', observer=null, scheduled=false;
  const normalized=text=>text.trim().replace(/\s+/g,' ');
  const escapeRegex=text=>text.replace(/[.*+?^${}()|[\]\\]/g,'\\$&');
  function translated(text) {
    const messages=catalogs[code]?.messages || {};
    const key=normalized(text);
    if (Object.hasOwn(messages,key)) return text.replace(text.trim(),messages[key]);
    for (const [source,target] of Object.entries(catalogs[code]?.templates || {})) {
      const names=[];
      const pieces=source.split(/(\{[a-z]+\})/g).map(part=>{
        if (/^\{[a-z]+\}$/.test(part)) {names.push(part);return '(.+?)';}
        return escapeRegex(part);
      });
      const match=key.match(new RegExp('^'+pieces.join('')+'$'));
      if (match) return text.replace(text.trim(),target.replace(/\{[a-z]+\}/g,p=>match[names.indexOf(p)+1]));
    }
    return text;
  }
  function apply(force=false) {
    if (observer) observer.disconnect();
    for (const element of document.querySelectorAll('[data-launcher-guide]')) {
      const value=catalogs[code]?.guide[element.dataset.launcherGuide];
      if(value && element.textContent!==value) element.textContent=value;
    }
    const walker=document.createTreeWalker(document.body,NodeFilter.SHOW_TEXT);
    for (let node=walker.nextNode();node;node=walker.nextNode()) {
      if (!node.parentElement || node.parentElement.closest(skip)) continue;
      const previous=original.get(node), now=node.nodeValue;
      const source=previous && previous.output===now ? previous.source : now;
      if (!force && previous && previous.output===now) continue;
      const output=translated(source);
      if (now!==output) node.nodeValue=output;
      original.set(node,{source,output});
    }
    for (const element of document.querySelectorAll('[title],[aria-label],[placeholder]')) {
      if (element.closest(skip) && element.id!=='launcherPath') continue;
      const record=attributes.get(element)||{};
      for (const name of ['title','aria-label','placeholder']) {
        const now=element.getAttribute(name);if(now===null) continue;
        const previous=record[name],source=previous&&previous.output===now?previous.source:now;
        const output=translated(source);if(now!==output)element.setAttribute(name,output);
        record[name]={source,output};
      }
      attributes.set(element,record);
    }
    document.documentElement.lang=code;
    document.documentElement.dir=catalogs[code].direction;
    setup.href='/launcher-setup/'+encodeURIComponent(code);
    if(observer) observer.observe(document.body,{subtree:true,childList:true,characterData:true});
  }
  function select(value) {
    code=value;picker.value=value;
    status.textContent=catalogs[code].guide.scope;
    // Keep the already translated scope note out of subsequent English lookup.
    status.dataset.noTranslate='';
    apply(true);
    try{localStorage.setItem('wuwa-language',code);}catch{}
    document.dispatchEvent(new CustomEvent('wuwa-language-changed',{detail:{language:code}}));
  }
  function preferred(payload) {
    if(catalogs[payload.language]) return payload.language;
    let remembered='';try{remembered=localStorage.getItem('wuwa-language')||'';}catch{}
    if(catalogs[remembered])return remembered;
    for(const language of navigator.languages||[]) {
      if(catalogs[language])return language;
      const prefix=language.toLowerCase();
      if(prefix==='zh-cn'||prefix==='zh-sg'||prefix.startsWith('zh-hans'))return 'zh-Hans';
      if(prefix.startsWith('pt'))return 'pt-BR';
      if(catalogs[prefix.split('-')[0]])return prefix.split('-')[0];
    }
    return 'en';
  }
  picker.addEventListener('change',async()=>{
    const requested=picker.value,previous=code;
    picker.disabled=true;
    try {
      const token=document.querySelector('meta[name="wuwa-token"]').content;
      const response=await fetch('/api/language',{method:'POST',headers:{'Content-Type':'application/json','X-WuWa-Token':token},body:JSON.stringify({language:requested})});
      if(!response.ok)throw Error('Language could not be saved.');
      select(requested);
    } catch {
      picker.value=previous;
      status.textContent=translated('Language could not be saved. Restart the launcher and try again.');
    } finally {picker.disabled=false;}
  });
  fetch('/api/language',{cache:'no-store'}).then(response=>{
    if(!response.ok)throw Error('Language catalogs unavailable');return response.json();
  }).then(payload=>{
    catalogs=payload.catalogs;
    if(!catalogs.en||!catalogs.en.messages)throw Error('Language catalogs unavailable');
    picker.replaceChildren();
    for(const [value,entry]of Object.entries(catalogs)) {
      const option=document.createElement('option');option.value=value;option.textContent=entry.name;option.lang=value;picker.append(option);
    }
    observer=new MutationObserver(()=>{
      if(scheduled)return;scheduled=true;
      queueMicrotask(()=>{scheduled=false;apply();});
    });
    select(preferred(payload));picker.disabled=false;
  }).catch(()=>{status.textContent='Restart the launcher to load the language update. / 重启启动器以加载语言更新。';});
})();
