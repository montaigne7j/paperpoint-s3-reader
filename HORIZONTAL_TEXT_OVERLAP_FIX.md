# Horizontal Text Overlap Fix

## Problem
When the reader layout is set to horizontal mode, digits and punctuation such as `...` / `…` could visually overlap the following CJK character.

## Root cause
The horizontal line layout uses glyph advance values to place each word and each glyph. Some built-in or external font glyphs, especially digits and punctuation, may report an advance that is smaller than the actual bitmap's right edge. When the next CJK glyph is placed using that too-small advance, the two bitmaps can collide.

A second contributing factor is CJK no-space chunk boundaries. They correctly avoid inserting a full word space, but a zero-pixel boundary is too tight when ASCII digits or punctuation touches CJK text.

## Fix
- Protect built-in EpdFont advance metrics by ensuring visible glyph ink fits inside the advance.
- Apply the protection more strongly to digits, ASCII punctuation, Unicode punctuation such as U+2026 ellipsis, CJK punctuation, and full-width forms.
- Apply the same glyph-bounds protection to external rich-metrics fonts.
- Add a tiny 2 px guard gap only at no-space CJK boundaries where digits/punctuation touches CJK text.
- Keep normal CJK-CJK boundaries unchanged.

## Modified files
- `lib/GfxRenderer/GfxRenderer.cpp`
- `lib/GfxRenderer/ExternalFontHelpers.cpp`
- `lib/Epub/Epub/ParsedText.cpp`

## Notes
This changes horizontal metrics, so the section cache version is bumped to 33 so old cached horizontal pages are rebuilt automatically.
