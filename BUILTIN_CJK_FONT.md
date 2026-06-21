# Built-in Traditional Chinese font

## Identity

The firmware bundles a compact bitmap derivative named **PaperPoint Sans TC
Medium**. The distinct derivative name is used because the source Font Software
was format-converted and subset/cropped for an embedded target.

The maintainer supplied the fixed-cell raster source as:

```text
Noto Sans CJK TC Medium 15.5pt.21×30.bin
```

It was renamed in this repository to:

```text
lib/EpdFont/builtinFonts/source/PaperPointSansTC/
  PaperPointSansTC-Medium-15_5pt-21x30.bin
```

Source raster SHA-256:

```text
bf143dd1bb632af7af6107dc4e32e8426e5cd375580a77300982a3f697dcb6fc
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

- source cell: 21 × 30 pixels;
- fixed advance: 21 pixels;
- retained glyphs: 31,338;
- Unicode intervals: 37;
- packed bitmap bytes: 1,444,571;
- estimated total firmware data including glyph metadata: approximately
  1.86 MiB;
- no runtime decompression or PSRAM allocation is required.

Rebuild command:

```powershell
python scripts/embed_legacy_cjk_font.py `
  "lib/EpdFont/builtinFonts/source/PaperPointSansTC/PaperPointSansTC-Medium-15_5pt-21x30.bin" `
  "lib/EpdFont/builtinFonts/paperpoint_sans_tc_15_5_medium.h" `
  --name paperpoint_sans_tc_15_5_medium `
  --width 21 --height 30 --advance 21 `
  --source-sha256 bf143dd1bb632af7af6107dc4e32e8426e5cd375580a77300982a3f697dcb6fc
```

## Runtime behaviour

- The embedded font is always registered as the Traditional Chinese fallback.
- It is used for CJK/full-width codepoints and when the selected proportional
  built-in Latin font lacks an exact glyph.
- UI and EPUB text therefore remain readable without an SD-card font.
- A user-selected external UI or reader font still has priority; the embedded
  font remains available for missing glyphs.
- Existing Latin families continue to supply proportional Latin text, bold,
  italic and kerning. The embedded CJK derivative is one fixed medium weight.
## Flash partition requirement

The repository includes `partitions.csv` with two 7 MiB OTA application slots.
This leaves sufficient headroom for the approximately 1.86 MiB embedded font
while preserving OTA updates. A release build must still verify the final
`firmware.bin` size against the 7 MiB application partition.
