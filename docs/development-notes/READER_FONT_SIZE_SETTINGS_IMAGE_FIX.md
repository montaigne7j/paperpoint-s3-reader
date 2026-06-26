# Reader font size, in-reader settings, and EPUB image fixes

## Reader font size

- `Settings -> Reader -> Font Size` opens a numeric picker.
- Range: 20 to 60 px, step 1 px.
- TTF uses the selected pixel size exactly.
- Built-in and legacy bitmap fonts remain pre-rasterized and use the nearest
  available built-in size.

## Reader menu

The EPUB reader menu now contains `Reader Settings`. It opens the normal
Settings screen directly on the Reader category and returns to the same book
and reading position after closing.

## EPUB images

The parser now supports:

- `img`, SVG `image`, and `svg:image` elements
- `src`, lazy-load attributes, `srcset`, `href`, and `xlink:href`
- relative, root-relative, package-root, percent-encoded, and query/fragment
  image references
- JPEG/PNG resources without a filename extension or with a misleading suffix,
  using file-signature detection

Existing section caches are invalidated once by the cache version bump. In
Reader Settings, set `Images` to `Display`. JPEG and PNG are supported. Native
SVG, WebP, GIF, data-URI, and remote images still fall back to alt text.

Useful logs:

```
[EHP] Found image: src=...
[EHP] Resolved image: ... -> ...
[EHP] Image dimensions: ...
```
