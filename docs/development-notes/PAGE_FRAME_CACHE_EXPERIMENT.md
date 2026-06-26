# Page Framebuffer Cache Experiment

This build keeps four Paper S3 reader framebuffers in PSRAM so a page turn can reuse an already rendered page and call `displayBuffer()` immediately.

## Slots

The cache has four independent slots.  A slot is identified by:

- `spineIndex`
- `pageNumber`
- screen width / height

Framebuffers are **not shifted or moved** when the page changes.  The reader searches the slot metadata and copies only the matching framebuffer into the renderer when it is needed.

## Intended coverage

The four slots are intended to cover:

1. current page
2. previous page
3. next page
4. next-next page

This is an intended coverage set, not a physical slot order.

## Background warm priority

When the reader is idle, the background warmer renders at most one missing page per pass.  Existing cached pages are skipped; they are never rendered again just because they are current or previous.

Priority for missing pages is:

1. next page: `current + 1`
2. current page: `current`
3. next-next page: `current + 2`
4. previous page: `current - 1`

So if the current page or previous page is already in cache, the warmer does not rebuild it.  It only fills whichever of the four target pages is missing, in the priority order above.

## Hit path

When rendering the current page, the reader first checks the cache:

- hit: copy cached framebuffer to the renderer and display immediately
- miss: render the page normally, display it, then store that framebuffer into a cache slot

## Notes

This is an experimental reader-only optimization.  It does not change page-turn waveform behavior.
