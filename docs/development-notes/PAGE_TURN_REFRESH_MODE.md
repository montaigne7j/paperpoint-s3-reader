# Page Turn Refresh Mode

This build adds a reader-only page-turn refresh path for M5Stack Paper S3.

## Goal

Keep the visible portrait-mode scan-in effect while reducing the strong ghosting seen with the pure fast waveform.

## Behavior

- Normal UI screens keep the existing conservative refresh behavior.
- Reader page turns use `HalDisplay::PAGE_TURN_REFRESH`.
- `PAGE_TURN_REFRESH` sends the complete new framebuffer once.
- The Paper S3 physical row scan still creates the right-to-left sweep impression in portrait orientation.
- The refresh quality is `EPD_Painter::QUALITY_NORMAL`, not `QUALITY_FAST`, to reduce ghosting.
- Existing periodic full refresh logic is preserved through `SETTINGS.getRefreshFrequency()`.

## Why not QUALITY_FAST?

`QUALITY_FAST` gave a clear scan-in feeling, but left strong ghost traces.  `QUALITY_NORMAL` uses all 13 waveform passes with shorter delay than high quality.  It is slower than pure fast mode, but should retain most of the scan-in feeling while improving ink settling.

## Affected reader paths

- EPUB page turns
- TXT page turns
- XTC page turns
- EPUB image + anti-aliased text final page paint

## Not affected

- Home screen
- File browser
- Settings
- Reader menu
- Chapter selection
- Sleep image rendering
## v10 high-quality direction test

This test build intentionally uses the v6 single full-frame page-turn path, but changes `PAGE_TURN_REFRESH` to `EPD_Painter::Quality::QUALITY_HIGH`.

Expected behavior:

- Cleaner ink drive than v6 `QUALITY_NORMAL`.
- Slower page turn than v6.
- No segmented/striped refresh, so it should not create the v7/v8 broken-text strip artifacts.
- Use it to check whether the native scan direction is still visually recognizable with HIGH quality.



## v11 row-major HIGH experiment

This test build changes reader page turns to use an experimental row-major painter.

Normal EPD_Painter behavior is pass-major:

```text
pass 0: row 0 → row 539
pass 1: row 0 → row 539
...
```

The v11 experiment is target-row-major at the visible level:

```text
row 0: pass 0 → pass 12
row 1: pass 0 → pass 12
...
row 539: pass 0 → pass 12
```

Paper S3 row addressing is sequential, so the firmware resets at row 0 and clocks neutral rows until the target row before driving that target row.  This is expected to be dramatically slower than the normal v6/v10 path.

Purpose of this build:

- Verify whether a completed row-by-row waveform creates the expected scan direction.
- Observe whether row-major driving causes stronger or weaker ghosting than v6/v10.
- Keep the experiment isolated to reader page turns; menus and other UI screens are not changed.
