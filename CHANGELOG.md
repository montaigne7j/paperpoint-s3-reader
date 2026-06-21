## 1.4.0 — 2026-06-21 — Large UI and Traditional Chinese release

- Added the **Large Text** UI theme for settings, file browser, recent books, reader menu, and reader status bar.
- In the Large Text theme, the reader clock and chapter/book title share the same left status-bar slot; enabling one hides the other.
- Changed Large Text bottom hints to compact symbols (`<<`, `o`, `^`, `v`) to keep the oversized UI readable.
- Added the built-in **PaperPoint Sans TC Medium** Traditional Chinese fallback font and made it the built-in UI CJK fallback.
- Updated the Chinese README and web installer page with version 1.4.0 notes, browser flashing steps, supported sleep image formats, and current feature limitations.

## 2026-06-21 — EPUB image cache reliability fix

- Prevented silent next-chapter indexing from persisting transient image failures as permanent `[Image: alt]` section caches.
- Added three-attempt retry handling for EPUB PNG/JPEG extraction and image-dimension reads.
- Increased the section cache format from version 28 to 29 so previously degraded caches rebuild automatically.
- Kept foreground parsing tolerant for genuinely unsupported or damaged images.

## 2026-06-21 — Built-in CJK compile fix

- Restored the missing `#ifndef OMIT_FONTS` guard removed during the Bookerly cleanup.
- Kept Noto Sans 14 available outside the optional-font guard so reduced builds retain a default reader font.
- Moved Noto Sans 14 registration outside the matching `OMIT_FONTS` block.
- Revalidated all conditional-compilation directives in `src/main.cpp`.

## 2026-06-20 — Licence compliance remediation

- Removed the proprietary built-in reading font and all generated/source references.
- Migrated the default reader font to Noto Sans with backward-compatible settings conversion.
- Renamed generated OpenDyslexic and Ubuntu derivatives to comply with font naming terms.
- Added EPD_Painter, GC16, hyphenation, font, LGPL release, asset, SBOM and third-party notices.
- Pinned PlatformIO library versions and added automated compliance/SBOM/relink-kit checks.
- Added packaging of the exact resolved ArduinoWebSockets and Arduino-ESP32 source trees for each binary release.

# Changelog

## Unreleased

### UI / UX
- **Vertical text centering** in all list rows (file browser, settings, recent books) — text, icons, subtitles, and values are now properly centered within their row height using dynamic font metrics instead of hardcoded pixel offsets
- **Tap-friendly row heights** for in-book menus on PaperS3: chapter selection, footnotes, and reader menu rows increased from 30–36px to 75px with vertically centered text
- **Lyra 3-Covers theme PaperS3 fix** — added PaperS3-specific metrics (120px menu/list rows, 16px spacing) so touch hit-testing matches the rendered layout; previously all taps mapped to wrong items due to metrics mismatch (64px vs 120px)
- **Multi-cover touch selection** — tapping a specific book cover in the 3-cover home layout now opens that book instead of always opening the first one (uses touch X coordinate)
- **Boot splash footer** aligned with in-app status bar position and pushed 2px higher to prevent descender ghosting (letters like j, g)
- **Reader status bar footer** pushed 2px higher to avoid descender mirroring at the screen edge
- **First-open cover skip** — books now skip the cover page on first open; uses a proper `isFirstOpen` flag (progress.bin existence) instead of the unreliable `spineIndex == 0` check, with fallback to spine 1 when no text reference is found

### Rendering
- **Background-prepared custom sleep images** on Paper S3: `.bmp`, `.jpg`/`.jpeg`, and `.png` files are decoded and converted to validated GC16 caches while the device is idle, so shutdown never waits for image processing
- **Arbitrary-size sleep images**: opaque images preserve aspect ratio and use fixed center-crop fill for the 540×960 screen; transparent PNG overlays preserve aspect ratio, fit entirely without cropping, and blend with the current reading page or a white background
- **Fail-safe sleep fallback**: shutdown uses the newly prepared cache, then the previous valid cache, then the built-in sleep screen; incomplete `.tmp` files are never displayed
- **Force full e-ink refresh** on every activity transition (PaperS3) to eliminate ghosting artifacts
- **VIEWABLE_MARGIN_BOTTOM** reduced from 22 to 16 to reclaim empty space under the footer

### Performance / Stability
- **JPEGDEC stack overflow fix** — the ~16KB JPEGDEC object is now heap-allocated in PSRAM via placement new instead of overflowing the 8KB render task stack
- **Fast JPEG thumbnail path** — on PaperS3, cover thumbnails are decoded directly from PSRAM using JPEGDEC at 1/8 scale with Floyd-Steinberg dithering, bypassing the slow picojpeg + temp file path
- **JPEGDEC patches** for progressive JPEG support (skip AC Huffman tables) and MCU_SKIP guard to prevent crashes on grayscale chroma skip
- **Build optimization** — `-O2` with `-Os` removed from build_unflags so the speed optimization actually takes effect

### CI / Release
- **GitHub Actions**: fixed `upload-artifact@v6` → `@v4` in CI workflow (v6 doesn't exist)
- **GitHub Release creation**: release workflow now creates a GitHub Release with firmware binaries attached via `softprops/action-gh-release@v2`

### Credits
- Display driver: [EPD_Painter](https://github.com/nickoala/EPD_Painter) by nickoala
### Built-in Traditional Chinese font

- Embedded a compact 21×30 **PaperPoint Sans TC Medium** fallback generated from the maintainer-supplied Noto Sans CJK TC Medium raster.
- Added exact-glyph lookup so proportional Latin families no longer hide missing CJK glyphs behind U+FFFD.
- Added mixed Latin/CJK measurement and rendering for horizontal and vertical text.
- Replaced the 5.9 MB direct-index raster at runtime with 31,338 sparse cropped glyphs (about 1.86 MiB including metadata), with no runtime decompression.
- Stopped auto-forcing the legacy SD-card UI font; saved external choices still override the embedded fallback.
- Added OFL licence, provenance, generator, SBOM entry and compliance checks.
- Restored the referenced `partitions.csv` with two 7 MiB OTA application slots so the larger firmware can still use OTA.
