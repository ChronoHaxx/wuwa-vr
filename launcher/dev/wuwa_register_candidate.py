#!/usr/bin/env python3
"""Register a locally built UEVRBackend.dll as a candidate in the portable Launcher.exe catalog.

The portable launcher lists builds from app/dev/wuwa-builds.json; each entry names a runtime
folder under app/runtime (checked for five files and the backend SHA-256 on Select/Launch) and
a profile seed under app/builds/<id>/profile. This copies an existing catalog build's runtime
and seed, replaces only UEVRBackend.dll, and appends one `candidate` entry. Existing entries,
runtimes and seeds are never modified, so rollback is the usual one: choose another build in
Launcher.exe (Select backs up the live profile first). Recording and localization are untouched.

    python\\python.exe app\\dev\\wuwa_register_candidate.py --app app --backend <UEVRBackend.dll>
    ... --dry-run    # print what would be written, change nothing

Close Launcher.exe first. No game process, profile or injector state is touched here.
"""
import argparse
import datetime
import hashlib
import json
import os
import re
import shutil
import sys
from pathlib import Path

REQUIRED = ('UEVRBackend.dll', 'Custom_UEVR_Injector.exe', 'openxr_loader.dll', 'openvr_api.dll', 'LuaVR.dll')
DEFAULT_ID = 'eye-diff-lifecycle-20260928'
DEFAULT_BASE = 'camera-trial-controls-20260928-r2'
SUMMARY = ('Diagnostic candidate: repairs paired-eye sampling in the LOD trace. Retains the '
           'headset-confirmed ultimate camera fix and r.OneFrameThreadLag=0. Tracing starts off.')
KNOWN = ('Not a foliage, prop, LOD or reflection fix. The left-eye foliage freeze remains '
         'unresolved; weapon/lower Resonators reflections remain deferred. Use it only for the '
         'far/near diagnostic session, then return to your usual build.')
DESCRIPTION = ('Before-submission eye-diff samples are scheduled by validated pair sequence, not '
               'by the not-yet-assigned family frame; after-submission snapshots take the callback '
               'lock shared. Adds per-phase pair and sampler counters. Read-only; no rendering change.')


def sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def inside(root, path):
    root, path = Path(root).resolve(), Path(path).resolve()
    return path == root or root in path.parents


