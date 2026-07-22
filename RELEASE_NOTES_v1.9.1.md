# PaperPoint S3 Reader v1.9.1 Release Notes

Release date: 2026-07-22
Target: M5Stack Paper S3 / M5Paper S3

## Fixes

- Fixed upside-down reading lock polarity when Reader Direction is set to Fixed 180°.
- Preserved the confirmed BMI270 lock state between sensor data-ready polls.
- Added a 450 ms stable-pose debounce for lock and unlock transitions.
- Made toggle settings switch and redraw immediately when tapped.
- Corrected File Browser row-icon orientation.
- Corrected Novel Reader, Comic Reader, and Recent Books home icon orientation.

## Installation

For most users, flash `merged-firmware.bin` at offset `0x0` with the web installer or `esptool`:

```sh
python -m esptool --chip esp32s3 --port COM5 --baud 921600 write_flash -z 0x0 merged-firmware.bin
```

Replace `COM5` with the device port on your computer.

## Release assets

- `merged-firmware.bin`: complete image containing bootloader, partition table, boot app, and firmware.
- `firmware.bin`: application image for advanced/manual flashing.
- `complete-application-source.zip`: complete application source archive.
- `lgpl-component-sources.zip`: resolved LGPL component sources.
- `lgpl-relink-kit.zip`: LGPL relinking materials.
- `SBOM.spdx.json`: SPDX software bill of materials.
- `SHA256SUMS.txt`: SHA-256 checksums for release artifacts.

