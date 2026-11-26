// Isolated Node checks for the usage-count wrapper in site/app.js (docs/TRACKING-PLAN.md), in the
// shared usage-counter collector's format (workers/usage-counter in ChronoHaxx/the-finals-outfit).
// Runs the real app.js in a sandbox with a fake browser: no network, no DOM library.
const fs = require('node:fs'), path = require('node:path'), vm = require('node:vm');
const assert = require('node:assert/strict');
const source = fs.readFileSync(path.join(__dirname, '../site/app.js'), 'utf8');
const ENDPOINT = 'https://collector.example.workers.dev/e';

// A page with none of the optional elements: app.js must run and only count the page view.
function browser({host = 'chronohaxx.github.io', protocol = 'https:', pathname = '/wuwa-vr/guide.html', lang = 'en',
  config = {analyticsEndpoint: ENDPOINT}, gpc, dnt, webdriver, stored = {}, beacon = true, fetch = true, videos = [], width = 1440} = {}) {
  const sent = [], fetched = [], timers = [], listeners = {}, appended = [], storageWrites = [];
  const store = new Map(Object.entries(stored));
  const localStorage = {getItem: k => store.has(k) ? store.get(k) : null,
    setItem: (k, v) => { storageWrites.push(k); store.set(k, String(v)); }, removeItem: k => { storageWrites.push(k); store.delete(k); }};
  const element = () => ({dataset: {}, set defer(v) {}, set src(v) { this._src = v; }, get src() { return this._src; }});
  const document = {
    documentElement: {lang}, visibilityState: 'visible', readyState: 'complete',
    querySelectorAll: selector => selector === '[data-youtube]' ? videos.map(id => ({dataset: {youtube: id}})) : [],
    querySelector: () => null, getElementById: () => null, addEventListener: (type, fn) => { (listeners['doc:' + type] ||= []).push(fn); },
    createElement: element, head: {append: el => appended.push(el)},
    get cookie() { return ''; }, set cookie(v) { throw Error('app.js must never set a cookie'); }
  };
  const navigator = {globalPrivacyControl: gpc, doNotTrack: dnt, webdriver, languages: ['en'],
    sendBeacon: beacon ? (url, body) => { sent.push({url, body: JSON.parse(body)}); return beacon !== 'refuse'; } : undefined};
  const sandbox = {
    document, navigator, localStorage, console,
    location: {protocol, hostname: host, pathname, href: `${protocol}//${host}${pathname}`, search: '', origin: `${protocol}//${host}`},
    matchMedia: () => ({matches: false}), URL, URLSearchParams, JSON, Promise, innerWidth: width,
    setTimeout: (fn, ms) => { timers.push({fn, ms}); return timers.length; }, clearTimeout: id => { if (timers[id - 1]) timers[id - 1].fn = null; },
    fetch: fetch ? (url, options) => { fetched.push({url, options, body: JSON.parse(options.body)}); return Promise.resolve(); } : undefined,
    addEventListener: (type, fn) => { (listeners[type] ||= []).push(fn); }
  };
  sandbox.window = sandbox;
  sandbox.WUWA_SITE = config;
  vm.runInNewContext(source, sandbox, {filename: 'site/app.js'});
  const fire = type => (listeners[type] || []).forEach(fn => fn());
  const runTimers = () => timers.splice(0).forEach(t => t.fn && t.fn());
  const all = () => [...sent.map(s => s.body), ...fetched.map(f => f.body)];
  return {api: sandbox.WuWaTrack, sent, fetched, timers, appended, storageWrites, store, fire, runTimers, all, events: () => all().flatMap(b => b.events)};
}
// What the collector accepts: the envelope, 1-20 events, and exactly the listed properties.
function collectorAccepts(body, spec) {
  assert.deepEqual(Object.keys(body).sort(), ['app', 'build', 'events', 'form_factor', 'schema']);
  assert.equal(body.app, 'wuwa-vr'); assert.equal(body.schema, 1);
  assert(['phone', 'tablet', 'desktop'].includes(body.form_factor));
  assert.match(body.build, /^(?:[0-9a-f]{7}|dev)$/);
  assert(body.events.length >= 1 && body.events.length <= 20);
  for (const event of body.events) {
    assert.deepEqual(Object.keys(event).sort(), ['e', 'p']);
    assert.deepEqual(Object.keys(event.p).sort(), ['lang', 'page', ...Object.keys(spec[event.e])].sort(), event.e);
    assert(Object.keys(event.p).length <= 4, 'the collector keeps at most 4 properties');
    assert.match(event.p.page, /^[a-z0-9][a-z0-9_.-]{0,47}$/);
  }
}

