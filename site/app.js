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
