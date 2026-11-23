# Site tracking plan

**Status: signed off 23 Nov 2026.** Hazimi chose the provider (Cloudflare) and
the visitor count (daily hash) for all four of his apps, and delegated the
rest of the sign-off to Claude ("do them all for me"). The decisions taken on
that basis are listed under [Sign-off](#sign-off). Any change to events or
properties updates this file first, in the same pull request.

Layer 1 (download counts) needs no tracking; see [METRICS.md](METRICS.md).
The launcher is covered in [LAUNCHER-METRICS-PROPOSAL.md](LAUNCHER-METRICS-PROPOSAL.md).

This plan follows the shared contract in THE FINALS Outfit Studio's plan
(`docs/analytics-tracking-plan.md` in ChronoHaxx/the-finals-outfit, PR #90):
the same collector, naming scheme, envelope, visitor definition and opt-out.

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

- **Page view:** one page load (`page_viewed`).
- **Daily visitor:** the collector hashes the request's IP address and browser
  string with a random salt for that UTC day, shared by all four apps and
  deleted after 48 hours. The hash goes on `page_viewed` only; other events
  carry no visitor key.
- **Weekly and monthly visitors:** a daily salt cannot recognise the same
  person on two different days, so a weekly or monthly figure is the sum of
  daily visitors. Report it as "visitor-days" or "monthly visits", never as
  "unique users".

## Questions and the measures that answer them

| Question | Measure | Decision it informs |
| --- | --- | --- |
| How many download and install, per release? | Layer 1: `installer`, `mod_package`, `app_update` per release | Whether a release reached people; how fast existing installs update |
| How many people visit? | Daily visitors from `page_viewed`; visitor-days per month | Growth, for showcasing |
| Which guides and pages are read, in which languages? | `page_viewed` by `page` and `lang` | Which pages to keep current first; which translations deserve a native speaker's review |
| Where do people drop off between landing, download and setup guide? | Landing `page_viewed` → `download_clicked` (installer) → GitHub `installer` count → guide `page_viewed` and `guide_section_reached` | Which step to fix first |
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
- Envelope added by the wrapper to every batch: `app` (`"wuwa-vr"`) and `schema` (`1`). The collector adds the time.
- WuWa VR adds two properties to every event: `page` (path only, e.g. `/guide.html`, `/l/ja-guide.html`) and `lang` (the page's language code).

## Events

| Event | Fires when | Properties (fixed values only) | Decision |
| --- | --- | --- | --- |
| `page_viewed` | Once per page load, after the gate passes | `page`, `lang` (common) | Visitors, pages and languages that matter |
| `download_clicked` | A download or release link is clicked | `asset`: `installer`, `portable_zip`, `release_notes`, `previous_release`, `source`; `placement`: `header`, `hero`, `closing`, `guide`, `page` | Which placement drives installs; whether the portable ZIP still earns its place in the hero |
| `guide_section_reached` | A guide or reference section first scrolls into view, once per section per page load | `section`: `start`, `controls`, `updates`, `recovery`, `recording`, `comfort` | How far down the setup guide people get; what to shorten or move up |
| `outbound_link_clicked` | A link leaves the site (other than downloads) | `target`: `github_issues`, `github_repo`, `youtube`, `kofi`, `uevr_docs`, `game`, `other` | Which channels people use |
| `feedback_draft_used` | A button on the feedback form is used | `action`: `copied`, `opened_github`; `kind`: `bug`, `feature`, `language`, `other` | Whether the form is used, and what for |
| `faq_opened` | A question under Questions is opened | `question`: position in the list, `q1`…`q12` | What people don't know |
| `shortcut_searched` | The shortcut finder is used, once after typing stops | `result`: `found`, `none` | Whether people find the shortcut they look for |
| `video_played` | A Watch video is started | `video`: YouTube id of a listed video; `placement`: `feature`, `grid`, `story` | Which clips to make more of |
| `language_chosen` | A language is picked | `to`: language code; `via`: `menu`, `suggestion` | Whether the language suggestion works |
| `controls_card_used` | The controls card is switched or saved | `pad`: `xbox`, `ps`, `vr`; `action`: `switched`, `saved_image` | Demand for PlayStation and VR controller labels |

Values outside a list become `other`. The wrapper drops unknown events and
properties, and the collector's allowlist (built from all four apps' plans)
drops them again.

## Delivery

- **One wrapper:** `track(event, props)` in `site/app.js` (already loaded with
  `defer`, so no new request). It checks the event and properties against the
  table above, adds `page` and `lang`, and queues the event.
- **Batching:** the queue is sent as one `navigator.sendBeacon` with a
  `text/plain` body (no CORS preflight) when the page is hidden, every
  15 seconds while events wait, or at 20 events. Nothing is retried, and the
  wrapper never throws. A download click therefore never waits for it.
- **Body:** `{"app":"wuwa-vr","schema":1,"events":[{"name":"download_clicked","props":{...}}]}`.
- **Endpoint:** the shared collector's address, set in `site/config.js`
  (`analyticsEndpoint`). Empty means nothing is sent: the site ships ready and
  switches on when the collector is deployed.
- **Cloudflare Web Analytics** (page views, referrers, countries, Core Web
  Vitals) loads after the page's `load` event, under the same gate, when
  `site/config.js` has `webAnalyticsToken`.
- **Opt-out:** GPC, DNT, or the "Don't count my visits" switch on the privacy
  page, which stores `wuwa-analytics = "off"`. It stops both the wrapper and
  the Cloudflare beacon.

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
- [ ] `track()` and its tests are in; tests pass, including nothing sent on opt-out.
- [ ] Privacy page live in ten languages and linked from every footer.
- [ ] Performance unchanged on a before/after comparison.
- [ ] Shared collector deployed and its address in `site/config.js`; Web Analytics token in `site/config.js` (owner, needs the Cloudflare login).
- [ ] Events seen live in the collector.
- [ ] DevTools on the live site: no cookies, no analytics storage, only allowed fields in requests.

## Sign-off

- Provider: Cloudflare Web Analytics + shared Worker collector writing to Analytics Engine. *Hazimi, 23 Nov 2026.*
- Visitor count: daily hash, for all four apps. *Hazimi, 23 Nov 2026 (in the WuWa Studio session).*
- Events: all ten rows above, aligned to the shared past-tense naming. *Claude, delegated by Hazimi, 23 Nov 2026.*
- Privacy note wording: as above. *Claude, delegated by Hazimi, 23 Nov 2026.*
- Contact route: GitHub issues, as on the rest of the site. *Claude, delegated.*
