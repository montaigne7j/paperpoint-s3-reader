# Paper S3 runtime TTF reader fonts

## Usage

1. Copy a `.ttf`, legacy `.bin`, or `.epdf` font into `/fonts` on the SD card.
2. Open **Settings → Reader → Reader Font File**.
3. Select **Built-in font** or one of the files found in `/fonts`.

TTF is used only for EPUB/TXT reader content in this first version. Menus
and other UI continue to use the configured bitmap UI font.

Legacy bitmap filenames must keep the existing format:

- `FontName_size_WxH.bin`
- `FontName_size_WxH.epdf`

TTF filenames do not need that naming convention. The filename without `.ttf`
is used as the display name.

## Size and cache behaviour

The **Reader Font Size** setting is a numeric picker from **20 to 60 px**.
The selected value is used exactly for runtime TTF rasterization. Built-in and
legacy bitmap fonts remain fixed raster fonts and use the nearest available
built-in size.

Changing the size reloads the TTF and invalidates cached reader page layouts.
Each size gets a separate persistent glyph cache.

Runtime cache layers:

1. OpenFontRender/FreeType cache in PSRAM.
2. CrossPoint glyph bitmap LRU in PSRAM.
3. Persistent glyph records under `/.crosspoint/fontcache/*.ttfc` on SD.

The first page containing new characters is slower because those glyphs must be
rasterized. Repeated pages and later boots reuse the caches. Pending cache
writes are flushed after page prewarming and before deep sleep.

## Current limitations

- `.ttf` is supported; `.ttc`, `.otc`, `.otf`, variable fonts, and color fonts
  are not listed by this version.
- The OpenFontRender v1.2 UTF-8 decoder used here handles BMP codepoints
  (`U+0000`–`U+FFFF`). Unsupported supplementary-plane glyphs fall back to the
  existing reader font path when possible.
- TTF is intentionally not available as the UI font in phase 1.

## Useful log lines

```
[FONT_MGR] Found TTF font: ...
[TTF] Persistent glyph index: ... records
[TTF] Loaded /fonts/... at ...px
[TTF] Prewarm: new/unique glyphs in ...ms
```
