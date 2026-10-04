# Launcher release text

These English and Simplified Chinese files describe the installed desktop app.
They update the existing published site without importing the separate site redesign.
Other language starters keep their portable instructions with an explicit scope note.

From the repository root:

```powershell
node dev/build-site.cjs --launcher-release
node dev/i18n/check-launcher-release.cjs <output-directory>
node dev/check-site.cjs <output-directory>
node dev/check-community.cjs <output-directory>
```

The generator requires the existing `marked` dependency; browser checks use the
existing `playwright` dependency. `WUWA_NODE_MODULES` can point to an already
available dependency directory. No installer, game or headset is launched.

Do not label the old browser-launcher screenshots as images of the installed app.
The release flow uses three text steps until current native screenshots are ready.
The full `dev/check-site.cjs` checks the three-step flow and current download links
alongside its existing assets, links, accessibility, printing and layout checks.
