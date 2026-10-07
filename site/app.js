"use strict";
(() => {
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

  // Hero: a muted preview loop. Reduced-motion and data-saver visitors start paused.
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
      // Browsers may refuse playback in a hidden tab; start once the page is visible.
      document.addEventListener("visibilitychange", () => {
        if (!calm && !chosen && !document.hidden && video.paused) play();
      });
      if (!calm) play();
    }
    // Contact YouTube only after the visitor asks for the full run.
    const id = /^[\w-]{11}$/.test(frame.dataset.youtube || "") ? frame.dataset.youtube : "";
    if (id) document.querySelectorAll("[data-youtube-open]").forEach(link => link.addEventListener("click", event => {
      event.preventDefault();
      const player = document.createElement("iframe");
      player.src = `https://www.youtube-nocookie.com/embed/${id}?autoplay=1&rel=0&playsinline=1`;
      player.title = frame.dataset.youtubeTitle || "YouTube";
      player.allow = "autoplay; encrypted-media; picture-in-picture; fullscreen";
      player.allowFullscreen = true;
      player.referrerPolicy = "strict-origin-when-cross-origin";
      frame.replaceChildren(player);
      frame.scrollIntoView({block: "center", behavior: calm ? "auto" : "smooth"});
      player.focus();
    }));
  }

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
      try {
        if (!navigator.clipboard?.writeText) throw new Error("Clipboard unavailable");
        await navigator.clipboard.writeText(draft.value);
        state.textContent = message("copied");
      } catch {
        draft.focus(); draft.select();
        state.textContent = message("fallback");
      }
    });
    // A usable form is only revealed after submission is intercepted locally.
    const notice = document.querySelector(".report-intro .notice");
    if (repository && notice) notice.hidden = true;
    form.hidden = false;
  }
})();
