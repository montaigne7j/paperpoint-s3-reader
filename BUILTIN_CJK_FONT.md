# Built-in Traditional Chinese font

## Identity

The firmware bundles a compact bitmap derivative named **PaperPoint Sans TC
Medium**. The distinct derivative name is used because the source Font Software
was format-converted and subset/cropped for an embedded target.

The maintainer supplied the fixed-cell raster source as:

```text
Noto Sans CJK TC Medium 23.5pt.31×39.bin
```

It was renamed in this repository to:

```text
lib/EpdFont/builtinFonts/source/PaperPointSansTC/
  PaperPointSansTC-Medium-23_5pt-31x39.bin
```

Source raster SHA-256:

```text
0d75d0abcea1f3ce12512686fa5cfb4140cc8066fc68095aab271678e081f34a
```

The supplied raster is treated as the project source raster generated from Noto
Sans CJK TC Medium. Its exact embedded-input SHA-256 is recorded above and
enforced by the generator and release-compliance checks.

## Licence

Noto Sans CJK is distributed under the SIL Open Font License 1.1. The converted
bitmap data remains under OFL-1.1 and is not relicensed under the project's MIT
licence. See:

```text
LICENSES/OFL-1.1-NotoSansCJK.txt
```

The generated derivative deliberately uses the primary name `PaperPoint Sans TC`
rather than presenting itself as an original Noto font.

## Embedded representation

Generator:

```text
scripts/embed_legacy_cjk_font.py
```

Generated source:

```text
lib/EpdFont/builtinFonts/paperpoint_sans_tc_15_5_medium.h
```

The input is a 65,536-entry, BMP-direct-index, 1-bit fixed-cell font. The
generator:

1. verifies the exact input size and SHA-256;
2. detects repeated non-empty missing-glyph boxes;
3. retains Traditional Chinese, CJK punctuation, Bopomofo, Japanese kana and
   useful symbol ranges;
4. removes unsupported/tofu entries and unused blank entries;
5. crops each retained glyph to its ink bounds;
6. tightly bit-packs the cropped glyphs for direct random access from flash;
7. emits sparse Unicode intervals and `EpdFontData` metadata.

Current generated result:

- source cell: 31 × 39 pixels;
- fixed advance: 31 pixels;
- retained glyphs: 31,338;
- Unicode intervals: 37;
- packed bitmap bytes: 3,126,747;
- estimated total firmware data including glyph metadata: approximately
  3.5 MiB;
- no runtime decompression or PSRAM allocation is required.

Runtime layout still uses the historical logical CJK grid of 21 × 30 pixels.
The larger 31 × 39 source is resampled into the logical target size, so the
reader keeps its existing layout scale while improving built-in CJK stroke
quality.

Rebuild command:

```powershell
python scripts/embed_legacy_cjk_font.py `
  "lib/EpdFont/builtinFonts/source/PaperPointSansTC/PaperPointSansTC-Medium-23_5pt-31x39.bin" `
  "lib/EpdFont/builtinFonts/paperpoint_sans_tc_15_5_medium.h" `
  --name paperpoint_sans_tc_15_5_medium `
  --width 31 --height 39 --advance 31 `
  --source-sha256 0d75d0abcea1f3ce12512686fa5cfb4140cc8066fc68095aab271678e081f34a
```

## Runtime behaviour

- The embedded font is always registered as the Traditional Chinese fallback.
- It is used for CJK/full-width codepoints and when the selected proportional
  built-in Latin font lacks an exact glyph.
- UI and EPUB text therefore remain readable without an SD-card font.
- A user-selected external UI or reader font still has priority; the embedded
  font remains available for missing glyphs.
- Existing Latin families continue to supply proportional Latin text and bold.
  The embedded CJK derivative is one fixed medium weight.

## Flash partition requirement

The repository includes `partitions.csv` with two 7 MiB OTA application slots.
The larger 31 × 39 embedded CJK font requires the font/hyphenation slimming
changes documented in `FONT_FLASH_SLIMMING.md`. A release build must verify the
final `firmware.bin` size against the 7 MiB application partition.
