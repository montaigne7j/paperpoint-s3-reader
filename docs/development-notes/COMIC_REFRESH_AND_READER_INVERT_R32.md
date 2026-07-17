# R32 comic refresh and reader inversion

## Comic reader

R32 removes the comic 4-level grayscale menu option. The CBZ reader now always uses the smoother grayscale/dither path.

A new comic-specific full-refresh frequency is stored in settings:

```text
0 = every page
1 = every 2 pages
2 = every 5 pages
3 = every 10 pages
4 = never
```

The Comic Reader Menu exposes this as:

```text
每頁 / 每2頁 / 每5頁 / 每10頁 / 不全刷
```

CBZ rendering uses this comic setting instead of the global novel-reader refresh frequency.

## Novel reader inversion

Black/white inversion now affects the whole novel page stack:

- page background,
- selected reader background PNG,
- guide lines,
- text/content,
- status bar.

For background PNGs, the renderer first draws the PNG normally, then inverts the framebuffer and stores that inverted version in the reader-background cache. The cache key includes the invert state so normal and inverted backgrounds are not mixed.

Guide lines are drawn even in inverted mode and use the renderer's inverted drawing path, so they become light guide lines on dark backgrounds.
