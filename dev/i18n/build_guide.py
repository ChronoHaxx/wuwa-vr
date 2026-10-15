#!/usr/bin/env python3
"""Build launcher/native/PlayerGuide.html, the offline player guide the launcher
opens at the player's language, from dev/i18n/guide/<lang>.json.

    python dev/i18n/build_guide.py           write the guide
    python dev/i18n/build_guide.py --check   exit 1 if a language is incomplete or the guide is stale

en.json is the source. Each language has the same keys; values may use
<strong>, <code>, <em> and <a href="..."> (same tags and links as English) and
{version} {tag} {game}, filled from the launcher's version and the catalog's
game version. The launcher opens #en, #zh (Simplified Chinese) or #<code>, and
the Controls link #controls (#controls-zh, #controls-<code>).

The controls tables are not written here: they come from mod/controls.json,
in the words of the VR mod's catalogs (mod/localization/wuwa), as the website
and the in-VR controls cards do, so a shortcut is changed in one place.
"""
import html
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ROOT / 'dev/i18n/guide'
OUT = ROOT / 'launcher/native/PlayerGuide.html'
ORDER = ['en', 'zh-Hans', 'ja', 'ko', 'de', 'es', 'fr', 'pt-BR', 'ru', 'ar']
TAG = re.compile(r'<(/?)(strong|code|em|a)(\s[^>]*)?>')
HREF = re.compile(r'href="([^"]*)"')
FIELD = re.compile(r'\{(version|tag|game)\}')

STYLE = (':root{color-scheme:dark}*{box-sizing:border-box}body{margin:0;background:#10191d;color:#edf3f1;'
         'font:17px/1.65 system-ui,"Microsoft YaHei","Yu Gothic UI","Malgun Gothic","Segoe UI",sans-serif}'
         'main{max-width:840px;margin:auto;padding:32px 24px 56px}a{color:#82dfc7}nav{display:flex;flex-wrap:wrap;gap:10px 20px;font-size:14px}'
         'h1{font-size:34px;line-height:1.2;margin-bottom:10px}h2{margin-top:30px;font-size:23px}h3{font-size:18px;margin:0 0 8px}'
         '.muted{color:#a5b8be}.card{background:#19262c;border:1px solid #30454d;border-radius:12px;padding:18px;margin:12px 0}'
         '.notice{border-left:3px solid #dfbd82;padding:10px 16px;background:#29291f}[dir=rtl] .notice{border-left:0;border-right:3px solid #dfbd82}'
         'section{scroll-margin-top:20px}p{margin:8px 0 12px}li{margin:7px 0}code{color:#82dfc7;overflow-wrap:anywhere}'
         'hr{border:0;border-top:1px solid #30454d;margin:40px 0}footer{margin-top:24px;font-size:14px}'
         'table{width:100%;border-collapse:collapse;font-size:15px}th,td{text-align:start;vertical-align:top;padding:9px 8px;border-bottom:1px solid #30454d}'
         'td:first-child{width:43%;color:#82dfc7;font-weight:600}th.when{color:#a5b8be;font-weight:400;font-size:14px;padding-top:14px}'
         'h3.group{margin:22px 0 4px;color:#dfbd82;font-size:15px;letter-spacing:.04em}details{margin:14px 0}summary{cursor:pointer;color:#82dfc7}'
         '@media(max-width:480px){main{padding:24px 16px}h1{font-size:29px}.card{padding:14px}th,td{padding:8px 4px}}')
CONTROLS = ROOT / 'mod/controls.json'
CATALOGS = ROOT / 'mod/localization/wuwa'
HOME = ROOT / 'dev/i18n/home'
# Rows that only apply in a mode get a heading naming it and its shortcut, as on the website.
CONTEXT = {'freecam': ('freecam', 'Double R3'), 'mouse': ('mouse', 'L3 + LB'), 'uevr': ('uevr', 'L3 + R3')}


def load(code):
    path = SOURCES / f'{code}.json'
    return json.loads(path.read_text(encoding='utf-8')) if path.exists() else None


def fields():
    info = (ROOT / 'launcher/native/AssemblyInfo.cs').read_text(encoding='utf-8')
    version = re.search(r'AssemblyInformationalVersion\("([^"]+)"\)', info).group(1)
    game = json.loads((ROOT / 'launcher/native/catalog.json').read_text(encoding='utf-8-sig'))['releases'][0]['gameVersion']
    return {'version': version, 'tag': 'beta-' + version.replace('.', '-'), 'game': game}


def controls_tables(code):
    """One table per group of mod/controls.json, in the catalog's words (English where a
    catalog has no entry; mod_strings.py check fails while any language lacks one)."""
    words = {} if code == 'en' else json.loads((CATALOGS / f'{code}.json').read_text(encoding='utf-8'))['strings']
    pad = json.loads((HOME / f'{code}.json').read_text(encoding='utf-8'))['pad']
    t = lambda english: html.escape(words.get(english) or english)
    out = []
    for group in json.loads(CONTROLS.read_text(encoding='utf-8'))['groups']:
        rows, context = [], None
        for row in group['rows']:
            if row.get('when') != context:
                context = row.get('when')
                if context:
                    label, keys = CONTEXT[context]
                    rows.append(f'<tr><th class="when" colspan="2">{html.escape(pad[label])} · <bdi dir="ltr">{t(keys)}</bdi></th></tr>')
            rows.append(f'<tr><td><bdi dir="ltr">{t(row["keys"])}</bdi></td><td>{t(row["action"])}</td></tr>')
        title = re.sub(r'^\d+\s+', '', words.get(group['title']) or group['title'])
        out.append(f'<h3 class="group">{html.escape(title)}</h3>\n<table><tbody>\n' + '\n'.join(rows) + '\n</tbody></table>')
    return '\n'.join(out)


