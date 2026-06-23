# Vertical punctuation and alignment fix

This change fixes vertical-layout punctuation direction and the built-in font path used by ASCII letters / digits.

## Changes

- CJK ideographs are never intentionally rotated in vertical layout.
- Common East Asian punctuation is rendered through vertical presentation forms first:
  - `「」` -> `﹁﹂`
  - `『』` -> `﹃﹄`
  - `（）` / `()` -> `︵︶`
  - `〈〉` -> `︿﹀`
  - `《》` -> `︽︾`
  - `【】` -> `︻︼`
  - `〔〕` -> `︹︺`
  - `[]` / `［］` -> `﹇﹈`
- If a selected font does not contain the vertical presentation glyph, the renderer falls back to the previous 90-degree rotation path for known punctuation only.
- Vertical rendering now tries these centered cell paths in order:
  1. external reader font centered glyph
  2. built-in primary font centered glyph
  3. built-in CJK fallback centered glyph
- ASCII letters and numbers in built-in-font vertical mode no longer fall back directly to horizontal `drawText()` unless all centered paths fail.

## Commit message

```text
Vertical punctuation and alignment fix
```

## Validation targets

Recommended samples:

```text
「與」
『測試』
（第12章）
《中文》ABC123
【註】〔一〕
```

Expected behavior:

- Corner quotes and brackets use vertical forms instead of appearing upside-down.
- Han characters remain upright.
- Built-in-font English and numbers are centered in the same vertical cell as CJK glyphs.
