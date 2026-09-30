"""Verify SHA256SUMS.txt and manifest.json against the staged tree.

Both records list the SHA-256 of every tracked file, so they must be
regenerated whenever a tracked file changes. This check recomputes each hash
from the git index blob, which gives the same answer on LF and CRLF checkouts,
and fails when either record lags the tree:

- a tracked file missing from a record, or a recorded file that is not tracked
- a recorded hash that no longer matches the file
- manifest.json disagreeing with SHA256SUMS.txt

CLAUDE.md and SHA256SUMS.txt are conventionally not recorded; manifest.json
additionally omits its own entry, while SHA256SUMS.txt records manifest.json.

    python dev/check-sums.py

Run it before committing; it reads the index, so stage your changes first.
"""
from __future__ import annotations

import hashlib
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SUMS_FILE = ROOT / "SHA256SUMS.txt"
MANIFEST_FILE = ROOT / "manifest.json"
UNRECORDED = {"CLAUDE.md", "SHA256SUMS.txt"}
MANIFEST_EXTRA_UNRECORDED = {"manifest.json"}


def staged_blobs():
    """Map every tracked path to its staged blob object id."""
    result = subprocess.run(
        ["git", "ls-files", "-s", "-z"],
        cwd=ROOT,
        check=True,
        stdout=subprocess.PIPE,
    )
    blobs = {}
    for entry in result.stdout.split(b"\0"):
        if not entry:
            continue
        meta, path = entry.split(b"\t", 1)
        blobs[path.decode("utf-8")] = meta.split()[1].decode("ascii")
    return blobs


def sha256_of_blob(object_id):
    """Hash a git blob's bytes; identical on LF and CRLF checkouts."""
    result = subprocess.run(
        ["git", "cat-file", "blob", object_id],
        cwd=ROOT,
        check=True,
        stdout=subprocess.PIPE,
    )
    return hashlib.sha256(result.stdout).hexdigest()


def read_sums():
    entries = {}
    for line in SUMS_FILE.read_text(encoding="utf-8-sig").splitlines():
        line = line.strip("\r")
        if not line:
            continue
        digest, path = line.split("  ", 1)
        entries[path] = digest
    return entries


def main():
    blobs = staged_blobs()
    sums = read_sums()
    manifest = json.loads(MANIFEST_FILE.read_text(encoding="utf-8-sig"))["files"]

    expected_sums = set(blobs) - UNRECORDED
    expected_manifest = expected_sums - MANIFEST_EXTRA_UNRECORDED
    problems = []
    for path in sorted(set(sums) - expected_sums):
        problems.append("SHA256SUMS.txt records a file that is not tracked: " + path)
    for path in sorted(expected_sums - set(sums)):
        problems.append("SHA256SUMS.txt is missing tracked file: " + path)
    for path in sorted(set(manifest) - expected_manifest):
        problems.append("manifest.json records a file that is not tracked: " + path)
    for path in sorted(expected_manifest - set(manifest)):
        problems.append("manifest.json is missing tracked file: " + path)

    digest_of_blob = {}
    for path in sorted(set(sums) & expected_sums):
        object_id = blobs[path]
        if object_id not in digest_of_blob:
            digest_of_blob[object_id] = sha256_of_blob(object_id)
        if digest_of_blob[object_id] != sums[path]:
            problems.append(
                "SHA256SUMS.txt hash mismatch for "
                + path
                + ": recorded "
                + sums[path]
                + ", staged "
                + digest_of_blob[object_id]
            )
    for path in sorted(set(manifest) & expected_manifest):
        if path in sums and manifest[path] != sums[path]:
            problems.append("manifest.json disagrees with SHA256SUMS.txt for: " + path)

    if problems:
        print("\n".join(problems))
        print(
            str(len(problems))
            + " problem(s); regenerate SHA256SUMS.txt and manifest.json before committing"
        )
        return 1
    print(
        "OK: "
        + str(len(sums))
        + " SHA256SUMS.txt entries and "
        + str(len(manifest))
        + " manifest.json entries match the staged tree"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
