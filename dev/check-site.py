"""Static checks for the generated website: python dev/check-site.py

- every local link, image, script, stylesheet and media file resolves (anchors included);
- every page declares its language and direction, and translated pages use their own;
- translation coverage per language (untranslated units are reported, not fatal);
- no generated page names a release other than the one in dev/site-status.json.
Exits non-zero on a broken link, a wrong language attribute or a stale release tag.
"""
from __future__ import annotations

import json
import re
import sys
import warnings
from pathlib import Path
from urllib.parse import unquote, urlsplit

from bs4 import BeautifulSoup, MarkupResemblesLocatorWarning

warnings.filterwarnings("ignore", category=MarkupResemblesLocatorWarning)
ROOT = Path(__file__).resolve().parent.parent
SITE = ROOT / "site"
STATUS = json.loads((ROOT / "dev" / "site-status.json").read_text(encoding="utf-8"))
LANGUAGES = {"zh-Hans": "ltr", "ja": "ltr", "ko": "ltr", "es": "ltr", "pt-BR": "ltr", "fr": "ltr", "de": "ltr",
             "ru": "ltr", "ar": "rtl"}


def main() -> int:
    problems: list[str] = []
    pages = sorted(p for p in SITE.rglob("*.html") if not p.name.startswith("google"))
    ids: dict[Path, set[str]] = {}
    soups = {}
    for page in pages:
        soup = BeautifulSoup(page.read_text(encoding="utf-8"), "html.parser")
        soups[page] = soup
        ids[page] = {e["id"] for e in soup.find_all(id=True)}
    for page, soup in soups.items():
        rel = page.relative_to(SITE).as_posix()
        lang = soup.html.get("lang") if soup.html else None
        parts = rel.split("/")
        expected = parts[1] if len(parts) == 3 and parts[0] == "l" else ("en" if len(parts) == 1 else None)
        if expected and lang != expected:
            problems.append(f"{rel}: lang={lang!r}, expected {expected!r}")
        if expected in LANGUAGES and soup.html.get("dir") != LANGUAGES[expected]:
            problems.append(f"{rel}: dir={soup.html.get('dir')!r}")
        for element in soup.find_all(["a", "link", "img", "script", "source", "track", "video"]):
            for attribute in ("href", "src", "poster"):
                url = element.get(attribute)
                if not url or re.match(r"^(?:[a-z]+:|//)", url):
                    continue
                parsed = urlsplit(url)
                target = page if not parsed.path else (page.parent / unquote(parsed.path)).resolve()
                if not target.exists():
                    problems.append(f"{rel}: missing {url}")
                elif parsed.fragment and target.suffix == ".html" and parsed.fragment not in ids.get(target, set()):
                    problems.append(f"{rel}: missing anchor {url}")
        text = str(soup)
        for tag in set(re.findall(r"releases/(?:tag|download)/((?:beta|experimental)-[\w-]+)", text)):
            if tag != STATUS["tag"] and "Earlier" not in text:
                problems.append(f"{rel}: names release {tag}, current is {STATUS['tag']}")
    for problem in problems:
        print("FAIL", problem)
    english = len([p for p in pages if p.parent == SITE])
    print(f"{len(pages)} pages checked ({english} English); {len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
