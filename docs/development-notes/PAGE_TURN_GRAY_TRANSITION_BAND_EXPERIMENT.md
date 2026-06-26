# Page Turn 4-Level Gray Transition Band Experiment (v15)

This experimental reader page-turn path keeps the band-scan idea, but changes the transition scheduler from monochrome-only to 4-level grayscale.

## Scope

Only `PAGE_TURN_REFRESH` / reader page turns use this path.
Normal UI screens such as Home, File Browser, Settings, Reader Menu, and sleep image rendering are not changed.

## Pixel model

Packed 2bpp pixel values are interpreted as:

- `0`: white
- `1`: light gray
- `2`: dark gray
- `3`: black

Each physical pixel compares the previous packed screen state with the new target frame.
The normal 64-pixel chunk darker/lighter scheduler is bypassed for this experiment.

## Transition rule

For each pixel:

- target > previous: apply black drive
- target < previous: apply white drive
- target == previous: neutral

The number of drive passes is proportional to the gray-level distance.
With the default full-range counts:

```cpp
#define EPD_PAGE_TURN_BLACK_PASS_COUNT 6
#define EPD_PAGE_TURN_WHITE_PASS_COUNT 6
```

The default mapping is:

| Transition | Pass count |
|---|---:|
| 0 -> 1 | 2 black passes |
| 0 -> 2 | 4 black passes |
| 0 -> 3 | 6 black passes |
| 1 -> 2 | 2 black passes |
| 1 -> 3 | 4 black passes |
| 2 -> 3 | 2 black passes |
| 3 -> 2 | 2 white passes |
| 3 -> 1 | 4 white passes |
| 3 -> 0 | 6 white passes |

## End-aligned timing

The band pass count is:

```cpp
max(EPD_PAGE_TURN_BLACK_PASS_COUNT, EPD_PAGE_TURN_WHITE_PASS_COUNT)
```

Each pixel starts late enough that it finishes at the same final pass. For example, if max count is 6:

- 0 -> 3 runs for all 6 passes
- 0 -> 2 waits for 2 passes, then runs for 4 passes
- 0 -> 1 waits for 4 passes, then runs for 2 passes

This follows the "do not over-drive" principle: pixels are only driven for their transition distance.

## Current defaults

```cpp
#define EPD_PAGE_TURN_BAND_ROWS 540
#define EPD_PAGE_TURN_BLACK_PASS_COUNT 6
#define EPD_PAGE_TURN_WHITE_PASS_COUNT 6
#define EPD_PAGE_TURN_PASS_DELAY_MS 9
#define EPD_PAGE_TURN_BLACK_DRIVE 0x55
#define EPD_PAGE_TURN_WHITE_DRIVE 0xAA
```

PlatformIO upload speed is set to:

```ini
upload_speed = 1500000
```
