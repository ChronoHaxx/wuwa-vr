# Site tracking plan (draft for sign-off)

**Status: draft. Nothing in this file is built.** Once the owner signs off
(bottom of the page), the `track()` wrapper, the counter and the privacy page
are built as written here. Any later change to events or properties updates
this file first.

Layer 1 (download counts) needs no tracking and is already running; see
[METRICS.md](METRICS.md). The launcher is a separate decision:
[LAUNCHER-METRICS-PROPOSAL.md](LAUNCHER-METRICS-PROPOSAL.md).

## Hard rules

- No cookies, and nothing stored on the visitor's device to recognise them.
- No fingerprinting and no visitor hash: nothing ties two page views to the same person.
- No IP addresses stored. No session replay, heatmaps or mouse tracking.
- No free text from visitors (search terms, form fields), no URLs with query strings.
- Global Privacy Control or Do Not Track in the browser means nothing loads and nothing is sent.
- Site performance stays as it is: no render-blocking script, no extra file in the critical path.
- All ten languages keep working; the privacy note ships in all ten.

## Questions and the measures that answer them

| Question | Measure | Decision it informs |
| --- | --- | --- |
| How many download and install, per release? | Layer 1: `installer`, `mod_package`, `app_update` per release | Whether a release reached people; how fast existing installs update |
| Which guides and pages are read, in which languages? | `page_view` per path (the path carries the language: `l/ja-guide.html`) | Which pages to keep current first; which translations deserve a native speaker's review |
| Where do people drop off between landing, download and setup guide? | Landing `page_view` → `download_click` (installer) → GitHub `installer` count → guide `page_view` and `guide_section_reach` | Which step to fix first |
| Which features or settings do people not know about? | `faq_open`, `shortcut_search`, `guide_section_reach` on controls | What to move into the home page, the guide or the launcher |

**The funnel is aggregate, by design.** With no visitor identifier, the site
cannot follow one person from landing to download. It compares daily totals at
each step (e.g. 1,000 landing views, 120 installer clicks, 90 installer
downloads, 70 guide views). People who come from a direct GitHub link skip the
site's steps, so a step can exceed the one before it.

## Events

Priority 1 is what the brief asks for. Priority 2 answers "what don't people
know about"; strike any you do not want.

Every event also carries the common properties: `site` (`wuwa-vr`), `page`
(path only, e.g. `/guide.html`, `/l/ja-guide.html`) and `lang` (the page's
language code).

