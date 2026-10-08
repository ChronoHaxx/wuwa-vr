#!/usr/bin/env python3
"""Regenerate mod/source-changes/uevr-working-tree-full.patch from mod/uevr.

    python dev/sync-native-patch.py <local UEVR clone> [--check]

mod/uevr holds the patched native files (LF line endings in this repository);
the patch is what mod/BUILD.md applies to a clean UEVR checkout, so it is the
native source of record. Run this after editing anything under mod/uevr.

The clone only has to contain the pinned commit; its own working tree is not
touched. A temporary detached worktree of the pin gets the current patch,
then mod/uevr in that checkout's own line endings, and the patch is rewritten
from `git diff --binary --full-index` against the pin. Files deleted from
mod/uevr are not removed from the patch. --check compares without writing.
"""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = '4ee5c6b6162dee2291fc75f9dfc57667f6d45a2d'  # mod/BUILD.md
PATCH = ROOT / 'mod/source-changes/uevr-working-tree-full.patch'
SOURCE = ROOT / 'mod/uevr'


def git(cwd, *args):
    return subprocess.run(['git', '-c', 'core.autocrlf=false', '-C', str(cwd), *args],
                          check=True, capture_output=True).stdout


def main():
    check = '--check' in sys.argv
    args = [a for a in sys.argv[1:] if a != '--check']
    if len(args) != 1:
        sys.exit(__doc__)
    clone = Path(args[0]).resolve()
    scratch = Path(tempfile.mkdtemp(prefix='uevr-patch-'))
    tree = scratch / 'tree'
    git(clone, 'worktree', 'add', '--detach', str(tree), BASE)
    try:
        git(tree, 'config', 'core.autocrlf', 'false')
        git(tree, 'apply', '--whitespace=nowarn', '--exclude=dependencies/submodules/UESDK', str(PATCH))
        changed = []
        lf = lambda b: b.replace(b'\r\n', b'\n')
        for source in sorted(p for p in SOURCE.rglob('*') if p.is_file()):
            target = tree / source.relative_to(SOURCE)
            data = source.read_bytes()
            if target.exists():
                old = target.read_bytes()
                # mod/uevr may differ from the patched tree in line endings
                # only; that is not a change. Real edits take the tree's style.
                if data == old or (b'\0' not in data and lf(data) == lf(old)):
                    continue
                if b'\0' not in data:
                    crlf = old.count(b'\r\n')
                    if crlf and crlf != old.count(b'\n'):
                        sys.exit(f'{source.relative_to(SOURCE)}: mixed line endings in the patched tree; edit the patch by hand')
                    data = lf(data).replace(b'\n', b'\r\n') if crlf else lf(data)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            changed.append(source.relative_to(SOURCE).as_posix())
        git(tree, 'add', '-A')
        patch = git(tree, 'diff', '--cached', '--binary', '--full-index', BASE)
    finally:
        git(clone, 'worktree', 'remove', '--force', str(tree))
        shutil.rmtree(scratch, ignore_errors=True)
    same = patch == PATCH.read_bytes()
    for name in changed:
        print('changed:', name)
    print('patch', 'up to date' if same else 'out of date' if check else 'rewritten')
    if not same:
        if check:
            sys.exit(1)
        PATCH.write_bytes(patch)


if __name__ == '__main__':
    main()
