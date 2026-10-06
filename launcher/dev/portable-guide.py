"""Offline package guide derived from the actual bundled builds and profiles."""
from html import escape
from pathlib import PurePosixPath
import re
from urllib.parse import unquote, urlsplit

SETTINGS = (
    ('Native Stereo Fix', 'VR_NativeStereoFix'),
    ('Use Same Stereo Pass', 'VR_NativeStereoFixSamePass'),
    ('Stereo camera transition candidate', 'VR_WuWaStereoBasePose'),
    ('Extract additional game menus', 'VR_WuWaLguiMenuRedirect'),
    ('Per-eye reflection candidate', 'VR_WuWaPlanarEyeParameters'),
    ('Hide Kuro material reflections', 'VR_WuWaHideKuroReflections'),
    ('Early stereo view setup', 'VR_WuWaEarlyStereoViews'),
    ('Render-thread lag', 'Engine_r.OneFrameThreadLag'),
)
SETUP = (
    'Extract the whole ZIP and open WuWa VR Launcher.exe. Python is included.',
    'Connect the Xbox controller to the PC and start the headset software. For the target headset check, use Quest Pro through Steam Link/SteamVR.',
    'Choose the intended build in the launcher. A baseline is selected initially; select a candidate explicitly. Check “Next Apply & launch”.',
    'Choose the installed game copy, then acknowledge the risk notice and Apply & launch. Steam opens the Steam version automatically; for Kuro’s launcher, press Play after accepting the injector’s Windows prompt.',
    'L3 + R3 (both stick clicks), or Insert, opens UEVR settings. Close settings to use gameplay shortcuts. Leave the launcher running for recording.',
)
CHECKS = (
    ('Launch and recovery', 'Enter actual VR through the official game launcher, then check a normal exit. The injector reporting success alone is insufficient. Keep the build name and runtime with your results.'),
    ('Stereo and menus', 'Check Xuanfang Hold trees and other props, shadows, affected character materials, several newer-character ultimates and their return to gameplay, Resonators/Echo reflections, ESC/map and NPC labels in both eyes. Keep reflection hiding off when judging alignment; hiding an effect is a workaround.'),
    ('Controls and focus', 'Check ordinary Xbox movement/right-stick look, menu navigation, L3+B hide/show UI, L3+LB HUD/mouse mode where available, UEVR scrolling with RT released, and recovery after Alt-Tab.'),
    ('First person', 'Check backward/sideways movement, sprinting, character swaps, grapple, wing flight and head/body shadows. Full-animation motion and headset-aligned game targeting remain experimental.'),
    ('Recording and rollback', 'Try a short 30 fps recording and review its video/sidecars. SteamVR and the updated simulator are supported capture routes; VDXR video is not implemented. Close the game/injector before switching or restoring builds.'),
)
RECOVERY = (
    'Blurred ESC menu with no buttons: L3+B restores hidden game UI. Later builds also offer Show game UI now. Additional-menu extraction and Native Stereo Fix defaults depend on the build; see its supplied values below.',
    'The optional stereo-renderer comparison turns Native Stereo Fix off in both choices. Use its Restore button to return to your previous settings; choosing Use native stereo alone does not restore NSF. No renderer comparison is required for this playtest.',
    'Buttons move the HUD or mouse unexpectedly: L3+LB exits latched adjustment in newer builds. Release the controls before continuing. The in-game shortcut sheet describes older bindings.',
    'Use headset restores the OpenXR runtime saved before enabling the simulator; that may be VDXR rather than SteamVR. An explicit runtime switch requires the game/injector closed and may need Windows permission.',
    'Reset this build restores that build’s supplied settings with a backup. Restore my settings from before WuWa VR restores the saved profile and injector selection; it does not change the OpenXR runtime.',
    'Before deleting the package, restore the headset runtime if this package’s simulator is active, restore your settings, then Stop launcher. Keep backups and recordings if wanted.',
)


