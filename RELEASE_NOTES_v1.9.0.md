# PaperPoint S3 Reader v1.9.0 Release Notes

Release date: 2026-07-17
Target: M5Stack Paper S3 / M5Paper S3

## Highlights

- Added guided BMI270 calibration and reader orientation modes, including fixed 0-degree, fixed 180-degree, and automatic orientation.
- Added CBZ/ZIP comic browsing and reading with natural page sorting, PSRAM preload, pending page turns, and configurable full-refresh intervals.
- Added hierarchical custom sleep-image selection from `/.sleep`, `/cover`, and `/sleep`, with fixed/random modes and preview support.
- Added reader background PNG selection, fade control, guide-line improvements, paragraph indentation, and inverted background rendering.
- Improved Paper S3 battery reporting across USB plug/unplug transitions and disabled unstable idle CPU down-clocking.
- Improved EPUB/TXT rendering, font discovery, page caching, navigation, and watchdog-friendly cooperative processing.
- Added File Browser parent navigation and an invisible reader power-off hotspot at the logical top-left corner.

## Installation

For most users, flash `merged-firmware.bin` at offset `0x0` with the web installer or `esptool`:

```sh
python -m esptool --chip esp32s3 --port COM5 --baud 921600 write_flash -z 0x0 merged-firmware.bin
```

Replace `COM5` with the device port on your computer.

## Release assets

- `merged-firmware.bin`: complete image containing bootloader, partition table, boot app, and firmware.
- `firmware.bin`: application image for advanced/manual flashing.
- `paperpoint-s3-reader-v1.9.0-source.zip`: complete application source archive.
- `lgpl-component-sources.zip`: resolved LGPL component sources.
- `lgpl-relink-kit.zip`: LGPL relinking materials.
- `SBOM.spdx.json`: SPDX software bill of materials.
- `SHA256SUMS.txt`: SHA-256 checksums for release artifacts.

## Validation

- `pio run -e gh_release`
- `python scripts/check_license_compliance.py`
- Firmware merge completed successfully for ESP32-S3 at offset `0x0`.

## Known limitations

- Wi-Fi features such as OTA and KOReader Sync depend on network and service availability.
- Very large or unusually structured EPUB/CBZ files may require additional processing time.
