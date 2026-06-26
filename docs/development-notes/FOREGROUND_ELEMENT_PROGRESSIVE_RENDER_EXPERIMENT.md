# V30 Foreground Element Progressive Render Experiment

This experiment changes the foreground cache-miss render path for vertical text pages.

## Goal

V29 attempted to wrap the normal full-page `page->render()` flow with 10 visual stripes. Logs showed that this still behaved like a full-page render first, then display, so the user could not see true progressive rendering.

V30 renders each `PageElement` directly:

```text
cache miss, vertical text page
  render PageElement 1
  refresh only that element's physical row range
  render PageElement 2
  refresh only that element's physical row range
  ...
```

This should make vertical pages appear column-by-column instead of waiting for the whole page to finish rendering.

## Scope

Enabled for:

- Paper S3 builds
- foreground cache misses
- text-only EPUB pages
- vertical layout

Cache hits are unchanged and still use the framebuffer cache fast path.

Image pages and horizontal layout still use the normal full-page render path.

## Direction

The render order follows the same reading-direction semantics as band-scan page turns:

```text
Vertical layout:
  next page     = right to left
  previous page = left to right
```

The experiment computes physical row order directly, so it still uses the intended direction even if the user-facing refresh mode is set to Original.

## Logs

Look for `PRG` lines:

```text
[PRG] foreground element progressive start: elements=10 order=physical-row-ascending ...
[PRG] element 1/10 index=0 tag=1 rows=... draw=... displayCall=...
[PRG] element 2/10 index=1 tag=1 rows=... draw=... displayCall=...
[PRG] foreground element progressive done: elements=10 draw=... text=... displayCalls=...
[ERS] Page render progressive-elements: ...
```

If the log still shows only:

```text
[PGE] page start
[PGE] page done
[ERS] Page render: ...
```

then this path was skipped, usually because the page has images or the reader is not in vertical layout.

## Known tradeoff

This improves visible first response, not necessarily total time. Each element refresh uses a row-range EPD update, so the total time may be similar or slightly longer than one full-page cache-miss render, but the user should see the new page begin appearing much earlier.