def problems_for(code, data, english):
    out = []
    if not data:
        return [f'{code}: dev/i18n/guide/{code}.json is missing']
    strings = data.get('strings', {})
    for key, source in english.items():
        value = strings.get(key)
        if not isinstance(value, str) or not value.strip():
            out.append(f'{code}.{key}: missing')
            continue
        if sorted(m.group(0) for m in FIELD.finditer(source)) != sorted(m.group(0) for m in FIELD.finditer(value)):
            out.append(f'{code}.{key}: {{version}} / {{tag}} / {{game}} differ from English')
        if sorted(t.group(2) for t in TAG.finditer(source)) != sorted(t.group(2) for t in TAG.finditer(value)):
            out.append(f'{code}.{key}: markup differs from English')
        if sorted(HREF.findall(source)) != sorted(HREF.findall(value)):
            out.append(f'{code}.{key}: links differ from English')
        if re.search(r'<(?!/?(strong|code|em|a)[\s>])', value):
            out.append(f'{code}.{key}: only <strong>, <code>, <em> and <a> are allowed')
    for key in strings:
        if key not in english:
            out.append(f'{code}.{key}: not in English (stale)')
    return out


def section(code, data, english, values):
    s = {k: data['strings'].get(k) or english[k] for k in english}
    t = lambda key: FIELD.sub(lambda m: values[m.group(1)], s[key])
    anchor = 'zh' if code == 'zh-Hans' else code
    suffix = '' if code == 'en' else '-' + anchor
    direction = data.get('direction', 'ltr')
    release = f'https://github.com/ChronoHaxx/wuwa-vr/releases/tag/{values["tag"]}'
    return f'''<section id="{anchor}" lang="{code}" dir="{direction}">
<h1>{t("title")}</h1>
<p class="muted">{t("subtitle")}</p>
<p>{t("intro")}</p>
<div class="card"><h3>{t("step1.title")}</h3>
<p>{t("step1.body")}</p></div>
<div class="card"><h3>{t("step2.title")}</h3>
<p>{t("step2.body")}</p>
<p>{t("step2.parts")}</p></div>
<div class="card"><h3>{t("step3.title")}</h3>
<p>{t("step3.runtime")}</p>
<p>{t("step3.changes")}</p>
<p>{t("step3.launch")}</p></div>
<p class="notice">{t("notice")}</p>

<h2>{t("updates.title")}</h2>
<p>{t("updates.check")}</p>
<p>{t("updates.release")}</p>
<p>{t("updates.blocked")}</p>
<details><summary>{t("portable.summary")}</summary><p>{t("portable.body")}</p></details>

<h2 id="controls{suffix}">{t("controls.title")}</h2>
<p>{t("controls.intro")}</p>
{controls_tables(code)}
<p>{t("controls.keyboard")}</p>
<p id="playstation{suffix}">{t("controls.playstation")}</p>
<p>{t("controls.views")}</p>
<details><summary>{t("optional.summary")}</summary>
<p>{t("optional.walk")}</p>
<p>{t("optional.share")}</p>
<p>{t("optional.diorama")}</p>
</details>

<h2 id="recovery{suffix}">{t("recovery.title")}</h2>
<ul>
<li>{t("recovery.stuck")}</li>
<li>{t("recovery.close")}</li>
<li>{t("recovery.controller")}</li>
<li>{t("recovery.hud")}</li>
<li>{t("recovery.menu")}</li>
<li>{t("recovery.report")}</li>
</ul>
<details><summary>{t("uninstall.summary")}</summary><p>{t("uninstall.body")}</p></details>

<h2>{t("recording.title")}</h2>
<p>{t("recording.body")}</p>
<p>{t("recording.limits")}</p>
<footer><a href="https://chronohaxx.github.io/wuwa-vr/">{t("footer.website")}</a> · <a href="{release}">{t("footer.release")}</a> · <a href="https://github.com/ChronoHaxx/wuwa-vr/blob/main/docs/CONTROLS.md">{t("footer.controls")}</a> · <a href="https://github.com/ChronoHaxx/wuwa-vr/issues">{t("footer.issue")}</a> · <a href="https://ko-fi.com/chronohax">{t("footer.support")}</a></footer>
</section>
'''


def build():
    english = load('en')['strings']
    values = fields()
    languages = [(code, load(code)) for code in ORDER]
    languages = [(code, data) for code, data in languages if data]
    nav = ''.join(f'<a href="#{"zh" if code == "zh-Hans" else code}" lang="{code}">{html.escape(data.get("name", code))}</a>'
                  for code, data in languages)
    body = '<hr>\n'.join(section(code, data, english, values) for code, data in languages)
    return f'''<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>WuWa VR — Player guide</title>
<!-- Generated by dev/i18n/build_guide.py from dev/i18n/guide/*.json; edit those. -->
<style>
{STYLE}
</style>
</head>
<body><main>
<nav aria-label="Guide language">{nav}</nav>
{body}</main></body></html>
'''


def check():
    english = load('en')['strings']
    problems = []
    for code in ORDER[1:]:
        problems += problems_for(code, load(code), english)
    if not OUT.exists() or OUT.read_text(encoding='utf-8') != build():
        problems.append('launcher/native/PlayerGuide.html is stale; run: python dev/i18n/build_guide.py')
    print('\n'.join(problems) if problems else f'OK: player guide, {len(ORDER)} languages, {len(english)} parts each')
    return 1 if problems else 0


if __name__ == '__main__':
    if '--check' in sys.argv:
        sys.exit(check())
    with open(OUT, 'w', encoding='utf-8', newline='\n') as out:
        out.write(build())
    print(f'{OUT.relative_to(ROOT).as_posix()}: {len([c for c in ORDER if load(c)])} languages')
