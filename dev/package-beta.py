"""Make a one-build beta of the portable WuWa VR Launcher from the local test package.

Copies the test package without its other runtimes, profile seeds, diagnostic source and
runtime-created files, lists only the chosen build in the launcher (as the default), writes
the player notes, and regenerates manifest.json + SHA256SUMS.txt so Verify-Package.ps1 passes.
Then zips the folder. Read-only on the source package; refuses to overwrite an output.

    python dev/package-beta.py --source "<test package>" --build lodfix-20261001 --out <dir> \
        --tag beta-2026-10-01 --notes notes.txt
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import zipfile
from datetime import datetime
from pathlib import Path

SKIP_TOP = {"diagnostic-source", "READ-ME-FIRST.txt", "PLAYTEST.txt", "manifest.json", "SHA256SUMS.txt"}
RUNTIME_CREATED = {"Custom_UEVR_Injector.txt", "registration.json", "wuwa-builds.archived.json"}


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def read_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write_json(path: Path, value) -> None:
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--source", type=Path, required=True, help="the local test package folder")
    ap.add_argument("--build", required=True, help="catalog id of the one build to ship")
    ap.add_argument("--out", type=Path, required=True, help="new output folder")
    ap.add_argument("--tag", required=True, help="release tag, also the package id")
    ap.add_argument("--notes", type=Path, required=True, help="player notes, written as PLAYTEST.txt")
    ap.add_argument("--folder-name", default="WuWa VR Launcher")
    args = ap.parse_args()

    src = args.source.resolve()
    out = args.out.resolve()
    if out.exists():
        raise SystemExit(f"refusing to overwrite {out}")
    catalog = read_json(src / "app/dev/wuwa-builds.json")
    build = next((b for b in catalog["builds"] if b["id"] == args.build), None)
    if build is None:
        raise SystemExit(f"{args.build} is not in the source catalog")
    runtime = src / "app" / build["runtime"]
    if sha256(runtime / "UEVRBackend.dll") != build["sha256"].lower():
        raise SystemExit("the build's backend does not match its catalog hash")

    pkg = out / args.folder_name
    keep_runtime = Path(build["runtime"]).name
    keep_seed = Path(build["seed"]).parts[1]

    def ignore(folder: str, names: list[str]) -> set[str]:
        rel = Path(folder).resolve().relative_to(src)
        skipped = {n for n in names if n == "__pycache__" or n in RUNTIME_CREATED}
        if rel == Path("."):
            skipped |= {n for n in names if n in SKIP_TOP}
        elif rel == Path("app/runtime"):
            skipped |= {n for n in names if n != keep_runtime}
        elif rel == Path("app/builds"):
            skipped |= {n for n in names if n != keep_seed}
        return skipped

    shutil.copytree(src, pkg, ignore=ignore)

    shipped = dict(build, role="baseline")
    write_json(pkg / "app/dev/wuwa-builds.json", {"schema": catalog.get("schema", 1), "builds": [shipped]})
    created = datetime.now().astimezone().isoformat(timespec="seconds")
    portable = read_json(pkg / "app/portable.json")
    portable.update(packageId=f"wuwa-vr-launcher-{args.tag}", createdAt=created, defaultBuild=build["id"])
    write_json(pkg / "app/portable.json", portable)
    shutil.copyfile(args.notes, pkg / "PLAYTEST.txt")

    manifest = read_json(src / "manifest.json")
    manifest.update(packageId=f"wuwa-vr-launcher-{args.tag}", createdAt=created, defaultBuild=build["id"],
                    builds=[{"id": build["id"], "name": build["name"], "sha256": build["sha256"],
                             "savedAt": build["savedAt"], "role": "baseline"}],
                    localChanges=[f"One-build beta of {build['id']}, made by dev/package-beta.py."])
    files = sorted(p for p in pkg.rglob("*") if p.is_file())
    manifest["files"] = {p.relative_to(pkg).as_posix(): sha256(p) for p in files}
    write_json(pkg / "manifest.json", manifest)
    sums = [f"{digest}  {name}" for name, digest in manifest["files"].items()]
    (pkg / "SHA256SUMS.txt").write_text("\n".join(sums) + "\n", encoding="utf-8")

    archive = out / "WuWa-VR-Launcher.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for p in sorted(pkg.rglob("*")):
            if p.is_file():
                z.write(p, p.relative_to(out).as_posix())
    print(json.dumps({"package": str(pkg), "zip": str(archive), "zip_bytes": archive.stat().st_size,
                      "zip_sha256": sha256(archive), "files": len(manifest["files"]), "build": build["id"]}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
