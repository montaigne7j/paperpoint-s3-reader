# Third-party notices

This project is primarily distributed under the MIT licence in `LICENSE`.
Files and components listed below remain under their own licences; the project
MIT licence does not relicense them. Release archives must include this file
and the complete `LICENSES/` directory.

## Bundled or modified source

- **EPD_Painter** — Apache-2.0. Original project by Tony Weston and contributors. Local modifications are described in `lib/EPD_Painter/NOTICE`.
- **GC16 waveform data derived from LovyanGFX/M5GFX** — BSD-2-Clause/FreeBSD notice, copyright (c) 2020 lovyan03. See `LICENSES/BSD-2-Clause-LovyanGFX.txt`.
- **Expat** — MIT-style licence; original notices remain in `lib/expat`. Its bundled SipHash implementation is CC0-1.0; see `LICENSES/CC0-1.0.txt`.
- **uzlib** — zlib licence; original notices remain in `lib/uzlib`; see `LICENSES/Zlib.txt`.
- **Hyphenation pattern data** — language-specific licences and copyrights documented in `HYPHENATION_LICENSES.md`.

## Fonts

- **PaperPoint Sans TC Medium generated bitmap data** — fixed-cell, cropped and sparse-packed derivative generated from the maintainer-supplied Noto Sans CJK TC Medium raster; SIL Open Font License 1.1. The derivative uses a distinct primary name. See `BUILTIN_CJK_FONT.md` and `LICENSES/OFL-1.1-NotoSansCJK.txt`.
- **Noto Sans source and generated bitmap data** — SIL Open Font License 1.1, copyright 2022 The Noto Project Authors. See `LICENSES/OFL-1.1-NotoSans.txt`.
- **ReaderDyslexic generated bitmap data** — derived from OpenDyslexic under SIL OFL 1.1. The generated derivative was renamed so it does not use the Reserved Font Name `OpenDyslexic`. The unmodified source font files retain their original names. See `LICENSES/OFL-1.1-OpenDyslexic.txt`.
- **Ubuntu derivative PaperPoint bitmap data** — format-converted derivative of Ubuntu Font Family, renamed in accordance with the Ubuntu Font Licence 1.0. See `LICENSES/UFL-1.0.txt`.
- **OpenFontRender / FreeType** — FreeType Project License. Portions of this software are copyright © The FreeType Project. All rights reserved. See `LICENSES/FTL.txt` and `THIRD_PARTY_NOTICES_TTF.md`.

## PlatformIO-resolved dependencies

The exact versions are pinned in `platformio.ini` and recorded in `SBOM.spdx.json`. Principal licences include:

- Arduino-ESP32 framework — LGPL-2.1.
- ArduinoWebSockets — LGPL-2.1.
- SdFat — MIT.
- ArduinoJson — MIT.
- QRCode by ricmoo — MIT.
- PNGdec and JPEGDEC — Apache-2.0.
- M5Unified and M5GFX — MIT, with additional third-party notices in their upstream distributions.
- OpenFontRender — FreeType Project License.

For binary firmware distribution, follow `BINARY_RELEASE_LGPL_COMPLIANCE.md`;
a firmware binary must not be published alone. A successful release build also
collects the exact upstream dependency licence/notice files into
`dist/dependency-licenses/` and archives the resolved LGPL component source
trees as `dist/lgpl-component-sources.zip`.

## Assets

Visual assets, screenshots, and logos are listed in `ASSETS_LICENSES.md`.
No trademark rights are granted by the software licences.
