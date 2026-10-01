"""Build the translated website (site/l/<lang>/*.html) from the English pages.

Each English page is split into translatable units: paragraphs, list items, table cells,
headings, captions, labels, buttons and options (inner HTML, so inline tags stay with their
words), plus the page title, meta description, image alt text, ARIA labels and the feedback
form's messages. Each unit is looked up in the language's translation memory
dev/i18n/<lang>.json, keyed by a hash of the English text:

    {"<key>": {"en": "<English HTML>", "t": "<translated HTML>"}}

A unit without a translation stays in English, so pages always build. Links are rewritten so
pages stay in the same language, assets resolve from the site root, and the language picker
switches to the same page in another language. The old one-page starters (site/l/<lang>.html)
become redirects.

    python dev/translate-site.py                 # build all languages, report coverage
    python dev/translate-site.py --missing DIR   # also write untranslated units per language
    python dev/translate-site.py --prune         # drop memory entries no page uses any more
"""
from __future__ import annotations

import argparse
import hashlib
import html
import json
import re
from pathlib import Path

import warnings

from bs4 import BeautifulSoup, MarkupResemblesLocatorWarning, NavigableString, Tag

warnings.filterwarnings("ignore", category=MarkupResemblesLocatorWarning)

ROOT = Path(__file__).resolve().parent.parent
SITE = ROOT / "site"
MEMORY = ROOT / "dev" / "i18n"
PAGES = ["index.html", "guide.html", "understanding.html", "developers.html", "credits.html", "support.html",
         "risk.html", "license.html", "feedback.html", "languages.html", "testing.html"]
LANGUAGES = {"zh-Hans": "ltr", "ja": "ltr", "ko": "ltr", "es": "ltr", "pt-BR": "ltr", "fr": "ltr", "de": "ltr",
             "ru": "ltr", "ar": "rtl"}
BLOCKS = {"p", "li", "td", "th", "h1", "h2", "h3", "h4", "h5", "h6", "figcaption", "summary", "label", "option",
          "button", "dt", "dd", "caption", "legend", "aside", "blockquote"}
CONTAINERS = {"section", "div", "article", "main", "figure", "details", "nav", "header", "footer", "body", "form"}
LOOSE = {"a", "span", "strong", "em"}
ATTRIBUTES = ["alt", "aria-label", "placeholder", "title"]
FORM_MESSAGES = ["data-pending", "data-ready", "data-long", "data-copied", "data-fallback", "data-empty"]
SKIP = {"script", "style", "pre", "code", "kbd", "textarea"}
LANGUAGE_NAMES = {"en", "zh-Hans", "ja", "ko", "es", "pt-BR", "fr", "de", "ru", "ar"}
NOTE = ("This page is translated from the English original, which is authoritative where they differ. "
        "Parts not yet translated are shown in English.")


def key(text: str) -> str:
    return hashlib.sha1(text.encode("utf-8")).hexdigest()[:16]