const checks = [];
const check = (name, fn) => checks.push([name, fn]);

check('app.js runs on a bare page and counts one page load, sent with fetch when the page is hidden', () => {
  const b = browser();
  assert.equal(b.all().length, 0, 'nothing leaves before the page is hidden or 15 s pass');
  b.fire('pagehide');
  assert.equal(b.fetched.length, 1);
  assert.equal(b.fetched[0].url, ENDPOINT);
  assert.deepEqual(b.fetched[0].body, {app: 'wuwa-vr', schema: 1, form_factor: 'desktop', build: 'dev', events: [{e: 'app_loaded', p: {page: 'guide', lang: 'en'}}]});
  assert.deepEqual(JSON.parse(JSON.stringify(b.fetched[0].options)), {method: 'POST', body: JSON.stringify(b.fetched[0].body), keepalive: true, mode: 'no-cors', credentials: 'omit', headers: {'Content-Type': 'text/plain'}});
});

check('form factor follows the viewport width and build comes from the deploy stamp', () => {
  const at = (width, config) => { const b = browser({width, config: {analyticsEndpoint: ENDPOINT, ...config}}); b.fire('pagehide'); return b.all()[0]; };
  assert.equal(at(390).form_factor, 'phone');
  assert.equal(at(800).form_factor, 'tablet');
  assert.equal(at(1280).form_factor, 'desktop');
  assert.equal(at(1280, {siteBuild: 'e295ae9'}).build, 'e295ae9');
  assert.equal(at(1280, {siteBuild: 'not a sha'}).build, 'dev');
});

check('every event the site can send is one the collector accepts', () => {
  const b = browser({videos: ['N3yqYROPPww']});
  const spec = b.api.EVENTS;
  b.api.track('download_clicked', {asset: 'installer'});
  b.api.track('video_played', {video: 'N3yqYROPPww', placement: 'grid'});
  for (const name of Object.keys(spec)) b.api.track(name, {});
  b.fire('pagehide');
  for (const body of b.all()) collectorAccepts(body, spec);
  assert.deepEqual(b.events()[1].p, {page: 'guide', lang: 'en', asset: 'installer', placement: 'other'}, 'a missing property is sent as other');
});

check('nothing is sent with no endpoint configured', () => {
  const b = browser({config: {}});
  assert.equal(b.api.track('download_clicked', {asset: 'installer'}), false);
  b.fire('pagehide'); b.runTimers();
  assert.deepEqual(b.all(), []);
});

for (const [label, options] of [
  ['Global Privacy Control', {gpc: true}],
  ['Do Not Track', {dnt: '1'}],
  ['the opt-out switch', {stored: {'wuwa-analytics': 'off'}}],
  ['an automated browser', {webdriver: true}],
  ['localhost', {host: 'localhost', protocol: 'http:'}],
  ['a fork on another GitHub Pages site', {host: 'someone.github.io'}],
  ['plain http', {protocol: 'http:'}]
]) check(`nothing is sent or loaded with ${label}`, () => {
  const b = browser({...options, config: {analyticsEndpoint: ENDPOINT, webAnalyticsToken: 'a'.repeat(32)}});
  assert.equal(b.api.track('download_clicked', {asset: 'installer'}, {now: true}), false);
  b.fire('pagehide'); b.runTimers();
  assert.deepEqual(b.all(), []);
  assert.equal(b.appended.length, 0, 'the Cloudflare beacon must not load');
});

check('opting out drops what is queued and stores only the switch', () => {
  const b = browser();
  b.api.track('faq_opened', {question: 'q2'});
  b.api.setOptOut(true);
  b.fire('pagehide'); b.runTimers();
  assert.deepEqual(b.all(), []);
  assert.deepEqual(b.storageWrites, ['wuwa-analytics']);
  assert.equal(b.store.get('wuwa-analytics'), 'off');
  b.api.setOptOut(false);
  assert.equal(b.store.has('wuwa-analytics'), false);
  assert.equal(b.api.track('faq_opened', {question: 'q2'}, {now: true}), true);
});

check('counting writes nothing to storage', () => {
  const b = browser();
  b.api.track('download_clicked', {asset: 'installer', placement: 'hero'}, {now: true});
  b.fire('pagehide');
  assert.deepEqual(b.storageWrites, []);
});

