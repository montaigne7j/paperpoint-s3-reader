# Built-in CJK scalable reader font

This change makes the embedded Traditional Chinese fallback font participate in reader font-size layout instead of staying fixed at one bitmap size.

## Source and logical grid

The embedded source raster is now `31×39`, but layout keeps the historical logical CJK grid:

```text
logical target = 21×30 × reader CJK scale
source raster  = 31×39
```

The renderer resamples from the larger source raster into the logical target. This improves the default 1.5x quality without making the page layout unexpectedly larger.

## Scale mapping

The existing reader font-size setting remains `20..60`, and the built-in CJK fallback maps that value to a logical bitmap scale:

| Reader font size | Built-in CJK logical scale | Target logical cell |
|---:|---:|---:|
| 20 | 0.8x | about 17×24 |
| 36 default | 1.5x | about 32×45 |
| 60 | 2.5x | about 53×75 |

The values between these points are piecewise-linear.

## Layout behavior

- Horizontal layout uses the scaled CJK advance / line height when measuring text.
- Vertical layout uses a tighter visible-ink advance so character spacing `0 px` is closer without relying on full cell padding.
- Character spacing still controls only the extra distance between adjacent characters.
- Line spacing still controls horizontal line distance / vertical column distance.
- UI fonts are not affected by reader font-size changes; UI CJK fallback is resampled back to the old logical 21×30 size.

## Cache behavior

Section cache version is bumped to `38`, so existing cached chapters rebuild once after updating.

## Commit message

```text
Built-in CJK 31x39 source and tighter vertical advance
```

## Validation targets

Recommended checks:

- Built-in font, horizontal Chinese EPUB: font size 20 / 36 / 60 changes actual CJK glyph size.
- Built-in font, vertical Chinese EPUB: font size 20 / 36 / 60 changes CJK glyph size and page layout.
- Character spacing `0 px` is visibly tighter in vertical mode but not overlapping.
- UI screens are not globally enlarged by the larger embedded CJK source.
