# Foreground Progressive Render Experiment (v29)

This build experiments with foreground progressive rendering for Paper S3 reader cache misses.

## Goal

When a reader page is not already in framebuffer cache, the old path rendered the complete page first and only then started the page-turn refresh. On CJK vertical TTF pages this can block the reader for roughly 0.5-1.0 seconds.

The v29 experiment changes text-only vertical pages in Band-scan mode to:

1. Clear/prepare the target framebuffer.
2. Split the physical panel rows into 10 stripes.
3. Render only the text elements that first intersect the current stripe.
4. Immediately refresh that physical row stripe using the transition-aware page-turn waveform.
5. Repeat for the remaining stripes.
6. Draw and refresh the status bar at the end.
7. Store the completed framebuffer into the normal 4-slot page framebuffer cache.

## Scope

Enabled only when all of these are true:

- Paper S3 build (`CROSSPOINT_PAPERS3`).
- Reader page is a cache miss.
- Page has no images.
- Reading layout is vertical.
- Page turn refresh mode is Band-scan mode, not original refresh mode.

Cache hits still use the existing fast cache path. Image pages and horizontal layout still use the existing full-page render path.

## New low-level display path

Added:

- `EPD_Painter::paintRowRange(framebuffer, rowStart, rowEnd)`
- `HalDisplay::displayBufferRows(rowStart, rowEnd)`
- `GfxRenderer::displayPhysicalRows(rowStart, rowEnd)`
- `GfxRenderer::logicalRectToPhysicalRows(...)`

`paintRowRange()` drives only the active physical rows. Rows outside the range are sent as neutral waveform data, and the internal 2bpp screenbuffer is updated only for the active row range.

## Expected effect

This does not necessarily reduce the total time required to render a page. The same TTF glyph drawing work still exists. The goal is to show the first part of the new page much earlier, so the reader feels more responsive than waiting for a full 0.8-1.0 second render before any visible change.

## Log tags

Look for:

```text
[PRG] foreground progressive start
[PRG] stripe ... physicalRows=... elements=... draw=... displayCall=...
[ERS] Page render progressive: ...
```

## Known limitations

- First version is intentionally vertical-layout only.
- Touch is polled between stripes and logged if seen, but this version does not switch to a new page in the middle of an ongoing progressive render.
- A stripe may have zero text elements; it is still refreshed so blank margins/background in that stripe are updated.
