# Page Turn Soft Gray Band Scan Experiment v14

This build is a reader-page-turn-only experiment for M5Stack Paper S3.

It is based on the v13 mono-band scan path, but changes the page-turn drive model to reduce over-thick black text and black bleed into neighboring white pixels.

## Scope

Only `HalDisplay::PAGE_TURN_REFRESH` / reader page turns use this path. Normal UI screens such as Home, File Browser, Settings, Reader Menu, and sleep image rendering continue to use the normal painter path.

## Core behavior

The experimental page-turn path:

1. Ignores the previous screen buffer.
2. Looks only at the current target frame.
3. Uses 6-row band-major scan by default.
4. Uses one paint stage.
5. Does not use the normal 64-pixel chunk darker/lighter scheduler.
6. Chooses drive per physical pixel/column from the target 2bpp value.

## Default parameters

Located in `lib/EPD_Painter/EPD_Painter.cpp`:

```cpp
#define EPD_PAGE_TURN_BAND_ROWS 6
#define EPD_PAGE_TURN_PASS_COUNT 8
#define EPD_PAGE_TURN_PASS_DELAY_MS 1
#define EPD_PAGE_TURN_BLACK_DRIVE 0x55
#define EPD_PAGE_TURN_WHITE_DRIVE 0xAA
#define EPD_PAGE_TURN_SPECIAL_DRIVE 0xFF
```

`EPD_PAGE_TURN_PASS_DELAY_MS` is the main delay parameter for this experiment.

## Default 8-pass drive schedule

Target pixel values are packed 2bpp:

| Target value | Meaning | Drive schedule |
|---|---|---|
| `00` | White | lighter × 5, special × 1, neutral × 2 |
| `01` | Gray 1 | lighter × 5, darker × 1, special × 2 |
| `10` | Gray 2 | lighter × 5, darker × 3 |
| `11` | Black | darker × 5, special × 3 |

This intentionally avoids driving black pixels with continuous black drive for all passes. The purpose is to test whether softer black formation reduces thick black strokes and severe ghosting on the next update.

## Notes

- `0x55` is used as darker drive.
- `0xAA` is used as lighter drive.
- `0xFF` is used as the special `3` drive state.
- `0x00` is neutral.

If black/white direction appears inverted on hardware, swap `EPD_PAGE_TURN_BLACK_DRIVE` and `EPD_PAGE_TURN_WHITE_DRIVE` for the next experiment.
