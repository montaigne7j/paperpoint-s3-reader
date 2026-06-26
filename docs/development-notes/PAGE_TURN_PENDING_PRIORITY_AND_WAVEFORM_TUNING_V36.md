# Page Turn Pending Priority and Waveform Tuning v36

This experiment builds on v35 visible-idle cache guard.

## Pending page turn behavior

- Normal idle cache delay is reduced:
  - `pageFrameCacheIdleDelayMs = 120`
  - `pageFrameCacheWorkCooldownMs = 80`
- If there is no pending page-turn command, background frame-cache work starts only after the visible display has gone idle and the 120 ms idle delay has elapsed.
- If there is a pending page-turn command, the idle delay and work cooldown are bypassed.
- Pending turns execute as soon as the requested target page frame cache is ready. They no longer wait for both adjacent page caches.
- If a background frame-cache job is already rendering a non-target page and the user queues a pending page turn, that job is aborted and the pending target page is warmed first.
- Only one pending page-turn command exists at a time. Later swipes are consumed until the pending command executes.

Expected LOG markers:

```text
[ERS] Frame cache warm pending-priority: dir=... cur=... target=... cooldownBypassed=1
[ERS] Frame cache job retargeted for pending turn: oldPage=... targetPage=...
[ERS] Executing queued page turn: dir=... queuedFor=...
```

## Band-scan first-pass tuning

The row-major page-turn waveform now has separate knobs for the first pass and for the same-color reinforcement pulses.

Defaults keep old behavior:

```cpp
#define EPD_PAGE_TURN_FIRST_PASS_DELAY_MS EPD_PAGE_TURN_PASS_DELAY_MS
#define EPD_PAGE_TURN_ENABLE_WHITE_STAY_LIGHTER 1
#define EPD_PAGE_TURN_ENABLE_BLACK_STAY_DARKER 1
#define EPD_PAGE_TURN_WHITE_STAY_LIGHTER_PASSES 1
#define EPD_PAGE_TURN_BLACK_STAY_DARKER_PASSES 1
```

Meaning:

- `EPD_PAGE_TURN_FIRST_PASS_DELAY_MS`
  - Delay after pass 0 only.
  - Other passes still use `EPD_PAGE_TURN_PASS_DELAY_MS`.
- `EPD_PAGE_TURN_ENABLE_WHITE_STAY_LIGHTER`
  - Enables/disables the lighter cleanup pulse for white -> white pixels.
- `EPD_PAGE_TURN_ENABLE_BLACK_STAY_DARKER`
  - Enables/disables the darker reinforcement pulse for black -> black pixels.
- `EPD_PAGE_TURN_WHITE_STAY_LIGHTER_PASSES`
  - Number of early passes used for white -> white cleanup when enabled.
- `EPD_PAGE_TURN_BLACK_STAY_DARKER_PASSES`
  - Number of early passes used for black -> black reinforcement when enabled.

For quick tests, uncomment or add build flags in `platformio.ini`, for example:

```ini
  -DEPD_PAGE_TURN_FIRST_PASS_DELAY_MS=4
  -DEPD_PAGE_TURN_ENABLE_WHITE_STAY_LIGHTER=0
  -DEPD_PAGE_TURN_ENABLE_BLACK_STAY_DARKER=1
```
