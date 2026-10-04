# Search verification and headset acceptance — 28 Sep 2026

## Search maintenance — 4 October 2026

The Google HTML verification file is retained byte-for-byte. The site now uses
one self-canonical URL per content page, with the homepage and sitemap pointing
to `https://chronohaxx.github.io/wuwa-vr/`. English and Simplified Chinese entry
pages identify Wuthering Waves / 鸣潮 VR and the Windows community beta clearly.
Sharing metadata uses an existing approved gameplay poster; no analytics was
added. Installer downloads, update feeds and page bodies are unchanged.

`dev/site-metadata.cjs` now owns the final sitemap and metadata pass for both
full and scoped site generation. `node dev/build-site.cjs --metadata-only`
refreshes just those fields. `node dev/test-site-metadata.cjs` checks all 21
content pages, repeat generation, and unchanged verification/feed files.

These are discovery improvements, not evidence that Google indexed the site.
In the verified owner's Search Console account, inspect the exact homepage URL
to distinguish an indexing exclusion from a ranking issue. Retain the existing
URL-prefix property and verification file, submit `/wuwa-vr/sitemap.xml`, and
request a recrawl if appropriate. Google says recrawling can take days to weeks
and requests do not guarantee inclusion.
[Google recrawl guidance](https://developers.google.com/search/docs/crawling-indexing/ask-google-to-recrawl),
[canonical URL guidance](https://developers.google.com/search/docs/crawling-indexing/consolidate-duplicate-urls).

## Historical acceptance record — 28 September

The owner confirmed the ultimate-return camera fix in headset testing for the
cases tested. This updates the earlier unaccepted-candidate wording; it does not
claim exhaustive testing of every character or acceptance of the synthetic input
and inventory tools. Foliage/prop mismatch remains open; reflection submenus stay
deferred. The guide, video, captions and transcript now reflect this distinction.

The 60-second video retains Claude Opus 5.5's original design and attribution.
Codex updated and re-rendered it without another Claude session. Its source and
updated verification receipt are in `media-source/explainer-20260928/`.

Google Search Console: use the HTML-file method for the URL-prefix property
`https://chronohaxx.github.io/wuwa-vr/`. The exact supplied file is published as
`site/google3ebe097a88f39e29.html`; retain it after verification. The site build
preserves that file. `dev/render-understanding.cjs` generates `site/sitemap.xml`
from the content pages and excludes verification tokens. Submit the public
sitemap URL after verification, then request indexing for the homepage.
Verification itself does not guarantee search indexing or ranking.
