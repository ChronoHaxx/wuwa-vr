"use strict";
// Cookie-free usage counts, exactly as listed in docs/TRACKING-PLAN.md, in the shared usage-counter
// collector's format. Nothing identifies a visitor in the browser; nothing is sent off the published
// site, by automated browsers, with Global Privacy Control or Do Not Track on, after "Don't count my
// visits", or with no endpoint.
window.WuWaTrack = (() => {
  const languages = ["en", "zh-Hans", "ja", "ko", "es", "pt-BR", "fr", "de", "ru", "ar"];
  const EVENTS = {
    app_loaded: {},
    download_clicked: {asset: ["installer", "portable_zip", "release_notes", "previous_release", "source"], placement: ["header", "hero", "closing", "guide", "page"]},
    guide_section_reached: {section: ["start", "controls", "updates", "recovery", "recording", "comfort"]},
    outbound_link_clicked: {target: ["github_issues", "github_repo", "youtube", "kofi", "uevr_docs", "game"]},
    feedback_draft_used: {action: ["copied", "opened_github"], kind: ["bug", "feature", "language"]},
    faq_opened: {question: Array.from({length: 12}, (_, i) => "q" + (i + 1))},
    shortcut_searched: {result: ["found", "none"]},
    video_played: {video: "youtube", placement: ["feature", "grid", "story"]},
    language_chosen: {to: languages, via: ["menu", "suggestion"]},
    controls_card_used: {pad: ["xbox", "ps", "vr"], action: ["switched", "saved_image"]}
  };
  const OPT_OUT = "wuwa-analytics", HOST = "chronohaxx.github.io", BASE = "/wuwa-vr";
  const MAX_BATCH = 20, FLUSH_MS = 15000, MAX_PER_PAGE = 60;
  function create(env) {
    const {window: w, document: d, navigator: n, location: l} = env, config = env.config || {};
    const storage = () => { try { return w.localStorage; } catch { return null; } };
    let endpoint = "";
    try { const u = new URL(config.analyticsEndpoint || ""); if (u.protocol === "https:" && !u.username && !u.password) endpoint = u.href; } catch { /* unset: nothing is sent */ }
    const signal = () => n.globalPrivacyControl === true || n.doNotTrack === "1" || w.doNotTrack === "1";
    const optedOut = () => { try { return storage()?.getItem(OPT_OUT) === "off"; } catch { return false; } };
    // Measurement runs only on the published site, for people, without a privacy signal or opt-out.
    const allowed = () => l.protocol === "https:" && l.hostname === HOST && n.webdriver !== true && !signal() && !optedOut();
    const page = () => { const p = l.pathname.startsWith(BASE + "/") ? l.pathname.slice(BASE.length) : l.pathname; return p.endsWith("/") ? p + "index.html" : p; };
    const lang = () => languages.includes(d.documentElement.lang) ? d.documentElement.lang : "other";
    // The collector takes lowercase ids: "/l/ja-guide.html" is sent as "l.ja-guide".
    const pageId = () => page().replace(/^\//, "").replace(/\.html$/, "").replace(/\//g, ".").toLowerCase();
    const width = w.innerWidth || 0, formFactor = width < 640 ? "phone" : width < 1024 ? "tablet" : "desktop";
    const build = /^(?:[0-9a-f]{7}|dev)$/.test(config.siteBuild || "") ? config.siteBuild : "dev";
    let queue = [], timer = null, sent = 0;
    const flush = () => {
      if (timer) { env.clearTimeout(timer); timer = null; }
      if (!queue.length) return;
      const body = JSON.stringify({app: "wuwa-vr", schema: 1, form_factor: formFactor, build, events: queue});
      queue = [];
      if (!allowed() || !endpoint) return;
      try { if (env.fetch) { env.fetch(endpoint, {method: "POST", body, keepalive: true, mode: "no-cors", credentials: "omit", headers: {"Content-Type": "text/plain"}}).catch(() => {}); return; } } catch { /* fall through */ }
      try { n.sendBeacon?.(endpoint, body); } catch { /* never throw */ }
    };
    // Unknown events and properties are dropped; every listed property is sent, and a value outside
    // its list (or missing) becomes "other", because the collector keeps only exact matches.
    function track(name, props = {}, {now = false} = {}) {
      try {
        const spec = EVENTS[name];
        if (!spec || !endpoint || !allowed() || sent >= MAX_PER_PAGE) return false;
        const clean = {page: pageId(), lang: lang()};
        for (const [key, values] of Object.entries(spec)) {
          const value = String(props[key] ?? "");
          clean[key] = values === "youtube" ? (/^[\w-]{11}$/.test(value) && env.videos?.includes(value) ? value : "other") : values.includes(value) ? value : "other";
        }
        queue.push({e: name, p: clean}); sent++;
        if (now || queue.length >= MAX_BATCH) flush();
        else if (!timer) timer = env.setTimeout(flush, FLUSH_MS);
        return true;
      } catch { return false; }
    }
    const setOptOut = off => { try { off ? storage()?.setItem(OPT_OUT, "off") : storage()?.removeItem(OPT_OUT); } catch { /* storage blocked */ } if (off) queue = []; };
    return {track, flush, allowed, signal, optedOut, setOptOut, page, pageId, get endpoint() { return endpoint; }};
  }
  if (typeof document === "undefined") return {create, EVENTS};
  const videos = [...document.querySelectorAll("[data-youtube]")].map(el => el.dataset.youtube);
  const api = create({window, document, navigator, location, config: window.WUWA_SITE, videos,
    setTimeout: (f, ms) => setTimeout(f, ms), clearTimeout: id => clearTimeout(id), fetch: window.fetch?.bind(window)});
  addEventListener("visibilitychange", () => { if (document.visibilityState === "hidden") api.flush(); });
  addEventListener("pagehide", () => api.flush());
  // Cloudflare Web Analytics (page views) loads after the page, under the same rules.
  const token = window.WUWA_SITE?.webAnalyticsToken;
  if (/^[0-9a-f]{32}$/.test(token || "") && api.allowed()) {
    const load = () => { const s = document.createElement("script"); s.defer = true; s.src = "https://static.cloudflareinsights.com/beacon.min.js"; s.dataset.cfBeacon = JSON.stringify({token, spa: false}); document.head.append(s); };
    document.readyState === "complete" ? load() : addEventListener("load", load, {once: true});
  }
  return {...api, create, EVENTS};
})();
(() => {
  const track = (...args) => window.WuWaTrack?.track(...args);
  // One destination for navigation, forks and reviewable issue drafts.
  let repository;
  try {
    const candidate = new URL(window.WUWA_SITE?.repositoryUrl);
    if (candidate.protocol === "https:" && candidate.hostname === "github.com" &&
        !candidate.username && !candidate.password && !candidate.port && !candidate.search && !candidate.hash &&
        /^\/[a-z\d](?:[a-z\d-]*[a-z\d])?\/[a-z\d_.-]+\/?$/i.test(candidate.pathname)) {
      repository = candidate.origin + candidate.pathname.replace(/\/$/, "").replace(/\.git$/, "");
    }
  } catch { /* Unconfigured sites retain local guides and copy-only feedback. */ }
  const destinations = {code:"", releases:"/releases", issues:"/issues", fork:"/fork", contribute:"/blob/HEAD/CONTRIBUTING.md"};
  document.querySelectorAll("[data-project-link]").forEach(link => {
    if (repository && Object.hasOwn(destinations, link.dataset.projectLink)) {
      link.href = repository + destinations[link.dataset.projectLink];
      link.hidden = false;
    }
  });
  document.querySelectorAll("[data-repository-pending]").forEach(note => {note.hidden = !!repository;});
  const search = document.getElementById("control-search");
  const rows = [...document.querySelectorAll("[data-control-row]")];
  function filterControls() {
    const words = search.value.toLowerCase().trim().split(/\s+/).filter(Boolean);
    let visible = 0;
    for (const row of rows) {
      const text = (row.textContent + " " + (row.dataset.context || "")).toLowerCase();
      row.hidden = !words.every(word => text.includes(word));
      visible += !row.hidden;
    }
    document.getElementById("control-count").textContent = visible ? `${visible} shortcuts shown.` : "No matching shortcuts. Try HUD, camera, acro, or a button name.";
  }
  if (search) {
    document.querySelector(".control-filter").hidden = false;
    search.addEventListener("input", filterControls);
    // Count whether a search found anything, once typing stops; never the words typed.
    let searchTimer;
    search.addEventListener("input", () => {
      clearTimeout(searchTimer);
      searchTimer = setTimeout(() => { if (search.value.trim()) track("shortcut_searched", {result: rows.some(row => !row.hidden) ? "found" : "none"}); }, 1500);
    });
    document.getElementById("clear-search").addEventListener("click", () => {search.value = ""; filterControls(); search.focus();});
    filterControls();
  }
  const printButton = document.getElementById("print-guide");
  if (printButton) {printButton.hidden = false; printButton.addEventListener("click", () => {if(search){search.value="";filterControls();} window.print();});}
  const support = document.getElementById("support-link");
  if (support && window.WUWA_SITE?.supportUrl) {
    try {
      const url = new URL(window.WUWA_SITE.supportUrl);
      if (url.protocol !== "https:" || url.username || url.password || !url.hostname.includes(".") || url.hostname === "localhost") throw new Error("Use an owner-supplied public HTTPS payment page.");
      support.href = url.href;
      support.textContent = window.WUWA_SITE.supportLabel || "Support the projects";
      support.hidden = false;
      document.getElementById("support-pending").hidden = true;
      document.getElementById("support-external").hidden = false;
    } catch { /* Keep the informational fallback if configuration is invalid. */ }
  }

  // The treadmill run at the top of Watch: a muted preview loop that plays while it is on screen.
  // Reduced-motion and data-saver visitors start paused.
  const frame = document.getElementById("run-video");
  if (frame) {
    const video = frame.querySelector("video"), toggle = frame.querySelector(".video-toggle");
    const calm = matchMedia("(prefers-reduced-motion: reduce)").matches || navigator.connection?.saveData === true;
    if (video && toggle) {
      const show = playing => {
        toggle.dataset.state = playing ? "playing" : "paused";
        toggle.setAttribute("aria-label", playing ? toggle.dataset.pause : toggle.dataset.play);
      };
      let chosen = false; // The visitor's own play/pause choice always wins.
      const play = () => { video.preload = "auto"; video.play().catch(() => show(false)); };
      toggle.hidden = false; show(false);
      toggle.addEventListener("click", () => { chosen = true; video.paused ? play() : video.pause(); });
      video.addEventListener("play", () => show(true));
      video.addEventListener("pause", () => show(false));
      // Nothing downloads until the loop scrolls into view; it pauses again when it leaves.
      let inView = false;
      const follow = () => {
        if (calm || chosen) return;
        if (inView && !document.hidden) { if (video.paused) play(); } else if (!video.paused) video.pause();
      };
      document.addEventListener("visibilitychange", follow);
      if ("IntersectionObserver" in window)
        new IntersectionObserver(entries => { inView = entries[0].isIntersecting; follow(); }, {threshold: .35}).observe(frame);
      else if (!calm) play();
    }
  }
  // Every video (hero and Watch cards): contact YouTube only after the visitor chooses one.
  const still = matchMedia("(prefers-reduced-motion: reduce)").matches;
  document.querySelectorAll("[data-youtube-open]").forEach(link => link.addEventListener("click", event => {
    const box = link.closest("[data-youtube]");
    const id = /^[\w-]{11}$/.test(box?.dataset.youtube || "") ? box.dataset.youtube : "";
    if (!id) return;
    event.preventDefault();
    const player = document.createElement("iframe");
    player.src = `https://www.youtube-nocookie.com/embed/${id}?autoplay=1&rel=0&playsinline=1`;
    player.title = box.dataset.youtubeTitle || "YouTube";
    player.allow = "autoplay; encrypted-media; picture-in-picture; fullscreen";
    player.allowFullscreen = true;
    player.referrerPolicy = "strict-origin-when-cross-origin";
    track("video_played", {video: id, placement: box.id === "run-video" ? "feature" : box.closest(".story") ? "story" : "grid"});
    box.replaceChildren(player);
    box.classList.add("is-playing");
    box.scrollIntoView({block: "center", behavior: still ? "auto" : "smooth"});
    player.focus();
  }));

  // Launcher tour: each step card shows its screenshot.
  document.querySelectorAll(".tour").forEach(tour => {
    const steps = [...tour.querySelectorAll(".tour-step")], shots = [...tour.querySelectorAll(".tour-shot")];
    steps.forEach((step, index) => {
      const button = step.querySelector(".tour-select");
      if (!button) return;
      button.hidden = false;
      button.addEventListener("click", () => steps.forEach((other, i) => {
        other.classList.toggle("is-active", i === index);
        shots[i]?.classList.toggle("is-active", i === index);
        other.querySelector(".tour-select")?.setAttribute("aria-pressed", String(i === index));
      }));
    });
  });

  // The language menu closes like a menu: outside click or Escape.
  document.querySelectorAll(".lang-menu").forEach(menu => {
    document.addEventListener("click", event => { if (menu.open && !menu.contains(event.target)) menu.open = false; });
    menu.addEventListener("keydown", event => {
      if (event.key === "Escape" && menu.open) { menu.open = false; menu.querySelector("summary").focus(); }
    });
  });

  // Only remember the chosen language, never report text or device details.
  const languages = ["en", "zh-Hans", "ja", "ko", "es", "pt-BR", "fr", "de", "ru", "ar"];
  document.querySelectorAll("[data-language]").forEach(link => link.addEventListener("click", () => {
    try { localStorage.setItem("wuwa-language", link.dataset.language); } catch { /* Private browsing may block storage. */ }
  }));
  const remembered = document.getElementById("remembered-language");
  if (remembered) {
    try {
      const code = localStorage.getItem("wuwa-language");
      const source = [...document.querySelectorAll("[data-language]")].find(a => a.dataset.language === code);
      if (languages.includes(code) && source) {
        remembered.href = source.href;
        remembered.textContent = "Continue in " + source.textContent;
        remembered.hidden = false;
      }
    } catch { /* The static language links still work. */ }
  }

  // English home only: offer the visitor's own language once, unless they chose or dismissed.
  const suggest = document.getElementById("language-suggest");
  if (suggest && document.querySelector("[data-home]")?.dataset.home === "en") {
    try {
      const options = JSON.parse(suggest.dataset.options || "{}");
      const chosen = localStorage.getItem("wuwa-language");
      const match = tag => {
        const lower = String(tag).toLowerCase();
        if (/^zh(-(cn|sg|hans)\b|$)/.test(lower)) return "zh-Hans";
        if (lower.startsWith("pt")) return "pt-BR";
        return ["ja", "ko", "es", "fr", "de", "ru", "ar"].find(code => lower === code || lower.startsWith(code + "-"));
      };
      const code = chosen ? null : (navigator.languages || [navigator.language]).map(match).find(Boolean);
      if (code && options[code] && localStorage.getItem("wuwa-language-dismissed") !== code) {
        const [label, href] = options[code];
        const link = Object.assign(document.createElement("a"), {href, textContent: label, lang: code});
        link.dir = code === "ar" ? "rtl" : "ltr";
        link.dataset.language = code;
        link.addEventListener("click", () => { try { localStorage.setItem("wuwa-language", code); } catch { /* optional */ } });
        const close = Object.assign(document.createElement("button"), {type: "button", textContent: "×"});
        close.setAttribute("aria-label", "Dismiss");
        close.addEventListener("click", () => {
          suggest.hidden = true;
          try { localStorage.setItem("wuwa-language-dismissed", code); } catch { /* optional */ }
        });
        suggest.replaceChildren(link, close);
        suggest.hidden = false;
      }
    } catch { /* The language menu still works. */ }
  }

  const form = document.getElementById("report-form");
  if (form) {
    const messages = {pending:"No destination is connected. Copy your draft; nothing has been sent.",ready:"Review your draft on GitHub, then submit it yourself.",long:"Copy the draft and paste it into GitHub; it is too long for a link.",copied:"Copied. Nothing has been submitted.",fallback:"Select and copy the draft. Nothing has been submitted.",empty:"Add a summary and description."};
    const message = key => form.dataset[key] || messages[key];
    const kinds = ["bug", "feature", "language"];
    const kind = form.elements.kind;
    const requested = new URLSearchParams(location.search).get("kind");
    if (kinds.includes(requested)) kind.value = requested;
    // The launcher supplies only build/runtime labels, never private paths or logs.
    const build = new URLSearchParams(location.search).get("build");
    if (build) form.elements.version.value = build.slice(0, 300);
    const preview = document.getElementById("report-preview");
    const draft = document.getElementById("report-draft");
    const state = document.getElementById("report-status");
    const issue = document.getElementById("issue-link");
    form.addEventListener("input", () => {
      form.elements.title.setCustomValidity(""); form.elements.details.setCustomValidity("");
      preview.hidden = true; issue.hidden = true; issue.removeAttribute("href");
    });
    form.addEventListener("submit", event => {
      event.preventDefault();
      for (const name of ["title", "details"]) {
        const field = form.elements[name];
        field.setCustomValidity(field.value.trim() ? "" : message("empty"));
      }
      if (!form.reportValidity()) return;
      const selected = kinds.includes(kind.value) ? kind.value : "bug";
      const prefix = {bug:"Bug",feature:"Feature",language:"Language"}[selected];
      const title = `[${prefix}] ${form.elements.title.value.trim()}`;
      const parts = [`### Description\n${form.elements.details.value.trim()}`];
      if (form.elements.version.value.trim()) parts.push(`### Build / headset / runtime\n${form.elements.version.value.trim()}`);
      if (form.elements.language.value.trim()) parts.push(`### Language / translation\n${form.elements.language.value.trim()}`);
      parts.push(`### Report language\n${form.dataset.lang}`);
      const body = parts.join("\n\n");
      draft.value = title + "\n\n" + body;
      state.textContent = message("pending");
      issue.hidden = true;
      issue.removeAttribute("href");
      if (repository) {
        const url = new URL(repository + "/issues/new");
        url.searchParams.set("template", selected + ".md");
        url.searchParams.set("title", title);
        url.searchParams.set("body", body);
        if (url.href.length <= 7500) {
          issue.href = url.href; state.textContent = message("ready");
        } else {
          // GitHub rejects excessively long query URLs. Do not truncate a report.
          issue.href = repository + "/issues/new?template=" + selected + ".md";
          state.textContent = message("long");
        }
        issue.hidden = false;
      }
      preview.hidden = false;
      document.getElementById("draft-heading").focus();
    });
    document.getElementById("copy-report").addEventListener("click", async () => {
      track("feedback_draft_used", {action: "copied", kind: kind.value});
      try {
        if (!navigator.clipboard?.writeText) throw new Error("Clipboard unavailable");
        await navigator.clipboard.writeText(draft.value);
        state.textContent = message("copied");
      } catch {
        draft.focus(); draft.select();
        state.textContent = message("fallback");
      }
    });
    issue.addEventListener("click", () => track("feedback_draft_used", {action: "opened_github", kind: kind.value}, {now: true}));
    // A usable form is only revealed after submission is intercepted locally.
    const notice = document.querySelector(".report-intro .notice");
    if (repository && notice) notice.hidden = true;
    form.hidden = false;
  }
  // Controller section (home and guide): Xbox / PlayStation button names, and
  // callouts or table rows that light up their buttons on the drawings.
  const padSwitch = document.querySelector(".pad-switch");
  const pad = padSwitch?.closest("section"), padCard = pad?.querySelector(".pad-card");
  if (padSwitch && padCard) {
    const named = [...pad.querySelectorAll("[data-ps]")];
    named.forEach(el => {el.dataset.xbox = el.textContent;});
    // Thin lines from each callout to its button while the lists sit either side of
    // the drawing, ending in a dot at the button's edge (Y / X: between the two).
    const leaders = padCard.querySelector(".pad-leaders"), drawing = padCard.querySelector(".pad-svg");
    const drawLeaders = () => {
      leaders.replaceChildren();
      const matrix = drawing.getScreenCTM();
      if (!matrix || drawing.parentElement.querySelector(".pad-left").getBoundingClientRect().right > drawing.getBoundingClientRect().left + 1) return;
      const local = (x, y) => new DOMPoint(x, y).matrixTransform(matrix.inverse());
      for (const li of drawing.parentElement.querySelectorAll(".pad-list li[data-btn]")) {
        const targets = [...drawing.querySelectorAll(`[data-btn~="${li.dataset.btn}"]`)].map(e => e.getBoundingClientRect());
        if (!targets.length) continue;
        const r = li.getBoundingClientRect(), onLeft = !!li.closest(".pad-left"), edge = onLeft ? r.right : r.left;
        const start = local(edge, r.top + r.height / 2), bend = local(edge + (onLeft ? 14 : -14), r.top + r.height / 2);
        const centre = local(targets.reduce((s, t) => s + t.left + t.width / 2, 0) / targets.length,
          targets.reduce((s, t) => s + t.top + t.height / 2, 0) / targets.length);
        const radius = targets.length > 1 ? 0 : Math.min(targets[0].width, targets[0].height) / 2 * matrix.inverse().a + 3;
        const dx = centre.x - bend.x, dy = centre.y - bend.y, length = Math.hypot(dx, dy) || 1;
        const end = {x: centre.x - dx / length * radius, y: centre.y - dy / length * radius};
        const group = document.createElementNS("http://www.w3.org/2000/svg", "g");
        const line = document.createElementNS(group.namespaceURI, "path"), dot = document.createElementNS(group.namespaceURI, "circle");
        group.dataset.btn = li.dataset.btn;
        line.setAttribute("d", `M${start.x} ${start.y}H${bend.x}L${end.x} ${end.y}`);
        dot.setAttribute("cx", end.x); dot.setAttribute("cy", end.y); dot.setAttribute("r", "3.5");
        group.append(line, dot);
        leaders.append(group);
      }
    };
    if (window.ResizeObserver) new ResizeObserver(drawLeaders).observe(drawing.parentElement);
    document.fonts?.ready.then(drawLeaders);
    // The live 3D controller (pad3d.js, the in-VR model from media/pad3d.json) replaces the drawing where it can.
    let live = null;
    const base = document.currentScript?.src || document.querySelector('script[src$="app.js"]')?.src || location.href;
    if (window.fetch && document.createElement("canvas").getContext) {
      const script = document.createElement("script");
      script.src = new URL("pad3d.js", base).href;
      script.onload = () => fetch(new URL("media/pad3d.json", base)).then(r => r.ok ? r.json() : Promise.reject(r.status)).then(models => {
        live = window.WuWaPad3D?.attach(padCard, models, () => pad.dataset.pad || "xbox");
      }).catch(() => { /* the drawing stays */ });
      document.head.append(script);
    }
    const choose = (choice, remember) => {
      named.forEach(el => {el.textContent = choice === "ps" ? el.dataset.ps : choice === "vr" && el.dataset.vr ? el.dataset.vr : el.dataset.xbox;});
      pad.dataset.pad = choice;
      padSwitch.querySelectorAll("[data-pad-choice]").forEach(b => b.setAttribute("aria-pressed", String(b.dataset.padChoice === choice)));
      if (remember) try { localStorage.setItem("wuwa-pad", choice); } catch { /* optional */ }
      drawLeaders();
      live?.redraw();
    };
    padSwitch.addEventListener("click", event => {
      const button = event.target.closest("[data-pad-choice]");
      if (button) { choose(button.dataset.padChoice, true); track("controls_card_used", {pad: button.dataset.padChoice, action: "switched"}); }
    });
    try { const saved = localStorage.getItem("wuwa-pad"); if (saved === "ps" || saved === "vr") choose(saved, false); } catch { /* optional */ }
    padSwitch.hidden = false;
    pad.querySelector(".pad-save")?.addEventListener("click", () => track("controls_card_used", {pad: pad.dataset.pad || "xbox", action: "saved_image"}));
    const light = (id, on) => padCard.querySelectorAll(`.pad-svg [data-btn~="${id}"], .pad-list [data-btn="${id}"], .pad-leaders [data-btn="${id}"]`).forEach(el => el.classList.toggle("is-hot", on));
    padCard.querySelectorAll(".pad-list li[data-btn]").forEach(li => {
      for (const [type, on] of [["mouseenter", true], ["mouseleave", false]]) li.addEventListener(type, () => light(li.dataset.btn, on));
    });
    pad.querySelectorAll(".pad-page tr[data-btns]").forEach(row => {
      const page = row.closest(".pad-page"), ids = row.dataset.btns.split(" ").filter(Boolean);
      const mark = on => ids.forEach(id => page.querySelectorAll(`.pad-svg [data-btn~="${id}"]`).forEach(el => el.classList.toggle("is-hot", on)));
      row.addEventListener("mouseenter", () => mark(true)); row.addEventListener("mouseleave", () => mark(false));
    });
  }

  // Usage counts (docs/TRACKING-PLAN.md). Each call is a no-op unless measurement is on.
  track("app_loaded");
  const here = location.href, path = location.pathname;
  const currentTag = (/\/releases\/download\/([^/]+)\//.exec(document.querySelector(".header-download")?.href || "") || [])[1];
  const onGuide = /(guide|reference)\.html$/.test(path);
  const placement = link => link.closest(".site-header") ? "header" : link.closest("#release-download") ? "hero"
    : link.closest(".closing-card") ? "closing" : onGuide ? "guide" : "page";
  const outbound = url => {
    const host = url.hostname.replace(/^www\./, "");
    if (host === "github.com") return /^\/ChronoHaxx\/wuwa-vr\/issues(\/|$)/i.test(url.pathname) ? "github_issues" : /^\/ChronoHaxx\/wuwa-vr(\/|$)/i.test(url.pathname) ? "github_repo" : "other";
    if (host === "youtube.com" || host === "youtu.be") return "youtube";
    if (host === "ko-fi.com") return "kofi";
    if (host === "docs.uevr.io") return "uevr_docs";
    if (/(^|\.)kurogames\.com$|(^|\.)steampowered\.com$/.test(host)) return "game";
    return "other";
  };
  document.addEventListener("click", event => {
    const link = event.target.closest?.("a[href]");
    if (!link || link.matches("[data-youtube-open], #issue-link")) return;
    if (link.dataset.language) { track("language_chosen", {to: link.dataset.language, via: link.closest("#language-suggest") ? "suggestion" : "menu"}); return; }
    let url; try { url = new URL(link.href, here); } catch { return; }
    if (url.origin === location.origin) return;
    const release = /^\/ChronoHaxx\/wuwa-vr\/releases\/(?:download\/([^/]+)\/([^/?#]+)|tag\/([^/?#]+))/i.exec(url.hostname === "github.com" ? url.pathname : "");
    if (release) {
      const [, , file, notes] = release;
      const asset = notes ? (notes === currentTag ? "release_notes" : "previous_release")
        : /setup[^/]*\.exe$/i.test(file) ? "installer" : /source[^/]*\.zip$/i.test(file) ? "source" : /\.zip$/i.test(file) ? "portable_zip" : "other";
      track("download_clicked", {asset, placement: placement(link)}, {now: true});
      return;
    }
    track("outbound_link_clicked", {target: outbound(url)}, {now: true});
  }, true);
  // How far down the guide and reference pages people read: each section once per page load.
  if (onGuide && "IntersectionObserver" in window) {
    const sections = new Set(window.WuWaTrack?.EVENTS.guide_section_reached.section || []);
    const seen = new IntersectionObserver(entries => entries.forEach(entry => {
      if (!entry.isIntersecting) return;
      seen.unobserve(entry.target);
      track("guide_section_reached", {section: entry.target.id});
    }));
    document.querySelectorAll("section[id]").forEach(section => { if (sections.has(section.id)) seen.observe(section); });
  }
  // Which questions people open: by position, once each per page load.
  document.querySelectorAll(".faq details").forEach((item, index) => {
    let counted = false;
    item.addEventListener("toggle", () => { if (item.open && !counted) { counted = true; track("faq_opened", {question: "q" + (index + 1)}); } });
  });
  // The privacy page's "Don't count my visits" switch.
  const optSwitch = document.getElementById("analytics-switch"), optState = document.getElementById("analytics-state");
  if (optSwitch && optState && window.WuWaTrack) {
    const show = () => {
      const signal = window.WuWaTrack.signal(), off = signal || window.WuWaTrack.optedOut();
      optState.textContent = signal ? optState.dataset.signal : off ? optState.dataset.off : optState.dataset.on;
      optSwitch.textContent = off ? optSwitch.dataset.on : optSwitch.dataset.off;
      optSwitch.setAttribute("aria-pressed", String(off));
      optSwitch.disabled = signal;
    };
    optSwitch.addEventListener("click", () => { window.WuWaTrack.setOptOut(!window.WuWaTrack.optedOut()); show(); });
    optSwitch.hidden = false; optState.hidden = false;
    show();
  }
})();
