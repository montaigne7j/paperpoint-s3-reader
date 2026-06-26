# Page Turn Row-Major Experiment

This is an experimental Paper S3 page-turn build.

## What changed

Reader page turns now call `EPD_Painter::paintRowMajor()` instead of the normal `paint()` path.

The normal painter is pass-major:

```text
for each waveform pass:
  scan row 0 → row 539
```

This experiment is target-row-major:

```text
for each target row:
  apply pass 0 → pass 12 to that target row
  then move to the next target row
```

## Important limitation

The Paper S3 gate driver is sequential.  It cannot directly jump to an arbitrary row.  To drive a target row repeatedly, the experiment resets at row 0 and clocks neutral rows until the target row, then applies the active waveform to the target row.

Because of this, the build is expected to be very slow.  It is meant only to validate scan direction and ghosting behavior, not as a final reading mode.

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