def register(app, backend, build_id=DEFAULT_ID, base_id=DEFAULT_BASE, name=None, dry_run=False, now=None):
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,79}', build_id):
        raise ValueError(f'invalid build id {build_id!r}: use letters, digits, dot, dash or underscore')
    app, backend = Path(app).resolve(), Path(backend).resolve()
    catalog_path = app / 'dev' / 'wuwa-builds.json'
    raw = catalog_path.read_bytes()
    catalog = json.loads(raw.decode('utf-8-sig'))
    builds = catalog.get('builds')
    if not isinstance(builds, list):
        raise ValueError(f'{catalog_path} has no builds list')
    if not backend.is_file():
        raise ValueError(f'backend not found: {backend}')
    digest = sha256(backend)
    existing = next((b for b in builds if b.get('id') == build_id), None)
    if existing is not None:
        if str(existing.get('sha256', '')).lower() != digest:
            raise ValueError(f'build id {build_id!r} is already registered with another backend; choose a new --id')
        # Hash the installed file, not just its presence: Launcher.exe refuses Select when the
        # runtime backend differs from the catalog, so "unchanged" must mean it will launch.
        installed = app / existing['runtime'] / 'UEVRBackend.dll'
        if not installed.is_file() or sha256(installed) != digest:
            raise ValueError(f'build id {build_id!r} is registered, but {installed} is missing or differs from '
                             'the catalog hash; restore that file or register under a new --id')
        return {'changed': False, 'id': build_id, 'sha256': digest}
    if any(str(b.get('sha256', '')).lower() == digest for b in builds):
        raise ValueError('this backend is already registered under another id')
    base = next((b for b in builds if b.get('id') == base_id), None)
    if base is None:
        raise ValueError(f'base build {base_id!r} is not in the catalog')
    base_runtime, base_seed = app / base['runtime'], app / base['seed']
    missing = [n for n in REQUIRED if not (base_runtime / n).is_file()]
    if missing or not base_seed.is_dir():
        raise ValueError(f'base build is incomplete: missing {missing or [str(base_seed)]}')
    # The launcher's own check (Assert-WuWaBuildFiles): never copy from a base runtime whose
    # backend no longer matches its catalog entry.
    if sha256(base_runtime / 'UEVRBackend.dll') != str(base.get('sha256', '')).lower():
        raise ValueError(f'base build {base_id!r}: its runtime backend differs from the catalog hash')
    runtime_rel = f'runtime/Wuthering Waves UEVR - {build_id}'
    seed_rel = f'builds/{build_id}/profile'
    runtime, seed = app / runtime_rel, app / seed_rel
    for target in (runtime, seed):
        if not inside(app, target):
            raise ValueError(f'{target} is outside {app}')
        if target.exists():
            raise ValueError(f'{target} already exists; remove it or choose a new --id')
    stamp = (now or datetime.datetime.now().astimezone()).isoformat(timespec='seconds')
    entry = {
        'id': build_id,
        'name': name or f'Paired-eye diagnostic repair · {stamp[:16].replace("T", " ")}',
        'role': 'candidate',
        'runtime': runtime_rel,
        'sha256': digest,
        'seed': seed_rel,
        'savedAt': stamp,
        'summary': SUMMARY,
        'known': KNOWN,
        'description': DESCRIPTION,
    }
    plan = {'changed': True, 'id': build_id, 'sha256': digest, 'base': base_id,
            'runtime': str(runtime), 'seed': str(seed), 'catalog': str(catalog_path), 'entry': entry}
    if dry_run:
        return plan
    try:
        shutil.copytree(base_runtime, runtime)
        shutil.copy2(backend, runtime / 'UEVRBackend.dll')
        shutil.copytree(base_seed, seed)
        if sha256(runtime / 'UEVRBackend.dll') != digest:
            raise RuntimeError('copied backend hash differs from the source')
    except BaseException:
        # Leave no half-registered folders: they would block a retry with "already exists".
        for created in (runtime, seed.parent):
            if created.exists() and inside(app, created):
                shutil.rmtree(created)
        raise
    (seed.parent / 'registration.json').write_text(json.dumps(
        {'id': build_id, 'base': base_id, 'backend_source': str(backend), 'sha256': digest,
         'registered_at': stamp, 'published': False, 'visual_fix_claim': False}, indent=2), encoding='utf-8')
    builds.append(entry)
    text = json.dumps(catalog, indent=2, ensure_ascii=False) + '\n'
    bom = raw.startswith(b'\xef\xbb\xbf')  # PowerShell 5.1 needs it to read the non-ASCII names
    temporary = catalog_path.with_suffix('.json.tmp')
    temporary.write_bytes((b'\xef\xbb\xbf' if bom else b'') + text.encode('utf-8'))
    os.replace(temporary, catalog_path)
    return plan


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--app', required=True, help="the portable package's app folder (holds dev\\wuwa-builds.json)")
    parser.add_argument('--backend', required=True, help='locally built UEVRBackend.dll')
    parser.add_argument('--id', default=DEFAULT_ID)
    parser.add_argument('--base', default=DEFAULT_BASE, help='catalog build whose runtime and profile seed are copied')
    parser.add_argument('--name', help='launcher display name')
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args(argv)
    try:
        result = register(args.app, args.backend, args.id, args.base, args.name, args.dry_run)
    except (OSError, ValueError, RuntimeError) as error:
        print(f'not registered: {error}', file=sys.stderr)
        return 1
    print(json.dumps(result, indent=2, ensure_ascii=False))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
