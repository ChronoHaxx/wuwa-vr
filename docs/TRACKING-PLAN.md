# Site tracking plan

**Status: signed off 23 Nov 2026.** Hazimi chose the provider (Cloudflare) and
the visitor count (daily hash) for all four of his apps, and delegated the
rest of the sign-off to Claude ("do them all for me"). The decisions taken on
that basis are listed under [Sign-off](#sign-off). Any change to events or
properties updates this file first, in the same pull request.

Layer 1 (download counts) needs no tracking; see [METRICS.md](METRICS.md).
The launcher is covered in [LAUNCHER-METRICS-PROPOSAL.md](LAUNCHER-METRICS-PROPOSAL.md).

This plan follows the shared contract in THE FINALS Outfit Studio's plan
(`docs/analytics-tracking-plan.md` in ChronoHaxx/the-finals-outfit, PR #90) and
its collector, `usage-counter` (`workers/usage-counter/` there, Analytics Engine
dataset `usage_events`): the same naming scheme, envelope, visitor definition
and opt-out. Release downloads stay on the GitHub API (layer 1); only the
site's events go through the collector.

## Rules

- No cookies. Nothing in localStorage, sessionStorage or IndexedDB identifies a
  visitor. The one stored value is the opt-out switch, and it identifies nothing.
- No fingerprinting in the browser, no session replay, no heatmaps, no third-party trackers.
- No IP addresses stored. The shared collector reads the request's IP only to
  make the daily visitor hash (below), then drops it.
- Property values are ids from fixed lists in this repo. Never free text
  (search terms, form fields), never URLs with query strings.
- Events are sent only from the published site (`chronohaxx.github.io`): never
  from localhost, forks, previews or automated browsers (`navigator.webdriver`),
  and never with Global Privacy Control or Do Not Track on, or after the
  visitor opts out.
- Site performance stays as it is: no render-blocking script and no new file in the critical path.
- All ten languages keep working; the privacy note ships in all ten.

## Definitions

- **Page load:** `app_loaded`, once per page. (The name is the shared one; on
  this multi-page site it fires on every page.)
- **Daily visitor:** the collector hashes the request's IP address and browser
  string with a random salt for that UTC day and the app's name
  (SHA-256, first 64 bits). The salt expires an hour after its day ends, so no
  hash can be tied to an address or to another day, and the app's name in it
  keeps one person's hashes on two apps apart. The hash goes on `app_loaded`
  only; other events carry no visitor key.
- **Weekly and monthly visitors:** a daily salt cannot recognise the same
  person on two different days, so a weekly or monthly figure is the sum of
  daily visitors. Report it as "visitor-days" or "monthly visits", never as
  "unique users".

## Questions and the measures that answer them

| Question | Measure | Decision it informs |
| --- | --- | --- |
| How many download and install, per release? | Layer 1: `installer`, `mod_package`, `app_update` per release | Whether a release reached people; how fast existing installs update |
| How many people visit? | Daily visitors from `app_loaded`; visitor-days per month | Growth, for showcasing |
| Which guides and pages are read, in which languages? | `app_loaded` by `page` and `lang` | Which pages to keep current first; which translations deserve a native speaker's review |
| Where do people drop off between landing, download and setup guide? | Landing `app_loaded` → `download_clicked` (installer) → GitHub `installer` count → guide `app_loaded` and `guide_section_reached` | Which step to fix first |
| Which features or settings do people not know about? | `faq_opened`, `shortcut_searched`, `guide_section_reached` on controls | What to move into the home page, the guide or the launcher |

**The funnel compares daily totals.** Without a key shared between events, the
site cannot follow one person from landing to download. It compares totals at
each step (e.g. 1,000 landing views, 120 installer clicks, 90 installer
downloads, 70 guide views). People who arrive by a direct GitHub link skip the
site's steps, so a step can exceed the one before it.

## Naming scheme (shared with the portfolio, Outfit Studio and WuWa Studio)

- Event names: `object_action`, snake_case, verb in the past tense: `download_clicked`, `video_played`.
- Names never include the app's name; the envelope carries `app`.
- Property names: snake_case. Values: lowercase ids from a fixed list; `other` for anything unexpected.
  Booleans are `true`/`false`.
- Envelope added by the wrapper to every batch: `app` (`"wuwa-vr"`), `schema` (`1`), `form_factor`
  (`phone` under 640 px wide, `tablet` under 1024 px, else `desktop`) and `build` (the deployed
  commit's short id, stamped into `site/config.js` by the Pages workflow; `dev` otherwise). The
  collector adds the time.
- WuWa VR adds two properties to every event: `page` (an id from the path: `index`, `guide`,
  `l.ja-guide`, `l.zh-hans-privacy`) and `lang` (the page's language code, e.g. `zh-Hans`). With them,
  every event has at most four properties, the collector's limit.

## Events

| Event | Fires when | Properties (fixed values only) | Decision |
| --- | --- | --- | --- |
| `app_loaded` | Once per page load, after the gate passes | `page`, `lang` (common) | Visitors, pages and languages that matter |
| `download_clicked` | A download or release link is clicked | `asset`: `installer`, `portable_zip`, `release_notes`, `previous_release`, `source`; `placement`: `header`, `hero`, `closing`, `guide`, `page` | Which placement drives installs; whether the portable ZIP still earns its place in the hero |
| `guide_section_reached` | A guide or reference section first scrolls into view, once per section per page load | `section`: `start`, `controls`, `updates`, `recovery`, `recording`, `comfort` | How far down the setup guide people get; what to shorten or move up |
| `outbound_link_clicked` | A link leaves the site (other than downloads) | `target`: `github_issues`, `github_repo`, `youtube`, `kofi`, `uevr_docs`, `game`, `other` | Which channels people use |
| `feedback_draft_used` | A button on the feedback form is used | `action`: `copied`, `opened_github`; `kind`: `bug`, `feature`, `language`, `other` | Whether the form is used, and what for |
| `faq_opened` | A question under Questions is opened | `question`: position in the list, `q1`…`q12` | What people don't know |
| `shortcut_searched` | The shortcut finder is used, once after typing stops | `result`: `found`, `none` | Whether people find the shortcut they look for |
| `video_played` | A Watch video is started | `video`: YouTube id of a listed video; `placement`: `feature`, `grid`, `story` | Which clips to make more of |
| `language_chosen` | A language is picked | `to`: language code; `via`: `menu`, `suggestion` | Whether the language suggestion works |
| `controls_card_used` | The controls card is switched or saved | `pad`: `xbox`, `ps`, `vr`; `action`: `switched`, `saved_image` | Demand for PlayStation and VR controller labels |

Every listed property is always sent; a value outside its list, or a missing
one, becomes `other`. The wrapper drops unknown events and properties, and the
collector drops any event whose properties are not exactly its listed ones.

## Delivery

- **One wrapper:** `track(event, props)` in `site/app.js` (already loaded with
  `defer`, so no new request). It checks the event and properties against the
  table above, adds `page` and `lang`, and queues the event.
- **Batching:** the queue is sent as one `fetch` with `keepalive`,
  `credentials: "omit"`, `mode: "no-cors"` and a `text/plain` body (no CORS
  preflight; `navigator.sendBeacon` only where `fetch` is missing) when the
  page is hidden, every 15 seconds while events wait, at 20 events, or at once
  for a download or outbound click. At most 60 events per page load. Nothing is
  retried, and the wrapper never throws, so a click never waits for it.
- **Body:** `POST /e`, `{"app":"wuwa-vr","schema":1,"form_factor":"desktop","build":"e295ae9","events":[{"e":"download_clicked","p":{"page":"index","lang":"en","asset":"installer","placement":"hero"}}]}`.
- **Endpoint:** the shared collector's address, set in `site/config.js`
  (`analyticsEndpoint`). Empty means nothing is sent: the site ships ready and
  switches on when the collector is deployed.
- **Cloudflare Web Analytics** (page views, referrers, countries, Core Web
  Vitals) loads after the page's `load` event, under the same gate, when
  `site/config.js` has `webAnalyticsToken`.
- **Opt-out:** GPC, DNT, or the "Don't count my visits" switch on the privacy
  page, which stores `wuwa-analytics = "off"`. It stops both the wrapper and
  the Cloudflare beacon.

## Registering with the collector

The collector accepts an app only once it is listed in `APPS` in
`workers/usage-counter/src/schema.ts` (the-finals-outfit). WuWa VR is
registered there as of ChronoHaxx/the-finals-outfit@f6540a2 (PR #90), with the
entry below. Every payload the site produces was run through that commit's own
`readEnvelope` and `checkEvent`, unmodified: 39 events from three pages
(phone, tablet and desktop) all accepted, and a foreign origin rejected.
Both apps post from `https://chronohaxx.github.io`, so `app` is self-declared;
only Hazimi's sites live on that origin.

```ts
// WuWa VR guide site (github.com/ChronoHaxx/wuwa-vr, docs/TRACKING-PLAN.md). A multi-page site: one app_loaded per
// page load. Every event carries the page (an id such as "l.ja-guide") and the page's language.
const wuwaLangs = ["en", "zh-Hans", "ja", "ko", "es", "pt-BR", "fr", "de", "ru", "ar", "other"] as const;
const wuwaPage = [["page", id], ["lang", oneOf(...wuwaLangs)]] as const;
const youtube: Check = v => typeof v === "string" && /^(?:[\w-]{11}|other)$/.test(v);
const wuwaVr: AppSpec = {
  origins: ["https://chronohaxx.github.io"],
  visitorEvent: "app_loaded",
  envelope: "full",
  events: {
    app_loaded: wuwaPage,
    download_clicked: [...wuwaPage, ["asset", oneOf("installer", "portable_zip", "release_notes", "previous_release", "source", "other")], ["placement", oneOf("header", "hero", "closing", "guide", "page", "other")]],
    guide_section_reached: [...wuwaPage, ["section", id]],
    outbound_link_clicked: [...wuwaPage, ["target", oneOf("github_issues", "github_repo", "youtube", "kofi", "uevr_docs", "game", "other")]],
    feedback_draft_used: [...wuwaPage, ["action", oneOf("copied", "opened_github", "other")], ["kind", oneOf("bug", "feature", "language", "other")]],
    faq_opened: [...wuwaPage, ["question", id]],
    shortcut_searched: [...wuwaPage, ["result", oneOf("found", "none", "other")]],
    video_played: [...wuwaPage, ["video", youtube], ["placement", oneOf("feature", "grid", "story", "other")]],
    language_chosen: [...wuwaPage, ["to", oneOf(...wuwaLangs)], ["via", oneOf("menu", "suggestion", "other")]],
    controls_card_used: [...wuwaPage, ["pad", oneOf("xbox", "ps", "vr", "other")], ["action", oneOf("switched", "saved_image", "other")]],
  },
};
// ...and in APPS:  "wuwa-vr": wuwaVr,
```

Columns in `usage_events`: index1 `wuwa-vr`, blob1 event, blob2 form factor,
blob3 build, blob4 visitor hash (`app_loaded` only), blob5 `page`, blob6
`lang`, blob7–8 the event's own properties in the order above.

## Retention and reports

Analytics Engine keeps data for 3 months. After 2–4 weeks, a report run
locally with a read-only API token (never committed) fills the metrics
snapshot in [METRICS.md](METRICS.md): monthly visits, daily visitors, top
pages and languages, guide sections reached, and the funnel. Figures are
copied from query output with the date range, never estimated.

## Privacy note (English; the site carries it in ten languages)

> **Privacy**
>
> This site counts how it is used, so I can see which guides help and where
> people get stuck. It uses no cookies and stores nothing that identifies you.
>
> **What is counted:** which pages are opened, in which language; clicks on
> Download and on links to GitHub, YouTube and Ko-fi; how far down the guide
> people read; which questions are opened; whether the shortcut finder found
> something (never what you typed); and the feedback form's buttons.
>
> **How:** Cloudflare Web Analytics counts page views, and a small counter of
> our own, also on Cloudflare, counts the rest. To count daily visitors, your
> IP address and browser are mixed with a random key that is thrown away every
> day; your IP address is never stored. The data stays on Cloudflare for
> 3 months, then is deleted. Only daily totals are looked at.
>
> **Not done:** advertising, cross-site tracking, screen recording, selling or
> sharing data.
>
> **Your choice:** if your browser sends Global Privacy Control or Do Not
> Track, nothing is counted. You can also turn counting off for this browser
> with the switch below. It saves one setting in your browser and nothing else.
>
> **Downloads:** GitHub counts how often each release file is downloaded; we
> copy those totals once a day.
>
> **The WuWa VR app** sends no usage data. It contacts GitHub to check for
> updates and to download the VR mod.
>
> **Saved in your browser by this site:** your language and controller-label
> choice, if you pick one, so the next page matches. It never leaves your
> browser.
>
> **Other sites:** videos play from youtube-nocookie.com only after you choose
> one. GitHub, YouTube and Ko-fi have their own privacy policies.
>
> Questions: open an issue on GitHub.

## Done when

- [x] Plan signed off; provider and visitor count chosen.
- [x] `track()` in `site/app.js`; `node dev/test-track.cjs` (19 checks, including nothing sent with GPC, DNT,
  the switch, automated browsers, other hosts or no endpoint).
- [x] `node dev/check-site.cjs` runs the pages under the published address with a fixture collector: the
  events and fields sent, guide sections in Japanese, the Cloudflare beacon after `load`, no cookies, no
  storage, and nothing sent after the switch, with GPC, or with the empty published config.
- [x] Privacy page in ten languages (`privacy.html`, `l/<code>-privacy.html`), linked from every footer.
- [x] Performance (local, median of 5, 23 Nov 2026): same requests before `load` (15 home, 10 guide), no
  external requests, no cookies; LCP and DOMContentLoaded within run-to-run noise. Cost: `app.js` +3.4 KB
  gzipped (6.1 to 9.6 KB), loaded with `defer`.
- [ ] Privacy page published (merge to `main`).
- [ ] Shared collector deployed and its address in `site/config.js` (`analyticsEndpoint`); Web Analytics
  token in `site/config.js` (`webAnalyticsToken`). Needs the owner's Cloudflare login.
- [ ] Events seen live in the collector.
- [ ] DevTools on the live site: no cookies, no analytics storage, only allowed fields in requests.

## Sign-off

- Provider: Cloudflare Web Analytics + shared Worker collector writing to Analytics Engine. *Hazimi, 23 Nov 2026.*
- Visitor count: daily hash, for all four apps. *Hazimi, 23 Nov 2026 (in the WuWa Studio session).*
- Events: all ten rows above, aligned to the shared past-tense naming. *Claude, delegated by Hazimi, 23 Nov 2026.*
- Privacy note wording: as above. *Claude, delegated by Hazimi, 23 Nov 2026.*
- Contact route: GitHub issues, as on the rest of the site. *Claude, delegated.*
