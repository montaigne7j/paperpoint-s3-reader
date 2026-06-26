# Page Frame Cache Idle-Safe Experiment (v20)

This experiment keeps the four-slot reader framebuffer cache but makes cache warming less aggressive so input always has priority.

## Cache slots

The cache still has four independent framebuffer slots.  Slots are located by metadata and are not shifted or copied on each page turn:

- `spineIndex`
- `pageNumber`
- screen width / height

The intended active window is:

1. current page
2. previous page
3. next page
4. next-next page

## Cache warming policy

Cache warming is opportunistic idle work only.

- Input handling happens before cache warming.
- A page-frame cache miss renders immediately through the normal reader path.
- A page that already exists in cache is not rendered again.
- Cache warming waits for an idle window before starting.
- Cache warming also has a cooldown between background renders, preventing it from monopolizing the reader loop.

Current warm priority when a slot is missing:

1. next page
2. current page
3. next-next page
4. previous page

## Tuned constants

The page-turn waveform test constants are now:

```cpp
#ifndef EPD_PAGE_TURN_BAND_ROWS
#define EPD_PAGE_TURN_BAND_ROWS 560
#endif

#ifndef EPD_PAGE_TURN_PASS_COUNT
#define EPD_PAGE_TURN_PASS_COUNT 8
#endif

#ifndef EPD_PAGE_TURN_PASS_DELAY_MS
#define EPD_PAGE_TURN_PASS_DELAY_MS 8
#endif
```

A band height of 560 covers the full Paper S3 physical height, so this behaves like a whole-frame band while keeping the experimental path and parameters easy to change.

## Full refresh setting

Reader full-refresh interval now supports a `Never` / `不全刷` option, and the default is no scheduled full refresh.