def values(config, cvars):
    result = {}
    wanted = {key for _, key in SETTINGS}
    for line in (config + '\n' + cvars).splitlines():
        key, sep, value = line.partition('=')
        if sep and key.strip() in wanted:
            key, value = key.strip(), value.strip()
            if key in result:
                raise ValueError('Duplicate supplied setting: ' + key)
            result[key] = {'true': 'On', 'false': 'Off'}.get(value.lower(), value)
    return result


def documents(catalog, profiles, sources, default):
    builds = catalog['builds']
    if {b['id'] for b in builds} != set(profiles) or default not in profiles:
        raise ValueError('Guide builds and supplied profiles disagree')
    md = ['# This WuWa VR package', '',
          'Use this guide for the builds in this ZIP. Supplied values below are read from their saved profiles; your own saved choices take priority.', '',
          'This is unofficial code injection. Anti-cheat restrictions or an account ban are possible. No account-safety guarantee is made. [Read the risk notice](RISK.md).', '',
          '## Start', '']
    md += [f'{i}. {item}' for i, item in enumerate(SETUP, 1)]
    compatibility = 'The standalone official launcher previously reached gameplay on the owner’s PC. The Steam route now starts through Steam and binds injection to the selected installation. Its background checks do not establish in-game compatibility: Steam injection previously failed and a new live playtest is pending. Epic remains manual and untested. SteamVR headset support is separate from Steam-store compatibility.'
    md += ['', compatibility, '', '## Included builds and supplied settings', '']
    tables = []
    for b in builds:
        role = 'Baseline · selected initially' if b['id'] == default else 'Candidate · select explicitly'
        settings = values(*profiles[b['id']])
        md += ['### ' + b['name'], '', role, '', b['summary'], '', b['known'], '',
               '| Setting | Supplied value |', '| --- | --- |']
        rows = [(label, settings.get(key, 'Not specified; code default applies')) for label, key in SETTINGS]
        md += [f'| {label} | {value} |' for label, value in rows]
        tag = sources[b['id']]['sourceTag']
        md += ['', f"Build: `{b['id']}`. Source: `{tag}`.", '', f"Backend SHA-256: `{b['sha256']}`.", '']
        tables.append((b, role, rows, tag))
    md += ['## One grouped playtest', '',
           'At the next normal playtest, report pass / fail / not tried for these groups together. No countdown or sequence of diagnostic-only relaunches is requested. Stop if it crashes or becomes uncomfortable.', '']
    md += [f'- [ ] **{title}:** {text}' for title, text in CHECKS]
    md += ['', '## Quick recovery', ''] + ['- ' + text for text in RECOVERY]
    md += ['', '[Controller reference](CONTROLS.md) · [Visual guide](../app/site/guide.html) · [Languages](../LANGUAGES.md)', '',
           'The visual guide shows previously captured builds, not proof that this candidate fixes an issue. Technical package notes are currently English; translated setup text is in the readme folder.', '']
    header = '''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>WuWa VR — this package</title><style>
:root{color-scheme:dark}body{background:#16181a;color:#eceeef;font:17px/1.6 system-ui,sans-serif;margin:0}main{max-width:62rem;margin:auto;padding:1.5rem}h1,h2,h3{line-height:1.25}h2{margin-top:2.2rem}a{color:#a5d4ff}a:focus-visible{outline:3px solid #ffd475;outline-offset:4px}li{margin:.55rem 0}code{overflow-wrap:anywhere;font-size:.87em}.table{overflow-x:auto}table{border-collapse:collapse;width:100%}th,td{padding:.55rem;text-align:left;border-bottom:1px solid #444}article{border-top:1px solid #555;margin-top:1.5rem;padding-top:.6rem}.muted{color:#bbc2c9}.controller{max-width:34rem;width:100%;height:auto}nav{display:flex;gap:1rem;flex-wrap:wrap}input{margin-right:.55rem}summary{cursor:pointer}details{margin:1rem 0}
</style><main><h1>WuWa VR — this package</h1><nav><a href="#start">Start</a><a href="#builds">Builds and settings</a><a href="#check">Playtest</a><a href="#recovery">Recovery</a><a href="app/site/guide.html">Visual guide</a></nav>
<p>Unofficial code injection can lead to anti-cheat restrictions or an account ban. <a href="RISK.md">Risk notice</a>.</p>
<p>Your saved settings take priority. The defaults below describe the profiles bundled in this ZIP.</p>'''
    parts = [header, '<h2 id="start">Start</h2><ol>']
    parts += ['<li>' + escape(t) + '</li>' for t in SETUP]
    parts += ['</ol><p>' + escape(compatibility) + '</p>',
              '<h2 id="builds">Included builds</h2>']
    for b, role, rows, tag in tables:
        parts += ['<article><h3>' + escape(b['name']) + '</h3><p class="muted">' + escape(role) + '</p>',
                  '<p>' + escape(b['summary']) + '</p><p>' + escape(b['known']) + '</p>',
                  '<div class="table"><table><thead><tr><th scope="col">Setting</th><th scope="col">Supplied value</th></tr></thead><tbody>']
        parts += ['<tr><th scope="row">' + escape(k) + '</th><td>' + escape(v) + '</td></tr>' for k, v in rows]
        parts += ['</tbody></table></div><details><summary>Build identity</summary><p><code>' + escape(b['id']) + '</code></p><p>Source: <code>' + escape(tag) + '</code></p><p>Backend SHA-256: <code>' + escape(b['sha256']) + '</code></p></details></article>']
    parts += ['<h2 id="check">One grouped playtest</h2><p>At the next normal playtest, report pass / fail / not tried for the groups together. No countdown or repeated short relaunches. Stop if it crashes or becomes uncomfortable.</p>',
              '<img class="controller" src="app/site/media/xbox-controller.png" alt="Xbox controller layout; use the in-game shortcut sheet for your build’s bindings"><ul>']
    parts += ['<li><label><input type="checkbox">' + escape(title) + '</label>: ' + escape(text) + '</li>' for title, text in CHECKS]
    parts += ['</ul><p>These checkboxes are a local reminder, not recorded acceptance.</p>', '<h2 id="recovery">Quick recovery</h2><ul>']
    parts += ['<li>' + escape(text) + '</li>' for text in RECOVERY]
    parts += ['</ul><p><a href="docs/CONTROLS.md">Controller reference</a> · <a href="app/site/guide.html#controls">Illustrated controls</a> · <a href="readme/en.txt">Setup text</a> · <a href="LANGUAGES.md">Languages</a></p>',
              '<p class="muted">Technical package notes are English. Translated setup is in the readme folder. The visual guide shows prior captures, not proof of a fix in this candidate. Tips are optional and do not buy promised support or updates.</p></main></html>']
    guide = '\n'.join(md)
    html = '\n'.join(parts)
    web = html.replace('app/site/', '').replace('href="RISK.md"', 'href="risk.html"')
    web = web.replace('href="docs/CONTROLS.md"', 'href="guide.html#controls"').replace('href="LANGUAGES.md"', 'href="languages.html"')
    web = web.replace(' · <a href="readme/en.txt">Setup text</a>', '')
    return {'BUILD GUIDE.html': html, 'app/site/package.html': web, 'docs/NEXT-SESSION.md': guide,
            'docs/START-HERE.md': '# Start this package\n\n[Open the package guide](../BUILD%20GUIDE.html) for the included builds, supplied settings, recovery and one grouped check.\n\n[Plain-text version](NEXT-SESSION.md).\n'}


def missing_links(documents, names):
    """Validate local guide targets against the planned package, not the repo."""
    known = {str(PurePosixPath(n)).casefold() for n in names}
    missing = []
    for name, text in documents.items():
        links = re.findall(r'(?:href|src)="([^"]+)"', text) if name.endswith('.html') else re.findall(r'\]\(([^)]+)\)', text)
        for href in links:
            ref = urlsplit(href)
            if ref.scheme or ref.netloc or not ref.path:
                continue
            parts = list(PurePosixPath(name).parent.parts)
            for part in unquote(ref.path).split('/'):
                if part == '..':
                    if not parts: break
                    parts.pop()
                elif part not in ('', '.'):
                    parts.append(part)
            else:
                target = '/'.join(parts)
                if target.casefold() in known:
                    continue
            missing.append((name, href))
    return missing
