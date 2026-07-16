# R30 Comic input reliability

R30 addresses three input problems seen in `device-monitor-260707-141426.log`.

## Observations

- CBZ cache hits are fast: decoded frame cache display is logged around 24-25 ms before the EPD waveform starts.
- Hidden preload still takes around 2.2-2.5 seconds because it performs ZIP copy plus JPEG decode.
- ComicReaderMenu repeatedly received footer-mode content taps (`btn=-1`) but the menu did not activate, making it feel like missed input.
- ComicFileBrowser received footer Back taps (`btn=0`) but logical Back did not always run.

## Changes

- ComicReaderMenu does not enter low-power 80 MHz mode.
- ComicReaderMenu content tap now activates the tapped row immediately.
- ComicFileBrowser explicitly treats the bottom-left footer quarter as Back even if logical button remapping disagrees.
- CBZ preload now waits for 5 seconds of idle time after visible display, so next-page taps are handled by the reader instead of being lost during preload.
