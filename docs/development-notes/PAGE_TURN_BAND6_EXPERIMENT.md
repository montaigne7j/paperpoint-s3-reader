# Page Turn Band-6 Experiment

This is an experimental Paper S3 page-turn build based on the v11 row-major scan test.

## What changed

Reader page turns still call `EPD_Painter::paintRowMajor()`, but the implementation is now a 6-row band-major experiment instead of a single-row experiment.

The normal painter is pass-major:

```text
for each waveform pass:
  scan row 0 → row 539
```

v11 row-major was:

```text
for each target row:
  apply pass 0 → pass 12 to that target row
  then move to the next target row
```

v12 band-6 is:

```text
for each 6-row band:
  apply pass 0 → pass 12 to rows in that band
  then move to the next 6-row band
```

## Test settings

- Band size: 6 physical rows
- Page-turn waveform data: `EPD_Painter::QUALITY_HIGH`
- Paint stages: 1 stage only
- Inter-pass delay in the band-major path: 1 ms
- Scope: reader page turns only

## Purpose

This test checks whether a small completed band can keep the strong scan feeling seen in v11 while reducing the extremely slow speed caused by giving every single row a full independent waveform sequence.

## Important limitation

The Paper S3 gate driver is still sequential.  For every band and pass, the experiment resets at row 0, clocks neutral rows up to the band, drives the active 6-row band, then stops at the band end.

This is still expected to be slower than the v6 pass-major page turn.  It is meant to compare scan feeling, speed, and ghosting, not as a final reading mode.

## Scope

Changed:

- Reader page turns via `HalDisplay::PAGE_TURN_REFRESH`

Not changed:

- Home
- File browser
- Settings
- Reader menu
- Sleep image
- Non-reader UI refresh paths
