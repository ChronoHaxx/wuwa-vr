# Site text

`home/<code>.json` holds the home page in ten languages: hero, setup steps,
features, first person, FAQ, limits and credits. Every file must have exactly
the English keys and list lengths, as plain text (the builder rejects markup).
Step names match the desktop launcher's own card titles in that language.
Risk text, everyday controls and the feedback form come from
`site/languages/<code>.js`.

`launcher-release.<en|zh-Hans>.json` describe the installed desktop app for the
English player guide and reference pages.

From the repository root:

```powershell
node dev/build-site.cjs --launcher-release
node dev/test-site-metadata.cjs
node dev/check-site.cjs <output-directory>
node dev/check-community.cjs <output-directory>
node dev/i18n/check-launcher-release.cjs <output-directory>
```

`node dev/build-home.cjs` rebuilds only the home pages, the languages page and
the capture guide, then refreshes the shared header/footer and metadata.

The generators need the existing `marked` dependency; browser checks use
`playwright`. `WUWA_NODE_MODULES` can point to an available dependency
directory, and `WUWA_CHROMIUM` to a Chromium executable if Playwright's own
browser build is missing. No installer, game or headset is launched.

Launcher screenshots in `site/media/launcher/` are rendered by the desktop
app itself (`--preview` with a `preview-state\preview.json` fixture) in each
language, for three states: `fresh`, `found` and `ready`.
