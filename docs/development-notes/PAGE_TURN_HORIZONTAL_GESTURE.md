# Page Turn Horizontal Gesture Experiment (v23)

Base: `crosspoint-reader-papers3_V1.6.0-page-frame-cache-cooperative-v22-compilefix.zip`

## Purpose

Add reader page-turn gestures that feel faster than release-based swipes:

- Horizontal reading layout: swipe right = next page, swipe left = previous page.
- Vertical reading layout: swipe left = next page, swipe right = previous page.
- The gesture fires as soon as the horizontal movement threshold is reached while the finger is still down.
- It does not wait for finger release.

## Implementation

### HalGPIO

Added reader-only horizontal swipe virtual buttons:

- `BTN_SWIPE_LEFT`
- `BTN_SWIPE_RIGHT`

`HALGPIO_NUM_BUTTONS` is increased from 10 to 12.

While a single finger is down and footer mode is disabled, `HalGPIO::update()` tracks movement from the touch-down point. Once horizontal movement is dominant and exceeds the threshold, it emits a swipe button immediately.

The release of that same touch is then ignored for tap classification so a center-start swipe does not also open the reader menu.

### ReaderUtils

`ReaderUtils::detectPageTurn()` now maps horizontal swipe gestures according to reader layout:

- Horizontal layout:
  - swipe right -> next
  - swipe left -> previous
- Vertical layout:
  - swipe left -> next
  - swipe right -> previous

The existing tap-zone direction behavior is unchanged.

## Tunable constants

In `lib/hal/HalGPIO.h`:

```cpp
static constexpr int16_t HORIZONTAL_SWIPE_THRESHOLD = 80;
static constexpr int16_t HORIZONTAL_SWIPE_DOMINANCE_MARGIN = 20;
```

## Notes

This version keeps the v22 cooperative frame-cache behavior. A horizontal swipe produces a `wasPressed()` edge while the finger is still down, so cooperative cache warm jobs can be aborted before finger release.
