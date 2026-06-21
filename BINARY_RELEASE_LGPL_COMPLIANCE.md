# Binary firmware release and LGPL compliance plan

This project links LGPL-licensed components into ESP32 firmware, notably the
Arduino-ESP32 framework and ArduinoWebSockets. This document defines the
mandatory release package used by this project. It is an engineering compliance
plan, not a substitute for legal advice.

## Chosen compliance approach

Every public binary release must provide, from the same download location:

1. Firmware binaries (`firmware.bin` and, when offered, merged firmware).
2. Complete corresponding application source for that exact tag, including
   build scripts, `platformio.ini`, local modifications, notices, and licence
   files.
3. `lgpl-component-sources.zip`, produced by
   `scripts/package_lgpl_sources.py`, containing the exact resolved
   ArduinoWebSockets and Arduino-ESP32 source package trees used for the build.
4. The exact PlatformIO platform URL and exact dependency versions.
5. `SBOM.spdx.json`.
6. An LGPL relink kit produced by `scripts/package_lgpl_relink_kit.py`, containing
   object files, static archives, ELF/map files, build configuration, and a
   manifest sufficient to reproduce or relink the firmware after replacing an
   LGPL library.
7. `THIRD_PARTY_NOTICES.md`, this document, the complete `LICENSES/`
   directory, and the exact upstream licence/notice files collected from
   resolved PlatformIO packages.
8. A written offer, valid for at least three years when distribution is not
   solely by network download, to provide the corresponding LGPL source and
   relink materials at no more than the cost of distribution.

## Release operator checklist

```text
[ ] Build only from a clean, immutable release tag.
[ ] Run: python scripts/check_license_compliance.py
[ ] Run: pio run -e gh_release
[ ] Run: python scripts/generate_sbom.py --output SBOM.spdx.json
[ ] Run: python scripts/collect_dependency_licenses.py --env gh_release
[ ] Run: python scripts/package_lgpl_sources.py --env gh_release
[ ] Run: python scripts/package_lgpl_relink_kit.py --env gh_release
[ ] Archive complete source from the same tag.
[ ] Publish firmware, application source, LGPL component source archive,
    SBOM, relink kit, notices and LICENSES together.
[ ] Keep the published source and relink materials available as long as the binary is offered.
[ ] Preserve build logs and SHA-256 checksums for release records.
```

## User relinking procedure

A recipient may replace an LGPL component with a modified compatible version,
then rebuild with PlatformIO using the pinned platform and dependency versions.
The relink kit preserves the exact object/archive outputs and map/ELF metadata
from the official build. The separate LGPL component source archive preserves
the resolved library/framework source trees. See the `README.txt` files inside
those archives. The release terms must not prohibit reverse engineering for
debugging modifications to the LGPL components. Device installation and
security details must be documented accurately; maintainers should obtain legal review if a
product distribution adds signing or installation restrictions.

## Source offer template

> This firmware includes software licensed under the GNU Lesser General Public
> License version 2.1. Complete corresponding source code, build
> scripts, licence texts, and relink materials for this release are available
> from the same release page. For a physical copy, contact the distributor.
> This offer remains valid for at least three years after the last distribution
> of this firmware version; reasonable media and shipping costs may apply.
