# Page Turn Visible Idle Cache Guard V35

This experimental build keeps the v34 single-refresh / single-pending-page-turn model, and adds a conservative guard between visible page display and background frame-cache rendering.

## Problem addressed

A few pages could look like the previous/adjacent page was not cleared before the next page was shown. One reproducible-looking case was turning from page 8 back to page 7 and seeing page 7 mixed with page 6.

The likely cause was not a dirty frame-cache slot. In v34, the visible reader page and the background cache job share the renderer framebuffer. After a visible page-turn display returned, the background cache job could start immediately and draw the adjacent page into the shared renderer before the e-paper controller had fully settled.

## Changes

- After each visible reader page display, call `renderer.waitDisplayIdle()`.
- Record the visible-display idle point with `lastVisibleDisplayIdleAt`.
- Reset `lastReaderInputAt` at the same time, so frame-cache warming observes the existing idle delay.
- Actually enforce `pageFrameCacheIdleDelayMs` before starting a new background frame-cache job.
- Actually enforce `pageFrameCacheWorkCooldownMs` between background frame-cache jobs.
- Wait for the image-page pre-clean `FAST_REFRESH` to become idle before drawing the final image page.
- Add `GFX` logging of the resolved display mode and whether a full refresh override was consumed.

## Expected behavior

Visible page display should settle before any background cache render modifies the shared renderer framebuffer. This should reduce or remove cases where page N appears mixed with page N-1 or N+1 immediately after a page turn.

## Useful log markers

```text
[GFX] Time = ... ms from clearScreen to displayBuffer mode=... forcedFull=...
[ERS] Visible display idle: source=cache-hit wait=...ms
[ERS] Visible display idle: source=cache-miss wait=...ms
[ERS] Frame cache job start: spine=... page=...
```

For a healthy sequence, `Frame cache job start` should appear only after the visible-display idle line and the configured idle delay.
