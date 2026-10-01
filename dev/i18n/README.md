# Website translations

`site/l/<lang>/` holds every website page in nine more languages, built from the
English pages by `dev/translate-site.py`. Do not edit those HTML files: edit the
translation memory here and rebuild.

## How it works

`translate-site.py` splits each English page into units: paragraphs, list items,
table cells, headings, captions, labels, buttons, the page title and description,
image alt text and the feedback form's messages. Each unit is looked up in
`<lang>.json`:

```json
{"<16-character key>": {"en": "<English HTML>", "t": "<translated HTML>"}}
```

The key is a hash of the English text. If the English changes, the old entry no
longer matches, so that unit shows in English (and is listed as missing) until it
is translated again. Pages always build.

## Commands

```bash
npm run build                                   # English pages, then all translations
python dev/translate-site.py --missing out/     # also write out/<lang>.json: untranslated units
python dev/translate-site.py --prune            # drop entries no page uses any more
python dev/i18n/merge.py <lang> <file.json>     # add {"<key>": "<translation>"} pairs to <lang>.json
```

## Translating well

- Keep HTML tags, links, `<kbd>` keys and code exactly; translate only the words.
- Keep button and option names as they appear in the mod (`L3 + R3`,
  `WuWa Controls`, `Native Stereo Fix`), so players can find them.
- Use the game's official terms for its own features where they exist
  (Resonators, Echoes).
- Arabic pages are right-to-left; key combinations stay left-to-right.

The current translations are machine-assisted drafts. Corrections from native
speakers are welcome through a pull request or the feedback form.
