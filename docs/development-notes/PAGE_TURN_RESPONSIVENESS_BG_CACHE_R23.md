# Page-turn responsiveness and reader background cache (V1.8.4-r23)

## Problem observed in device log

Rapid page turns could feel stuck when reader background PNG and page-frame cache were enabled. The log showed page-turn input being queued as `cache-not-ready` or `render-busy`, then waiting for the background frame-cache job to finish before executing. Each cache job decoded the same `/bg` PNG again, adding about 415 ms before glyph rendering even started.

Typical sequence:

1. User swipes next page.
2. Target page frame cache is not ready, so the input becomes a pending page turn.
3. Background frame-cache warm job decodes the same reader background PNG.
4. The job renders glyph chunks and stores a full framebuffer.
5. The pending turn finally executes.

This made the UI look unresponsive even though the input was detected.

## Changes

- Page-turn input no longer waits for adjacent or target frame-cache readiness.
- If the requested page cache is ready, the existing cache-hit render path is still used.
- If the requested page cache is not ready, the firmware aborts any background warm job and uses the normal visible render path immediately.
- Pending turns created while the EPD is busy also use the same visible fallback instead of waiting for the cache job.
- Reader background PNG rendering now has a PSRAM-backed framebuffer cache keyed by path, fade percentage, and screen size.

## Expected log changes

New useful diagnostics:

- `Reader background PNG cached: ...`
- `Reader background PNG cache hit: ...`
- `Page turn visible fallback: ...`
- `Queued page turn visible fallback: ...`

After the first background render, repeated `PNG decoding complete - render time: 415 ms` lines should largely disappear for the same background file and fade level.

## Tradeoff

A cache miss page turn may render visibly instead of waiting for a pre-rendered framebuffer. That can be slightly slower than a cache hit, but it removes the extra wait-before-render that made the reader feel stuck.