| P | Event | Fires when | Properties (fixed values only) | Decision |
| --- | --- | --- | --- | --- |
| 1 | `page_view` | A page loads (Cloudflare Web Analytics, automatic) | path, referrer site, country, browser (Cloudflare's own) | Pages and languages that matter |
| 1 | `download_click` | A download link is clicked | `asset`: `installer`, `portable_zip`, `release_notes`, `previous_release`, `source`; `placement`: `header`, `hero`, `guide`, `page` | Which placement drives installs; whether the portable ZIP still earns its place in the hero |
| 1 | `guide_section_reach` | A guide section's heading first scrolls into view, once per section per page load | `section`: the heading's id (`start`, its setup steps, `controls`, `updates`, `recovery`, `recording`; reference page: `controls`, `comfort`, `recovery`) | How far down the setup guide people get; what to shorten or move up |
| 1 | `outbound_click` | A link leaves the site | `target`: `github_issues`, `github_repo`, `github_release`, `youtube`, `kofi`, `uevr_docs`, `other`; `placement` | Which feedback channel people use |
| 1 | `feedback_draft` | A button on the feedback form is used | `action`: `copy`, `open_github`; `kind`: the form's report types | Whether the form is used, and what for |
| 2 | `faq_open` | A question under Questions is opened | `question`: its position in the English list (`q1`…`qN`) | What people don't know |
| 2 | `shortcut_search` | The shortcut finder is used (once, after typing stops) | `result`: `found`, `none`; `match`: id of the first matching shortcut from `mod/controls.json`, never the typed text | Which features people look for and can't find |
| 2 | `video_play` | A Watch video is started | `video`: YouTube id from `dev/site-content/watch.json`; `placement`: `hero`, `grid`, `story` | Which clips to make more of |
| 2 | `language_choice` | A language is picked | `to`: language code; `via`: `menu`, `suggestion` | Whether the language suggestion banner works |
| 2 | `controls_view` | The controls card is switched or saved | `pad`: `xbox`, `ps`, `vr`; `action`: `switch`, `save_image` | Demand for PlayStation and VR controller labels |

Values outside a list become `other`. The counter rejects unknown events and
properties, so a mistake in the page can never send something new.

## Naming scheme (shared with the portfolio, THE FINALS Outfit Studio and WuWa Studio)

- Event names: `snake_case`, object then action, so related events sort together: `download_click`, `video_play`, `guide_section_reach`.
- Property names: `snake_case`. Values: lowercase ids from a fixed list in code; `other` for anything unexpected.
- Never: free text, user input, full URLs, query strings, timestamps finer than a day, or anything per visitor.
- Common properties on every event: `site`, `page`, `lang`.
- One shared dataset for all four projects, keyed by `site`, so one query compares them.

## Provider

**Chosen by the owner (23 Nov 2026) for all four projects: Cloudflare for both
halves.** Cloudflare Web Analytics for
page views, and a small Cloudflare Worker that writes events to Workers
Analytics Engine. It is the only option here that keeps no visitor identifier
at all, it is free for all four projects, and Cloudflare is already in use
(Outfit Studio's Workers, the homelab tunnel).

| | Cloudflare Web Analytics + Worker | Umami Cloud | Plausible | GoatCounter |
| --- | --- | --- | --- | --- |
| Cost for four sites | Free (Analytics Engine: 100,000 events a day included) | Hobby is free but limited to 100,000 events a month and 1 site (some sources say 3); otherwise $20 a month or self-host | Paid only, from about $9 a month (check current pricing) | Free for non-commercial use |
| Custom events with properties | Yes (own Worker, up to 20 fields) | Yes | Yes | Counts only, no properties |
| How visitors are counted | No identifier: Cloudflare counts a "visit" when a page view comes from another site | Hash of IP, browser and a rotating salt | Hash of IP and browser, salt rotates daily | Hash for unique visitors |
| Fits "no fingerprinting" literally | Yes | Borderline (a hashed IP and browser) | Borderline | Borderline |
| Dashboard | Page views: yes. Events: SQL; a monthly job exports totals to the `metrics` branch | Yes, with funnels | Yes, funnels on higher plans | Basic |
| Retention | Analytics Engine keeps about three months (verify); the monthly export keeps it all | 6 months on Hobby | Plan-dependent | Unlimited |
| Script weight | Cloudflare beacon, loaded after the page; our wrapper inside the existing `app.js` | ~2 KB | <1 KB | ~3.5 KB |

What Cloudflare costs you: about an hour of setup (below), and no
click-around dashboard for the custom events. Unique visitors are not
available with any no-identifier setup; report "visits" and say so.

## How it works once approved

- **Wrapper:** one `track(event, props)` in `site/app.js` (already loaded with
  `defer`; no new file). It checks the event and properties against the
  allow-list above, adds `site`, `page` and `lang`, and sends with
  `navigator.sendBeacon` (falls back to `fetch` with `keepalive`), so a
  download click never waits for it. It also loads the Cloudflare beacon after
  the page has finished loading, and only when measurement is on.
- **Opt-out:** Global Privacy Control, Do Not Track, or the switch on the
  privacy page stops both the beacon and `track()`. The switch saves one value
  (`wuwa-measure: off`) in the browser; it is a preference like the existing
  language choice, not an identifier, and exists only if someone opts out.
- **Counter:** a Cloudflare Worker (`workers/metrics/` in this repository)
  accepts events only from the site's origin, checks them against the same
  allow-list, and writes one Analytics Engine row: `site`, event, `page`,
  `lang` and up to three properties. It does not read or store the IP, the
  browser string or the country, and request logging stays off.
- **Reports:** `node dev/site-metrics.cjs` prints the funnel and top pages from
  the Analytics Engine SQL API with a read-only token. A monthly workflow
  writes the same totals to the `metrics` branch beside the download counts.
- **Tests (Node, no browser):** nothing is sent with GPC, DNT or the switch;
  unknown events and properties are dropped; the payload holds only allowed
  keys; the page path loses its query string and hash; `sendBeacon` falls back
  to `fetch`. A Playwright check opens the built site and asserts no cookies,
  no analytics storage and only the expected requests.
- **Performance:** compared before and after on `index.html` and `guide.html`:
  requests in the critical path, bytes before first render, and Largest
  Contentful Paint. The beacon must not appear before the `load` event.
- **Languages:** the privacy page and a footer link in all ten languages
  through the existing i18n sources; machine-drafted translations flagged for
  review; `python dev/i18n/check_all.py` passes.

### Owner setup (needs your Cloudflare login)

1. Cloudflare dashboard → Web Analytics → add site `chronohaxx.github.io` (JS
   snippet mode) → copy the site token.
2. Deploy the Worker: `npx wrangler deploy` in `workers/metrics/` (creates the
   Analytics Engine dataset on first write). Pick its address: a
   `workers.dev` subdomain or a route on a domain you own.
3. Create an API token with **Account Analytics: Read** and save it as the
   repository secret `CF_ANALYTICS_TOKEN` (for the monthly export).

## Privacy note (English draft for the site)

> **Privacy**
>
> This site counts visits without knowing who you are.
>
> **What is counted:** which pages are opened and in which language; clicks on
> Download, on links to GitHub, YouTube and Ko-fi, and on the feedback form's
> buttons; how far down the guide people read; which questions are opened; and
> whether the shortcut finder found something (never what you typed).
>
> **How:** Cloudflare Web Analytics counts page views, and our own small
> counter, also on Cloudflare, counts the clicks. No cookies. Nothing is saved
> on your device to recognise you, no fingerprint is taken, and your IP address
> is not stored. Like any website, the server sees your IP address to answer
> the request; it is not kept in the counts. Counts are added up per day, and
> no single visit can be looked at.
>
> **Not done:** advertising, cross-site tracking, screen recording, selling or
> sharing data.
>
> **Your choice:** if your browser sends Global Privacy Control or Do Not
> Track, nothing is counted. You can also turn counting off for this browser:
> [Don't count my visits]. That saves one setting in your browser and nothing
> else.
>
> **Downloads:** GitHub counts how often each release file is downloaded; we
> copy those totals once a day.
>
> **The WuWa VR app** sends no usage data. It contacts GitHub to check for
> updates and to download the VR mod.
>
> **Stored on your device by the site:** your language and controller-label
> choice, if you pick one, so the next page matches. It never leaves your
> browser.
>
> **Other sites:** videos play from youtube-nocookie.com only after you choose
> one. GitHub, YouTube and Ko-fi have their own privacy policies.

## Done when

- [x] Provider named: Cloudflare (23 Nov 2026).
- [ ] Owner signs off the events and the privacy note.
- [ ] `track()` and its tests are in; tests pass.
- [ ] Events seen live in the counter (sent from a test page with `site=wuwa-vr-test`).
- [ ] DevTools on the live site: no cookies, no analytics storage, only allowed fields in requests.
- [ ] Privacy note live in ten languages and linked from every footer.
- [ ] Performance unchanged on the before/after comparison.

## Sign-off

- [ ] Events (strike any priority 2 rows you don't want): ________
- [x] Provider: Cloudflare Web Analytics + Worker with Analytics Engine (owner, 23 Nov 2026)
- [ ] Privacy note wording: ________
- Signed: ________ Date: ________
