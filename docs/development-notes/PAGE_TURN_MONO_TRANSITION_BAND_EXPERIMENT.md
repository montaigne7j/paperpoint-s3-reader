# Page-turn monochrome transition band experiment (v14)

This is a reader-page-turn-only experiment based on the v13 mono-band path.

## Purpose

The goal is to keep the visible band-scan feeling while avoiding unnecessary drive pulses.
For text pages, the page is treated as monochrome:

- packed `00` = white
- packed `01`, `10`, `11` = black

Unlike v13, this version compares the previous screen state with the new target frame.

## Transition rules

For each physical pixel/column:

- previous white -> target black: apply black drive
- previous black -> target white: apply white drive
- unchanged white -> white: neutral
- unchanged black -> black: neutral

This is per-pixel transition scheduling. It does not use the normal 64-pixel chunk darker/lighter scheduler in the page-turn experiment path.

## Pass-count alignment

Black and white pulse counts are separated:

```cpp
#define EPD_PAGE_TURN_BLACK_PASS_COUNT 8
#define EPD_PAGE_TURN_WHITE_PASS_COUNT 13
```

The total pass count is:

```cpp
max(BLACK_PASS_COUNT, WHITE_PASS_COUNT)
```

Pulse windows are end-aligned. Example: if white count is 13 and black count is 8:

- black->white pixels receive white drive on passes 0..12
- white->black pixels stay neutral on passes 0..4
- white->black pixels receive black drive on passes 5..12

Both transition types stop at the same time.

## Tunable parameters

All parameters are near the top of:

```text
lib/EPD_Painter/EPD_Painter.cpp
```

```cpp
#define EPD_PAGE_TURN_BAND_ROWS 6
#define EPD_PAGE_TURN_BLACK_PASS_COUNT 8
#define EPD_PAGE_TURN_WHITE_PASS_COUNT 13
#define EPD_PAGE_TURN_PASS_DELAY_MS 1
#define EPD_PAGE_TURN_BLACK_DRIVE 0x55
#define EPD_PAGE_TURN_WHITE_DRIVE 0xAA
```

If black and white are reversed on real hardware, swap the drive values:

```cpp
#define EPD_PAGE_TURN_BLACK_DRIVE 0xAA
#define EPD_PAGE_TURN_WHITE_DRIVE 0x55
```

## Scope

This affects only `PAGE_TURN_REFRESH`, which is used by reader page turns.
Home, Settings, File Browser, Reader Menu, and other normal UI screens keep the standard painter path.
