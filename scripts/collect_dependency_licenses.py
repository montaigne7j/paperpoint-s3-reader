#!/usr/bin/env python3
"""Collect upstream license/notice files from resolved PlatformIO packages."""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LICENSE_NAMES = ("license", "copying", "notice", "copyright", "authors")


def is_license_file(path: Path) -> bool:
    name = path.name.lower()
    return any(name == base or name.startswith(base + ".") or name.startswith(base + "-") for base in LICENSE_NAMES)


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def package_root(path: Path, scan_root: Path) -> str:
    rel = path.relative_to(scan_root)
    return rel.parts[0] if rel.parts else scan_root.name


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--env", default="gh_release")
    parser.add_argument("--output", default="dist/dependency-licenses")
    args = parser.parse_args()

    output = Path(args.output)
    if not output.is_absolute():
        output = ROOT / output
    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)

    home = Path.home()
    roots = [
        ROOT / ".pio" / "libdeps" / args.env,
        home / ".platformio" / "packages",
        home / ".platformio" / "platforms",
    ]
    manifest: list[dict[str, str | int]] = []

    for scan_root in roots:
        if not scan_root.is_dir():
            continue
        for source in sorted(scan_root.rglob("*")):
            if not source.is_file() or not is_license_file(source):
                continue
            component = package_root(source, scan_root)
            relative_inside_component = source.relative_to(scan_root / component)
            category = "project-libdeps" if ".pio" in scan_root.parts else scan_root.name
            target = output / category / component / relative_inside_component
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
            manifest.append(
                {
                    "component": component,
                    "source": str(source),
                    "path": target.relative_to(output).as_posix(),
                    "sha256": sha256(target),
                    "size": target.stat().st_size,
                }
            )

    if not manifest:
        raise SystemExit(
            "No resolved dependency licence files found. Run a successful "
            f"`pio run -e {args.env}` first."
        )

    (output / "manifest.json").write_text(
        json.dumps({"format": 1, "environment": args.env, "files": manifest}, indent=2) + "\n",
        encoding="utf-8",
    )
    print(f"Collected {len(manifest)} licence/notice files into {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
