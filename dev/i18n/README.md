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

## Where the text lives

| What players see | Source | Built by | Checked by |
| --- | --- | --- | --- |
| VR mod: the WuWa VR menu, controls cards, tips, notices, the classic UEVR panel | English in `mod/uevr/src`; translations in `mod/localization/wuwa/<lang>.json` | `dev/bundle-languages.py` (embedded in the plugin) | `mod_strings.py check` |
| Offline player guide (opened by the launcher) | `dev/i18n/guide/<lang>.json` | `dev/i18n/build_guide.py` → `launcher/native/PlayerGuide.html` | `build_guide.py --check` |
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

`dev/i18n/guide/en.json` holds the English guide as small HTML fragments (only
`<strong>`, `<code>`, `<em>` and links). Each language file has the
same keys, the same `{version}`, `{tag}` and `{game}` fields, tags and links.
After editing, run `python dev/i18n/build_guide.py` to rebuild
`launcher/native/PlayerGuide.html`; `--check` fails if the built file is out
of date. The launcher opens it at `#<lang>` (`#zh` for Simplified Chinese).

## Adding a language

Add `<lang>.json` beside the others in each source above (copy English and
translate), add the code to `LANGUAGES` in `mod_strings.py` and `check_all.py` and to
`ORDER` in `build_guide.py`, and to the launcher's language list. The plugin builds its
font ranges from the catalog's own characters; a script none of the bundled
fonts cover needs a font too.

# Site text

`home/<code>.json` holds the home page in ten languages: hero, setup steps,
features, first person, FAQ, limits and credits. Every file must have exactly
the English keys and list lengths, as plain text (the builder rejects markup).
Step names match the desktop launcher's own card titles in that language.
Risk text and the feedback form come from `site/languages/<code>.js`.

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
