#!/usr/bin/env python3
"""Package exact resolved source trees for LGPL-covered firmware components.

The release workflow uses the PlatformIO packages that actually produced the
firmware. The archive complements, rather than replaces, the application source
archive and relink kit.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SKIP_PARTS = {".git", "__pycache__", ".pytest_cache"}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def find_websockets(env: str) -> Path:
    libdeps = ROOT / ".pio" / "libdeps" / env
    if not libdeps.is_dir():
        raise SystemExit(
            f"Resolved library directory does not exist: {libdeps}\n"
            f"Run `pio run -e {env}` first."
        )
    for child in sorted(libdeps.iterdir()):
        if child.is_dir() and child.name.lower() == "websockets":
            return child
    raise SystemExit(f"Unable to find the resolved WebSockets source under {libdeps}")


def platformio_home(value: str | None) -> Path:
    if value:
        return Path(value).expanduser().resolve()
    configured = os.environ.get("PLATFORMIO_CORE_DIR")
    if configured:
        return Path(configured).expanduser().resolve()
    return Path.home() / ".platformio"


def iter_files(root: Path):
    for path in sorted(root.rglob("*")):
        if not path.is_file() or any(part in SKIP_PARTS for part in path.parts):
            continue
        yield path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--env", default="gh_release")
    parser.add_argument("--output", default="dist/lgpl-component-sources.zip")
    parser.add_argument(
        "--platformio-home",
        help="PlatformIO core directory; defaults to PLATFORMIO_CORE_DIR or ~/.platformio",
    )
    args = parser.parse_args()

    pio_home = platformio_home(args.platformio_home)
    components = [
        ("ArduinoWebSockets", find_websockets(args.env)),
        (
            "Arduino-ESP32",
            pio_home / "packages" / "framework-arduinoespressif32",
        ),
    ]
    for name, source in components:
        if not source.is_dir():
            raise SystemExit(
                f"Resolved source tree for {name} does not exist: {source}\n"
                f"Run `pio run -e {args.env}` with the pinned platform first."
            )

    output = Path(args.output)
    if not output.is_absolute():
        output = ROOT / output
    output.parent.mkdir(parents=True, exist_ok=True)

    generated = dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat()
    manifest: dict[str, object] = {
        "format": 1,
        "environment": args.env,
        "generated": generated,
        "components": [],
    }

    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, source in components:
            entries: list[dict[str, object]] = []
            for path in iter_files(source):
                rel = path.relative_to(source)
                archive_name = Path("lgpl-component-sources") / name / rel
                archive.write(path, archive_name.as_posix())
                entries.append(
                    {
                        "path": rel.as_posix(),
                        "sha256": sha256(path),
                        "size": path.stat().st_size,
                    }
                )
            if not entries:
                raise SystemExit(f"No source files found for {name}: {source}")
            manifest["components"].append(
                {
                    "name": name,
                    "resolvedPath": str(source),
                    "fileCount": len(entries),
                    "files": entries,
                }
            )

        readme = f"""PaperPoint S3 Reader LGPL component source archive

Environment: {args.env}
Generated: {generated}

This archive contains the exact resolved ArduinoWebSockets and Arduino-ESP32
source package trees used by the matching firmware build. It must be distributed
with the matching application source archive, firmware, SPDX SBOM, licence
bundle, and LGPL relink kit.

See BINARY_RELEASE_LGPL_COMPLIANCE.md in the application source archive. This
archive is an engineering compliance artifact and not a legal opinion.
"""
        archive.writestr("lgpl-component-sources/README.txt", readme)
        archive.writestr(
            "lgpl-component-sources/manifest.json",
            json.dumps(manifest, indent=2) + "\n",
        )

    print(f"Wrote {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
