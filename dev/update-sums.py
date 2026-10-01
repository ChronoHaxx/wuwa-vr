"""Regenerate SHA256SUMS.txt and manifest.json from the staged tree.

The counterpart of check-sums.py: hashes every staged blob (identical on LF and CRLF
checkouts), writes both records with the same exclusions, and stages them. Stage your
changes first, then run this, then check-sums.py.

    python dev/update-sums.py
"""
from __future__ import annotations

import datetime
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SUMS_FILE = ROOT / "SHA256SUMS.txt"
MANIFEST_FILE = ROOT / "manifest.json"
UNRECORDED = {"CLAUDE.md", "SHA256SUMS.txt"}
MANIFEST_EXTRA_UNRECORDED = {"manifest.json"}


def git(*args: str) -> bytes:
    return subprocess.run(["git", *args], cwd=ROOT, check=True, stdout=subprocess.PIPE).stdout


def staged_digests() -> dict[str, str]:
    digests, cache = {}, {}
    for entry in git("ls-files", "-s", "-z").split(b"\0"):
        if not entry:
            continue
        meta, path = entry.split(b"\t", 1)
        blob = meta.split()[1].decode("ascii")
        if blob not in cache:
            cache[blob] = hashlib.sha256(git("cat-file", "blob", blob)).hexdigest()
        digests[path.decode("utf-8")] = cache[blob]
    return digests


def main() -> int:
    digests = staged_digests()
    manifest = json.loads(MANIFEST_FILE.read_text(encoding="utf-8-sig"))
    recorded = sorted(set(digests) - UNRECORDED - MANIFEST_EXTRA_UNRECORDED)
    manifest["files"] = {path: digests[path] for path in recorded}
    manifest["updatedAt"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    MANIFEST_FILE.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    git("add", "manifest.json")
    digests = staged_digests()   # manifest.json changed; SHA256SUMS.txt records it
    lines = [f"{digests[path]}  {path}" for path in sorted(set(digests) - UNRECORDED)]
    SUMS_FILE.write_text("\n".join(lines) + "\n", encoding="utf-8")
    git("add", "SHA256SUMS.txt")
    print(f"SHA256SUMS.txt: {len(lines)} entries; manifest.json: {len(manifest['files'])} entries")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
