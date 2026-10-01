"""Add translations to a language's memory: python dev/i18n/merge.py <lang> <file.json>.

<file.json> maps unit keys to translated HTML, {"<key>": "<translation>", ...}. Each key must be
one of the language's current units (from translate-site.py --missing); its English text is stored
alongside, so a later English change is detected.
"""
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def main(lang: str, source: str, english: str | None = None) -> int:
    memory_path = HERE / f"{lang}.json"
    memory = json.loads(memory_path.read_text(encoding="utf-8")) if memory_path.exists() else {}
    units = json.loads(Path(english).read_text(encoding="utf-8")) if english else {}
    added = 0
    for key, text in json.loads(Path(source).read_text(encoding="utf-8")).items():
        en = units.get(key) or (memory.get(key) or {}).get("en")
        if not en:
            raise SystemExit(f"unknown unit {key}: pass the --missing file as the third argument")
        memory[key] = {"en": en, "t": text}
        added += 1
    memory_path.write_text(json.dumps(memory, ensure_ascii=False, indent=1, sort_keys=True) + "\n", encoding="utf-8")
    print(f"{lang}: {added} translations merged, {len(memory)} in memory")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(*sys.argv[1:4]))
