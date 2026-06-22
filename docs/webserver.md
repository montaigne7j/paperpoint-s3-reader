# Web Server Guide

The Wi-Fi transfer and built-in web server feature is currently treated as unverified in PaperPoint S3 Reader. This document is kept as implementation notes rather than a user-facing promise.

## What the feature is intended to do

When enabled from the device settings, the web server is intended to let another device on the same local Wi-Fi network manage files on the Paper S3 SD card through a browser.

Expected operations:

- Upload EPUB or TXT files.
- Browse SD card folders.
- Create folders for organizing books.
- Rename, move, or delete files.

## Security notes

- The server uses plain HTTP on the local network.
- There is no authentication.
- Anyone on the same Wi-Fi network may be able to access the file manager while the feature is active.
- Saved Wi-Fi credentials may be stored on the SD card in plaintext.

Use this only on a trusted private network until the feature is tested and documented again.

## Documentation assets

The old screenshots were removed because their source and reuse rights were not fully documented. Add new maintainer-owned screenshots before publishing this as end-user documentation.