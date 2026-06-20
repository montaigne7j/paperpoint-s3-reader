#!/usr/bin/env python3
"""Create a browser-flashable ESP32-S3 merged firmware image."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Merge PlatformIO ESP32-S3 build outputs into one binary."
    )
    parser.add_argument("--env", default="default", help="PlatformIO build env name")
    parser.add_argument(
        "--output",
        default=None,
        help="Output merged firmware path. Defaults to .pio/build/<env>/merged-firmware.bin",
    )
    parser.add_argument("--chip", default="esp32s3")
    parser.add_argument("--flash-mode", default="dio")
    parser.add_argument("--flash-freq", default="80m")
    parser.add_argument("--flash-size", default="16MB")
    parser.add_argument("--app-offset", default="0x10000")
    return parser.parse_args()


def resolve_part(build_dir: Path, part_path: str) -> Path:
    path = Path(part_path)
    if not path.is_absolute():
        path = build_dir / path
    path = path.resolve()
    if not path.is_file():
        raise FileNotFoundError(f"Missing firmware part: {path}")
    return path


def find_boot_app(project_dir: Path) -> Path:
    candidates = [
        project_dir
        / ".pio"
        / "packages"
        / "framework-arduinoespressif32"
        / "tools"
        / "partitions"
        / "boot_app0.bin",
        Path.home()
        / ".platformio"
        / "packages"
        / "framework-arduinoespressif32"
        / "tools"
        / "partitions"
        / "boot_app0.bin",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()

    search_roots = [
        project_dir / ".pio" / "packages",
        Path.home() / ".platformio" / "packages",
    ]
    for root in search_roots:
        if root.is_dir():
            matches = list(root.glob("**/tools/partitions/boot_app0.bin"))
            if matches:
                return matches[0].resolve()

    raise FileNotFoundError("Missing boot_app0.bin in PlatformIO package directories")


def collect_parts(project_dir: Path, build_dir: Path, app_offset: str) -> list[tuple[int, Path]]:
    idedata_path = build_dir / "idedata.json"
    firmware_path = build_dir / "firmware.bin"
    if not firmware_path.is_file():
        raise FileNotFoundError(f"Missing application firmware: {firmware_path}")

    if not idedata_path.is_file():
        fallback_parts = [
            (0x0000, build_dir / "bootloader.bin"),
            (0x8000, build_dir / "partitions.bin"),
            (0xE000, find_boot_app(project_dir)),
            (int(app_offset, 0), firmware_path.resolve()),
        ]
        missing = [str(path) for _, path in fallback_parts if not path.is_file()]
        if missing:
            raise FileNotFoundError(
                "Missing firmware part(s) without idedata fallback: "
                + ", ".join(missing)
            )
        print(f"PlatformIO metadata not found at {idedata_path}; using standard offsets.")
        return [(offset, path.resolve()) for offset, path in fallback_parts]

    idedata = json.loads(idedata_path.read_text(encoding="utf-8"))
    flash_images = idedata.get("extra", {}).get("flash_images", [])
    if not flash_images:
        raise ValueError(f"No flash_images found in {idedata_path}")

    parts: list[tuple[int, Path]] = []
    for image in flash_images:
        offset = int(str(image["offset"]), 0)
        parts.append((offset, resolve_part(build_dir, image["path"])))

    parts.append((int(app_offset, 0), firmware_path.resolve()))
    return sorted(parts, key=lambda item: item[0])


def main() -> int:
    args = parse_args()
    project_dir = Path.cwd()
    build_dir = project_dir / ".pio" / "build" / args.env
    output = Path(args.output) if args.output else build_dir / "merged-firmware.bin"
    output.parent.mkdir(parents=True, exist_ok=True)

    parts = collect_parts(project_dir, build_dir, args.app_offset)
    command = [
        sys.executable,
        "-m",
        "esptool",
        "--chip",
        args.chip,
        "merge-bin",
        "-o",
        str(output),
        "--flash-mode",
        args.flash_mode,
        "--flash-freq",
        args.flash_freq,
        "--flash-size",
        args.flash_size,
    ]
    for offset, path in parts:
        command.extend([hex(offset), str(path)])

    print("Merging firmware parts:")
    for offset, path in parts:
        print(f"  {hex(offset)} {path}")
    subprocess.run(command, check=True)
    print(f"Merged firmware written to {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