def normal(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip()


def worth_translating(text: str) -> bool:
    plain = normal(BeautifulSoup(text, "html.parser").get_text())
    return bool(re.search(r"[A-Za-z]{2,}", plain))


def in_picker(tag: Tag) -> bool:
    return any(p.name == "details" and "language-picker" in (p.get("class") or []) for p in tag.parents)


def units(soup: BeautifulSoup):
    """Yield (kind, element, attribute, english) for every translatable piece of a page."""
    if soup.title and soup.title.string:
        yield "title", soup.title, None, normal(soup.title.string)
    description = soup.find("meta", attrs={"name": "description"})
    if description:
        yield "attr", description, "content", normal(description["content"])
    for element in soup.find_all(True):
        if element.name in SKIP or any(p.name in SKIP for p in element.parents) or in_picker(element):
            continue
        if element.get("translate") == "no" or "wordmark" in (element.get("class") or []):
            continue
        for attribute in ATTRIBUTES + (FORM_MESSAGES if element.name == "form" else []):
            if element.get(attribute) and worth_translating(element[attribute]):
                yield "attr", element, attribute, normal(element[attribute])
        leaf = element.name in BLOCKS and not element.find(BLOCKS)
        # Inline elements sitting directly in a container (nav links, a lone "see more" link).
        loose = element.name in LOOSE and element.parent is not None and element.parent.name in CONTAINERS \
            and not element.find_parent(BLOCKS)
        if leaf or loose:
            inner = normal(element.decode_contents())
            if inner and worth_translating(inner):
                yield "html", element, None, inner


def rewrite_url(url: str, element: Tag) -> str:
    """Make an English page's relative URL work from site/l/<lang>/."""
    if not url or re.match(r"^(?:[a-z]+:|//|#)", url):
        return url
    match = re.match(r"^l/([\w-]+)/(.*)$", url)
    if match:
        return f"../{match.group(1)}/{match.group(2)}"
    page = re.split(r"[?#]", url)[0]
    if page in PAGES and element.get("hreflang") != "en":
        return url
    return "../../" + url


def build(lang: str, memory: dict, used: set, missing: dict) -> tuple[int, int]:
    total = done = 0
    target = SITE / "l" / lang
    target.mkdir(parents=True, exist_ok=True)
    for page in PAGES:
        soup = BeautifulSoup((SITE / page).read_text(encoding="utf-8"), "html.parser")
        page_missing = 0
        for kind, element, attribute, english in list(units(soup)):
            k = key(english)
            used.add(k)
            total += 1
            entry = memory.get(k)
            translated = entry.get("t") if entry and entry.get("en") == english else None
            if not translated:
                page_missing += 1
                missing.setdefault(k, english)
                continue
            done += 1
            if kind == "title":
                element.string = translated
            elif kind == "attr":
                element[attribute] = html.unescape(BeautifulSoup(translated, "html.parser").get_text())
            else:
                element.clear()
                for child in list(BeautifulSoup(translated, "html.parser").contents):
                    element.append(child)
        soup.html["lang"] = lang
        soup.html["dir"] = LANGUAGES[lang]
        for element in soup.find_all(["a", "link", "img", "script", "source", "track", "video"]):
            for attribute in ("href", "src", "poster"):
                if element.get(attribute) and not (element.name == "link" and element.get("rel") == ["alternate"]):
                    element[attribute] = rewrite_url(element[attribute], element)
        # The picker and language cards: English is the site root, other languages are siblings.
        for element in soup.select("[data-language]"):
            code = element["data-language"]
            current = element["href"].split("/")[-1]
            element["href"] = f"../../{current}" if code == "en" else f"../{code}/{current}"
        main = soup.find("main")
        if main is not None:
            note = soup.new_tag("p", attrs={"class": "small translation-note"})
            note_text = memory.get(key(NOTE), {}).get("t") or NOTE
            note.append(NavigableString(note_text + " "))
            original = soup.new_tag("a", href=f"../../{page}", hreflang="en", lang="en")
            original.string = "English"
            note.append(original)
            main.insert(0, note)
        form = soup.find("form", id="report-form")
        if form is not None:
            form["data-lang"] = lang
        (target / page).write_text(str(soup), encoding="utf-8", newline="\n")
        if page_missing:
            print(f"  {lang}/{page}: {page_missing} unit(s) in English")
    # The old one-page starters forward to the translated home page.
    (SITE / "l" / f"{lang}.html").write_text(
        f'<!doctype html><html lang="{lang}" dir="{LANGUAGES[lang]}"><head><meta charset="utf-8">'
        f'<meta http-equiv="refresh" content="0; url={lang}/index.html"><link rel="canonical" href="{lang}/index.html">'
        f'<title>WuWa VR</title></head><body><p><a href="{lang}/index.html">WuWa VR</a></p></body></html>\n',
        encoding="utf-8", newline="\n")
    return total, done


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--missing", type=Path, help="write <dir>/<lang>.json with untranslated units")
    ap.add_argument("--prune", action="store_true", help="remove memory entries no page uses")
    ap.add_argument("--only", nargs="*", help="languages to build (default: all)")
    args = ap.parse_args()
    MEMORY.mkdir(parents=True, exist_ok=True)
    note_key = key(NOTE)
    for lang in args.only or LANGUAGES:
        path = MEMORY / f"{lang}.json"
        memory = json.loads(path.read_text(encoding="utf-8")) if path.exists() else {}
        used, missing = {note_key}, {}
        if note_key not in memory or memory[note_key].get("en") != NOTE:
            missing[note_key] = NOTE
        total, done = build(lang, memory, used, missing)
        if args.prune:
            memory = {k: v for k, v in memory.items() if k in used}
            path.write_text(json.dumps(memory, ensure_ascii=False, indent=1, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
        if args.missing:
            args.missing.mkdir(parents=True, exist_ok=True)
            (args.missing / f"{lang}.json").write_text(json.dumps(missing, ensure_ascii=False, indent=1) + "\n", encoding="utf-8", newline="\n")
        print(f"{lang}: {done}/{total} units translated")
    write_sitemap()
    return 0


def write_sitemap() -> None:
    """Every content page, English and translated; redirects and ownership files excluded."""
    base = "https://chronohaxx.github.io/wuwa-vr/"
    urls = [base + page for page in PAGES if page != "testing.html"]
    urls += [f"{base}l/{lang}/{page}" for lang in LANGUAGES for page in PAGES if page != "testing.html"]
    lines = ['<?xml version="1.0" encoding="UTF-8"?>', '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">']
    lines += [f"  <url><loc>{u}</loc></url>" for u in urls] + ["</urlset>"]
    (SITE / "sitemap.xml").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


if __name__ == "__main__":
    raise SystemExit(main())