check('unknown events and properties are dropped; values outside a list become "other"', () => {
  const b = browser({videos: ['N3yqYROPPww']});
  assert.equal(b.api.track('mouse_moved', {x: 1}), false);
  b.api.track('download_clicked', {asset: 'installer', placement: 'banner', email: 'me@example.com'});
  b.api.track('video_played', {video: 'N3yqYROPPww', placement: 'grid'});
  b.api.track('video_played', {video: 'dQw4w9WgXcQ', placement: 'grid'});
  b.api.track('language_chosen', {to: 'xx', via: 'menu'});
  b.fire('pagehide');
  const events = b.events().slice(1);
  assert.deepEqual(events.map(e => e.e), ['download_clicked', 'video_played', 'video_played', 'language_chosen']);
  assert.deepEqual(events[0].p, {page: 'guide', lang: 'en', asset: 'installer', placement: 'other'});
  assert.equal(events[1].p.video, 'N3yqYROPPww');
  assert.equal(events[2].p.video, 'other', 'only videos on the page');
  assert.equal(events[3].p.to, 'other');
});

check('every payload holds only the envelope and allowed keys, whatever the page passes', () => {
  const b = browser();
  const spec = b.api.EVENTS;
  for (const name of Object.keys(spec)) b.api.track(name, {...Object.fromEntries(Object.keys(spec[name]).map(k => [k, 'x'])), email: 'me@example.com', url: 'https://example.com/page?q=1'});
  b.fire('pagehide');
  for (const body of b.all()) collectorAccepts(body, spec);
  assert(!JSON.stringify(b.all()).includes('example.com'));
});

check('the page is an id from the path only, without the site prefix, query or hash', () => {
  assert.equal(browser({pathname: '/wuwa-vr/'}).api.pageId(), 'index');
  assert.equal(browser({pathname: '/wuwa-vr/l/ja-guide.html', lang: 'ja'}).api.pageId(), 'l.ja-guide');
  assert.equal(browser({pathname: '/wuwa-vr/l/zh-Hans-privacy.html', lang: 'zh-Hans'}).api.pageId(), 'l.zh-hans-privacy');
  const b = browser({pathname: '/wuwa-vr/l/pt-BR.html', lang: 'pt-BR'});
  b.fire('pagehide');
  assert.deepEqual(b.events()[0].p, {page: 'l.pt-br', lang: 'pt-BR'});
});

check('events are batched: 20 at once, after 15 seconds, or immediately when asked', () => {
  const b = browser();
  assert.equal(b.timers[0].ms, 15000);
  b.runTimers();
  assert.equal(b.all().length, 1, 'the timer flushes the page load');
  for (let i = 0; i < 20; i++) b.api.track('faq_opened', {question: 'q1'});
  assert.equal(b.all().length, 2);
  assert.equal(b.all()[1].events.length, 20);
  b.api.track('download_clicked', {asset: 'installer'}, {now: true});
  assert.equal(b.all().length, 3);
});

check('a page sends at most 60 events', () => {
  const b = browser();
  for (let i = 0; i < 100; i++) b.api.track('faq_opened', {question: 'q1'});
  b.fire('pagehide');
  assert.equal(b.events().length, 60);
});

check('falls back to sendBeacon only where fetch is missing', () => {
  const b = browser({fetch: false});
  b.fire('pagehide');
  assert.equal(b.fetched.length, 0);
  assert.equal(b.sent.length, 1);
  assert.equal(b.sent[0].url, ENDPOINT);
  assert.equal(b.sent[0].body.events[0].e, 'app_loaded');
});

check('the Cloudflare beacon loads only with a valid token', () => {
  assert.equal(browser({config: {webAnalyticsToken: 'a'.repeat(32)}}).appended.length, 1);
  assert.equal(browser({config: {webAnalyticsToken: 'not-a-token'}}).appended.length, 0);
  const script = browser({config: {webAnalyticsToken: 'b'.repeat(32)}}).appended[0];
  assert.equal(script.src, 'https://static.cloudflareinsights.com/beacon.min.js');
  assert.deepEqual(JSON.parse(script.dataset.cfBeacon), {token: 'b'.repeat(32), spa: false});
});

check('an endpoint that is not https is ignored', () => {
  const b = browser({config: {analyticsEndpoint: 'http://collector.example/e'}});
  b.fire('pagehide');
  assert.deepEqual(b.all(), []);
});

let failed = 0;
for (const [name, fn] of checks) {
  try { fn(); console.log('ok   ' + name); }
  catch (error) { failed++; console.log('FAIL ' + name + '\n     ' + String(error.stack || error).split('\n').slice(0, 3).join('\n     ')); }
}
console.log(failed ? `${failed} of ${checks.length} tracking checks failed.` : `All ${checks.length} tracking checks passed.`);
process.exitCode = failed ? 1 : 0;
