# UI spacing / flash-size self-assessment

## Implemented
- One 21x30 embedded CJK font is retained.
- UI_10 and SMALL_FONT CJK glyphs are rendered at 100% at runtime.
- The duplicate 15x21 full CJK artifact was removed.
- Measurement, ascender, line height, normal drawing and rotated drawing use the same scale.
- Settings and vertical-reading spacing changes remain enabled.

## Size assessment
The removed artifact contained 760,187 bitmap bytes, 31,338 glyph records (about 501,408 bytes at 16 bytes each), interval data and metadata: approximately 1.26 MiB total.
Starting from the reported 8,091,063-byte firmware, the expected new size is about 6.83 MiB, below the 7,340,032-byte OTA slot by roughly 0.49 MiB.
The actual size must be verified by the target PlatformIO build.

## Visual assessment
Area sampling preserves thin strokes better than nearest-neighbor scaling. Rendering will cost more CPU for compact Chinese UI text, but UI/status strings are short and the cost should be acceptable.
