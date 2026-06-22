#!/usr/bin/env python3
"""Create the relinkable-material archive used by binary firmware releases.

The archive includes the non-LGPL application object files, static libraries,
link map/ELF when available, exact build configuration, licenses and a manifest.
It is not a legal opinion; maintainers must verify each release against
BINARY_RELEASE_LGPL_COMPLIANCE.md.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import shutil
import subprocess
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def git_commit() -> str:
    try:
        return subprocess.check_output(
            ["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True, stderr=subprocess.DEVNULL
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return "NOASSERTION"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--env", default="gh_release")
    parser.add_argument("--output", default="dist/lgpl-relink-kit.zip")
    args = parser.parse_args()

    build_dir = ROOT / ".pio" / "build" / args.env
    if not build_dir.is_dir():
        raise SystemExit(
            f"Build directory does not exist: {build_dir}\n"
            f"Run `pio run -e {args.env}` before creating the relink kit."
        )

    candidates: list[Path] = []
    for path in build_dir.rglob("*"):
        if path.is_file() and (
            path.suffix in {".o", ".a", ".elf", ".map", ".ld"}
            or path.name in {"firmware.bin", "partitions.bin", "bootloader.bin"}
        ):
            candidates.append(path)
    if not any(path.suffix == ".o" for path in candidates):
        raise SystemExit("No object files were found; refusing to create an incomplete relink kit.")

    static_items = [
        ROOT / "platformio.ini",
        ROOT / "partitions.csv",
        ROOT / "BINARY_RELEASE_LGPL_COMPLIANCE.md",
        ROOT / "THIRD_PARTY_NOTICES.md",
        ROOT / "SBOM.spdx.json",
        ROOT / "LICENSE",
    ]
    static_items.extend(path for path in (ROOT / "LICENSES").rglob("*") if path.is_file())
    dependency_license_dir = ROOT / "dist" / "dependency-licenses"
    if dependency_license_dir.is_dir():
        static_items.extend(path for path in dependency_license_dir.rglob("*") if path.is_file())

    output = Path(args.output)
    if not output.is_absolute():
        output = ROOT / output
    output.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="paperpoint-relink-") as tmp:
        stage = Path(tmp) / "lgpl-relink-kit"
        stage.mkdir(parents=True)
        manifest: list[dict[str, object]] = []

        for source in sorted(candidates):
            rel = Path("build") / source.relative_to(build_dir)
            target = stage / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
            manifest.append({"path": rel.as_posix(), "sha256": sha256(target), "size": target.stat().st_size})

        for source in static_items:
            if not source.is_file():
                continue
            rel = Path("release-source-info") / source.relative_to(ROOT)
            target = stage / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
            manifest.append({"path": rel.as_posix(), "sha256": sha256(target), "size": target.stat().st_size})

        readme = stage / "README.txt"
        readme.write_text(
            f"""PaperPoint S3 Reader LGPL relink kit

Environment: {args.env}
Source commit: {git_commit()}
Generated: {dt.datetime.now(dt.timezone.utc).isoformat()}

Purpose
-------
This archive accompanies the firmware binary and preserves the object files,
static libraries, ELF/map output, exact PlatformIO configuration and license
materials needed to replace or modify LGPL-covered libraries and relink a
firmware image.

Rebuild/relink
--------------
1. Obtain the matching Complete Corresponding Source archive from the same
   release.
2. Install the PlatformIO version and packages recorded by the release SBOM.
3. Replace or rebuild the desired LGPL library while preserving the ABI.
4. Use the matching source tree and `pio run -e {args.env}` to rebuild.
5. See BINARY_RELEASE_LGPL_COMPLIANCE.md for the release obligations and
   support/source-offer process.

This archive does not grant permission to bypass device security and is not a
legal opinion. Report incomplete material through the project's issue tracker.
""",
            encoding="utf-8",
        )
        manifest.append({"path": "README.txt", "sha256": sha256(readme), "size": readme.stat().st_size})
        (stage / "manifest.json").write_text(
            json.dumps(
                {
                    "format": 1,
                    "environment": args.env,
                    "sourceCommit": git_commit(),
                    "files": manifest,
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )

        with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for path in sorted(stage.rglob("*")):
                if path.is_file():
                    archive.write(path, path.relative_to(stage.parent))

    print(f"Wrote {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
