# v24 Touch Gesture Classification and Reader Content Inversion

## Scope

This experiment builds on v23 gesture support and fixes the touch classifier so a single touch sequence can only become one of:

1. horizontal swipe,
2. vertical swipe,
3. normal tap.

It also adds settings for enabling/disabling swipe page turns and for rendering reader content as black background with white/inverted foreground.

## Touch classification

The touch path now follows this order:

```text
finger down / move
  -> detect whether horizontal swipe threshold is reached
  -> if horizontal swipe is confirmed, emit the swipe immediately when enabled
  -> suppress tap-zone buttons for the rest of that touch sequence

finger up
  -> if the sequence was already a swipe, do not treat it as a tap
  -> otherwise classify horizontal swipe, vertical swipe, or tap
  -> only if it is not a swipe, map the start position to the tap zone
```

This prevents the previous mixed behavior where a center/left/right tap and a swipe could both become true for the same finger motion.

## New Controls setting

`Swipe Page Turn` / `滑動翻頁`

- Stored as `swipePageTurnEnabled`.
- Default: enabled.
- When disabled, horizontal swipe gestures are still recognized as movement and do not become taps, but they do not trigger page turns.

## New Display setting

`Invert Reader Content` / `內文黑白翻轉`

- Stored as `readerContentInvert`.
- Default: disabled.
- When enabled, the reader content area is drawn as black background with white text.
- Image pixels are inverted through the same render path.
- Status bar and other UI remain normal instead of being globally inverted.

## Notes

The framebuffer cache stores the final rendered frame, so cached pages naturally preserve the current inversion setting. Returning from settings clears the frame cache and re-renders with the updated setting.
