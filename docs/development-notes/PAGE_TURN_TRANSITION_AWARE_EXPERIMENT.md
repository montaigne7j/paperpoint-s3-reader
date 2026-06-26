# Page Turn Transition-Aware Band Scan Experiment (v17)

This experimental page-turn path is based on the v14/v16 soft-gray band scan path.
It is used only by `PAGE_TURN_REFRESH` / reader page turns.

## Purpose

v16 used a previous-frame override that depended only on the previous pixel value.
That made new black text too pale and made old black text harder to erase.

v17 changes the rule to look at the transition:

```cpp
if (prev == 0x00 && curr == 0x00) {
  // white stays white: lighter once, then neutral
}
else if (prev == 0x00 && curr != 0x00) {
  // white becomes gray/black: use current-frame schedule
}
else if (prev == 0x03 && curr == 0x00) {
  // black becomes white: multiple lighter pulses for ghost cleanup
}
else if (prev == 0x03 && curr == 0x03) {
  // black stays black: darker once, then neutral
}
else {
  // gray-related transitions: use current-frame schedule
}
```

## Default tunables

Defined in `lib/EPD_Painter/EPD_Painter.cpp`:

```cpp
#define EPD_PAGE_TURN_BAND_ROWS 6
#define EPD_PAGE_TURN_PASS_COUNT 8
#define EPD_PAGE_TURN_PASS_DELAY_MS 1
#define EPD_PAGE_TURN_WHITE_STAY_LIGHTER_PASSES 1
#define EPD_PAGE_TURN_BLACK_STAY_DARKER_PASSES 1
#define EPD_PAGE_TURN_BLACK_TO_WHITE_LIGHTER_PASSES 5
```

Drive values:

```cpp
#define EPD_PAGE_TURN_BLACK_DRIVE 0x55
#define EPD_PAGE_TURN_WHITE_DRIVE 0xAA
#define EPD_PAGE_TURN_SPECIAL_DRIVE 0xFF
```

## Current-frame schedule

Used for white->non-white and gray-related transitions:

```cpp
white : lighter x5, special x1, neutral x2
gray1 : lighter x5, darker  x1, neutral x2
gray2 : lighter x5, darker  x2, neutral x1
black : darker  x5, special x1, neutral x2
```

## Expected behavior

- New text from white background should no longer be suppressed.
- Old black text that becomes white gets a stronger lighter cleanup path.
- Existing black text receives only one darker pulse to reduce over-darkening / black spread.
- White background that stays white receives only one lighter pulse to avoid unnecessary panel drive.
