# Page Render Profiling Lite (v28)

This build keeps the v26/v27 reader behavior and changes only the profiling output.

## Why v28 exists

The v27 profiling build printed too much detail, especially per-word/per-glyph slow logs. At 115200 baud this can become a visible bottleneck and can make touch input feel delayed.

v28 changes profiling to summary-only logs:

- foreground `page->render()` only
- one `TXB` summary per TextBlock
- one `PGE` summary per PageElement
- one final `PGE` page summary
- no per-word or per-glyph logs
- background cooperative frame-cache warm is not profiled

## Log tags

### `ERS`

High-level reader render timing:

```text
[ERS] render phase: background=... beginContent=... modeSet=... pageRender=... endContent=...
[ERS] render phase: verticalTest=... statusBar=...
[ERS] render phase: cacheStore=...
[ERS] Page render: prewarm=... render=... display=... total=...
```

### `PGE`

Page-level / element-level summary:

```text
[PGE] page start: elements=10 font=... offset=(...,...)
[PGE] element 4/10 text layout=vertical entries=95 logical=12 bytes=285 glyphs=95 pos=(...) time=133ms
[PGE] element 5/10 image size=... pos=(...) time=...
[PGE] page done: elements=10 total=885ms textTotal=885ms imageTotal=0ms slowestIndex=4 slowestTag=1 slowest=133ms
```

Meaning:

- `entries`: stored TextBlock entries. In vertical layout this is usually glyph entries. In horizontal layout this is usually word entries.
- `logical`: original parser word count, used for footnote/anchor tracking.
- `bytes`: UTF-8 byte count in the TextBlock.
- `glyphs`: estimated UTF-8 codepoint count.
- `time`: total time spent rendering that PageElement.

### `TXB`

TextBlock-level summary:

```text
[TXB] summary layout=vertical entries=95 logical=12 bytes=285 glyphs=95 draw=129ms underline=0ms total=130ms avgDraw=1ms slowestIndex=32 slowest=6ms
```

Meaning:

- `draw`: accumulated time in `renderer.drawText()` / `renderer.drawVerticalText()` calls.
- `underline`: underline handling time for horizontal text.
- `total`: whole TextBlock render time.
- `avgDraw`: draw time divided by entries.
- `slowestIndex` / `slowest`: slowest entry inside this TextBlock. This does not print the text itself to avoid serial overhead.

## How to read the results

For a slow cache-miss page, first check:

```text
[ERS] render phase: ... pageRender=...
[PGE] page done: ... total=...
```

Then locate the slowest element:

```text
[PGE] element N/M ... time=...
```

Then compare with its `TXB` summary:

```text
[TXB] summary ... draw=... total=... slowest=...
```

If `TXB draw` is close to `TXB total`, the bottleneck is glyph/text drawing.
If `PGE element time` is much larger than its `TXB total`, the bottleneck may be outside TextBlock rendering.
If `imageTotal` is large, image decoding/rendering is the bottleneck.

## Notes

This is still a diagnostic build. It is lighter than v27, but it still prints more than a normal release build.
