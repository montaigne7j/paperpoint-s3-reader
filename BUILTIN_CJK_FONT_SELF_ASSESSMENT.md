# Built-in CJK font integration self-assessment

Assessment date: 2026-06-20

## Goal

Make Traditional Chinese UI and EPUB text readable without requiring a font on
the SD card, while keeping firmware storage use reasonable and preserving
external-font overrides.

## Implemented result

| Control | Result | Evidence |
|---|---|---|
| Input format recognised | **Pass** | 10,223,616 bytes = 65,536 BMP entries × 156 bytes; 31×39, row-aligned 1-bit glyph cells |
| Source integrity | **Pass** | SHA-256 `0d75d0abcea1f3ce12512686fa5cfb4140cc8066fc68095aab271678e081f34a` is enforced by the generator and compliance check |
| Flash-size reduction | **Pass** | 31,338 retained glyphs, 37 sparse intervals and 1,444,571 packed bitmap bytes; approximately 1.86 MiB including glyph metadata instead of the 5.9 MB direct-index raster |
| Runtime performance design | **Pass by design** | Direct random-access bitmaps in flash; no DEFLATE, no temporary decompression buffer and no PSRAM glyph-cache requirement |
| Missing-glyph handling | **Pass** | Two repeated tofu/sentinel patterns are discarded; exact-glyph lookup prevents a Latin family's U+FFFD replacement from hiding the CJK fallback |
| Horizontal UI rendering | **Pass by static/host validation** | Mixed proportional Latin plus fixed-cell CJK measurement and rendering are integrated in `GfxRenderer` |
| EPUB layout width | **Pass by static/host validation** | `getTextAdvanceX()` and `getTextWidth()` use the same embedded-fallback advance as drawing |
| Vertical text | **Pass by static validation** | Centered upright glyphs and 90-degree punctuation fallback paths are integrated |
| External font compatibility | **Pass by design** | Explicitly selected external UI/reader fonts retain priority; embedded data remains a missing-glyph fallback |
| No-SD default behaviour | **Pass by design** | The fallback is registered during display/font setup before SD-card font scanning |
| Licence and naming | **Pass** | Distinct derivative name, OFL-1.1 text, source-raster checksum, notices, SBOM and generator are included |
| OTA partition headroom | **Provisioned; final binary check pending** | `partitions.csv` provides two 7 MiB application slots; the actual linked firmware must still be checked |

## Verification completed

- Deterministic regeneration of the checked-in C++ font header: passed.
- Representative glyph presence checks for CJK punctuation, Bopomofo, `中` and
  `體`: passed.
- Absence check for Latin `A` in the CJK fallback: passed; Latin remains
  proportional.
- Host compilation and lookup test for `EpdFont` plus generated font data:
  passed.
- Clang C++ syntax check of the modified `GfxRenderer.cpp` with minimal Arduino
  declaration stubs: passed; only a stub-related override warning remained.
- Header and registration integration syntax test: passed.
- Python syntax, SBOM generation and licence-compliance checks: passed.
- Full PlatformIO build: attempted, but the execution environment could not
  resolve `github.com` to download the pinned platform package, so compilation
  did not start.

## Remaining limitations

1. The CJK fallback is one 31×39 medium-weight source bitmap resampled to the reader/UI logical grid. Reader font-size,
   bold and italic settings still affect the Latin family, but CJK remains the
   same fixed raster size/weight.
2. The embedded set covers useful BMP ranges, including basic CJK, Extension A,
   compatibility ideographs, punctuation, Bopomofo and kana. Supplementary-plane
   CJK extensions such as Extension B (`U+20000` and above) are not embedded.
3. The embedded source raster is the maintainer-supplied `Noto Sans CJK TC
   Medium 23.5pt.31×39.bin`, stored in the repository under the distinct
   derivative filename `PaperPointSansTC-Medium-23_5pt-31x39.bin`.
4. A real `pio run -e default` on the user's machine and an on-device test are
   still required to confirm linked firmware size, baseline appearance, page
   layout and refresh performance.

## Overall assessment

The implementation meets the repository-level requirement for a compact,
licence-documented, no-SD Traditional Chinese default fallback. The design is
materially better suited to the ESP32-S3 than embedding the complete 5.9 MB
direct-index raster, and its measurement/drawing paths are internally
consistent.

It is not yet appropriate to claim a final hardware-qualified release until the
normal PlatformIO build succeeds and representative Chinese UI, horizontal EPUB
and vertical EPUB pages are checked on the Paper S3.
