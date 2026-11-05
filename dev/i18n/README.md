# Translations

Everything a player reads ships in ten languages: English, Simplified Chinese,
Japanese, Korean, German, Spanish, French, Brazilian Portuguese, Russian and
Arabic (right to left). One command checks all of it, and releases run it
before packaging:

```powershell
python dev/i18n/check_all.py
```

It prints `All translations complete.` or lists what is missing and exits 1.
A new feature is not done until it passes.

The VR mod's catalogs are keyed by the English itself, so changed English shows
up as missing. The other sources are keyed by id, so `check_all.py` also keeps a
fingerprint of their English (`english-snapshot.json`): an edited or new English
value fails the check until every language has the new wording. Then record it:

```powershell
python dev/i18n/check_all.py --accept-english
```

## Where the text lives

| What players see | Source | Built by | Checked by |
| --- | --- | --- | --- |
| VR mod: the WuWa VR menu, controls cards, tips, notices, the classic UEVR panel | English in `mod/uevr/src`; translations in `mod/localization/wuwa/<lang>.json` | `dev/bundle-languages.py` (embedded in the plugin) | `mod_strings.py check` |
| Player guide on the website (`guide.html`, `l/<lang>-guide.html`) | `dev/i18n/guide/<lang>.json` | `dev/build-guide.cjs` (run by `dev/build-home.cjs`) | `node dev/build-guide.cjs --check` |
| Launcher app | `launcher/native/locales/<lang>.json` | the launcher build | `launcher/native/check-locales.py` |
| Portable web tool | `launcher/localization/launcher/<lang>.json` | served as is | `check_all.py` (same keys and list lengths as English) |
| Website home pages | `dev/i18n/home/<lang>.json` | `dev/build-home.cjs` | the site build |
| Website controls card and tables | `mod/controls.json`, words from the mod catalogs | `dev/controls-section.cjs` | `mod_strings.py check` |

## Adding or changing text in the VR mod

1. Write the English where it is drawn, through a translating call:
   `wuwa_ui::TextWrapped / Text / Button / draw...`, `tr()`, a VR menu row
   (`setting_toggle`, `setting_choice`, `action`, `paragraph`...), a controls
   card helper, or `N_("...")` for English kept in a table and drawn later.
   Plain `ImGui::Text("...")` with English in a `WuWa*` file fails the check.
   A new helper that draws text names its text parameter `english` and gets an
   entry in `CALLS` in `mod_strings.py`; the check fails until it does.
2. `python dev/i18n/mod_strings.py extract` adds new strings to `en.json`,
   drops ones no longer used from every catalog and refreshes `contexts.json`.
3. For each language, `python dev/i18n/mod_strings.py todo <lang>` prints the
   missing strings as JSON. Translate the values, then
   `python dev/i18n/mod_strings.py merge <lang> <file.json>`. Merge rejects a
   translation that breaks the rules below.
4. `python dev/bundle-languages.py`, then `python dev/i18n/check_all.py`.

Changing an English string changes its key, so every language needs the new
wording again: `extract` removes the old entry. Read the old translations
first if you want to reuse most of them.

## Rules for a translation

- Keep printf formats (`%d`, `%.1f`, `%s`) in the same order, `{L3}`-style
  button tokens and the number of line breaks, and never add `##` (ImGui
  would hide the rest as an id). A stray `%` in
  English makes every translation fail to load; write `%%` or rephrase.
- Button names stay as they are (L3, R3, LB, RB, LT, RT, A, B, X, Y, View,
  Menu), and so do product names (WuWa VR, UEVR, OpenXR, SteamVR).
- Use the words the game and the launcher already use in that language; the
  existing catalog is the glossary.
- Short labels must fit: render the cards and menus with the preview tool
  (`dev/imgui-preview`) in the language when a label is long.
- Machine-drafted text is welcome as a start. Say so in the change, and ask a
  speaker of the language to review it when you can.

## The player guide

The website is the one place player docs are written; the launcher and the
GitHub README link to it.

`dev/i18n/guide/en.json` holds the English guide as small HTML fragments (only
`<strong>`, `<code>`, `<em>` and links). Each language file has the
same keys, the same `{version}`, `{tag}` and `{game}` fields, tags and links.
Links to `https://chronohaxx.github.io/wuwa-vr/...` become relative on the site.
`dev/build-guide.cjs` builds `site/guide.html` (English) and
`site/l/<lang>-guide.html`, with the controls card and tables from
`mod/controls.json`; `--check` fails on a missing key or different markup.

The deeper English reference (`site/reference.html`: every shortcut with a
search, comfort, known issues) comes from `dev/site-content/*.md`, and the risk
notice from `dev/site-content/risk.md`. The GitHub README and the old
`docs/START-HERE.md`, `CONTROLS.md`, `COMFORT.md`, `TROUBLESHOOTING.md` and
`RISK.md` paths are generated pointers (`dev/build-readme.cjs`): never edit them
by hand.

After any change, rebuild from the repository root with
`node dev/build-site.cjs --launcher-release`. `node dev/check-site-sync.cjs`
rebuilds into a temporary copy and fails if any page, the README or a pointer
differs from what is committed; `check_all.py` runs it.

## Adding a language

Add `<lang>.json` beside the others in each source above (copy English and
translate), add the code to `LANGUAGES` in `mod_strings.py` and `check_all.py` and to
`languages` in `dev/site-layout.cjs`, and to the launcher's language list. The plugin builds its
font ranges from the catalog's own characters; a script none of the bundled
fonts cover needs a font too.

# Site text

`home/<code>.json` holds the home page in ten languages: hero, the Watch
section, setup steps, features, first person, FAQ, limits and credits. Every
file must have exactly the English keys and list lengths, as plain text (the
builder rejects markup). The Watch videos and their posters are listed in
`dev/site-content/watch.json`, with where each poster came from.
Risk text and the feedback form come from `site/languages/<code>.js`.

`launcher-release.<en|zh-Hans>.json` describe the current release for the
credits, test-notes and explainer pages.

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
