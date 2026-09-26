# License and community sharing

WuWa VR is a free, unofficial fan project. **There is no single license for
the combined mod.** Game content and upstream components keep their owners'
terms; this file cannot grant permission for those components.

## MIT for this project's original, independent work

Copyright (c) 2026 ChronoHaxx. The [MIT license](LICENSES/MIT.md) applies to:

- Original text in `README.md`, `SUPPORT.md`, `CONTRIBUTING.md`, `CREDITS.md`
  and `docs/`, excluding quoted third-party material.
- Original website HTML, CSS and JavaScript under `site/`, the translated
  getting-started text, and `site/media/mark.svg`.
- The original site generators and checks: `dev/build-site.cjs`,
  `dev/build-community.cjs`, `dev/site-layout.cjs`, `dev/check-site.cjs`,
  `dev/check-community.cjs`.
- Original local sharing configuration and its checks:
  `dev/configure-sharing.py` and `dev/test_configure_sharing.py`.
- The original GitHub issue/PR templates, funding configuration and website
  export README/workflow under `.github/` and `release/`.
- The original clean-recording helper `dev/steamvr-capture/record.cpp`,
  `dev/record-wuwa.py`, and `dev/recording-replay.html`. Their dependencies retain
  their own notices; this does not extend the grant to the injected backend.

This is an explicit scope list, not a grant for everything in this workspace.
Preserve the copyright and license notice when sharing covered work.

## Not covered by that MIT grant

- UEVR backend, native adaptations/patches and bundled dependencies.
- Community Lua/profile work and its adaptations, including polar, SannpoKun,
  mirudo2 and Indath contributions.
- Game code, assets, characters, screenshots, trademarks and other owners' work.
- The generated controller illustration is not represented as an official
  Xbox asset or as having an exclusive copyright grant from this project.

See [CREDITS.md](CREDITS.md) for component notices and provenance. The pinned
UEVR backend's root notice is All rights reserved; its SDK's separate MIT
notice does not cover the backend. Publishing a beta does not relicense those components or imply their authors
endorse this project. This does not restrict rights already granted by other owners.

## Our community request

Please keep accessible versions free, credit the original contributors, share
improvements, and welcome forks where the relevant component's license allows
them. Please do not sell repacks of this free project, gatekeep access or put
community improvements behind a paywall.

**That is a request, not an additional MIT condition.** MIT permits commercial
use and sale and does not require a fork to publish its source. We do not claim
otherwise. Donations to this project's author are entirely optional and do
not buy future support or maintenance.
