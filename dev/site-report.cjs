// Original site tooling, MIT. The local-only report form shared by the
// feedback page and every home page; app.js prepares drafts, never posts them.
const {escape: esc} = require('./site-layout.cjs');
function report(t) {
  return `<div class="report-intro"><p>${esc(t.reportIntro)}</p><p class="notice">${esc(t.pending)}</p><p lang="en"><a data-project-link="issues" hidden rel="noopener noreferrer">Search existing GitHub issues</a></p></div>
<form class="report-form" id="report-form" hidden data-lang="${t.lang}" data-pending="${esc(t.pending)}" data-ready="${esc(t.ready)}" data-long="${esc(t.tooLong)}" data-copied="${esc(t.copied)}" data-fallback="${esc(t.copyFallback)}" data-empty="${esc(t.empty)}">
<label for="report-kind">${esc(t.kind)}</label><select id="report-kind" name="kind">${['bug', 'feature', 'language'].map((id, i) => `<option value="${id}">${esc(t.kinds[i])}</option>`).join('')}</select>
<label for="report-title">${esc(t.titleLabel)}</label><input id="report-title" name="title" required maxlength="140" dir="auto" autocomplete="off">
<label for="report-details">${esc(t.detailsLabel)}</label><textarea id="report-details" name="details" rows="5" required maxlength="10000" dir="auto" aria-describedby="report-privacy"></textarea>
<label for="report-version">${esc(t.versionLabel)}</label><input id="report-version" name="version" maxlength="300" dir="auto" autocomplete="off">
<label for="report-language">${esc(t.languageLabel)}</label><input id="report-language" name="language" maxlength="160" dir="auto" autocomplete="off">
<p class="small" id="report-privacy">${esc(t.privacy)}</p><button type="submit">${esc(t.prepare)}</button>
</form><section class="report-preview" id="report-preview" hidden><h3 id="draft-heading" tabindex="-1">${esc(t.preview)}</h3><textarea id="report-draft" rows="12" readonly dir="auto" aria-labelledby="draft-heading"></textarea><p id="report-status" role="status" aria-live="polite"></p><div class="actions"><a id="issue-link" class="button" hidden target="_blank" rel="noopener noreferrer">${esc(t.openIssue)}</a><button id="copy-report" type="button">${esc(t.copy)}</button></div></section>
<noscript><p>${esc(t.privacy)}</p><p>${esc(t.pending)}</p><p>${esc(t.titleLabel)} · ${esc(t.detailsLabel)} · ${esc(t.versionLabel)} · ${esc(t.languageLabel)}</p></noscript>`;
}
module.exports = {report};
