# Built-in User Manual EPUB

This build embeds a small EPUB manual in firmware.

When the File Browser opens `/book`, the firmware checks whether the manual exists at:

```text
/book/CrossPoint_User_Manual.epub
```

If the file is missing, or if its size does not match the firmware copy, it is written to SD storage automatically. It then appears in the File Browser together with the user's books and opens through the normal EPUB reader path.

The manual content is bilingual:

1. Traditional Chinese first.
2. English second.

It covers home/menu operation, settings, direct touch selection, footer button behavior, and reading page operation.

## V1.8.4 status

The manual is still embedded in firmware as `src/resources/BuiltinManualEpub.cpp` and is installed automatically to `/book/CrossPoint_User_Manual.epub` on the SD card.  A copy of the generated EPUB is also kept at `docs/CrossPoint_User_Manual.epub` for release review.

The release documentation covers page-turn caching, TTF memory protection, CBZ/ZIP comics, custom sleep images, reader backgrounds, BMI270 orientation calibration, battery display smoothing, and file-browser parent navigation. The generated EPUB copy remains available at `docs/CrossPoint_User_Manual.epub` for release review.


## V1.8.4 按鍵與觸控診斷

V1.8.4 主要改善偶發「按鍵沒反應」的可診斷性。Reader 會在 release serial log 中記錄 touch / swipe / page-turn queue 狀態，並統計 `touchDetected`、`inputQueued`、`inputIgnoredBusy`、`inputIgnoredPending`、`inputExecuted`。

若 render 或 background frame cache 正忙，reader 會保留 1 個 pending page-turn；若後續又快速連按，會記錄為 pending ignore，避免無法判斷事件是否被觸控硬體收到。

若 display idle、render busy 或 pending page-turn 等待超過 500 ms / 1000 ms，也會輸出 warning 型態的 INF log，方便定位體感卡頓。

## V1.8.4 外部字型與導覽調整

- 外部字型資料夾支援 `/font` 與 `/fonts`，同名檔案以 `/font` 優先。
- `.bin` / `.epdf` 檔名支援中文、空格、符號，以及 `23x30` / `23×33` 尺寸寫法。
- 外部 legacy `.bin` reader glyph 會跟隨閱讀字級縮放。
- 閱讀字距可調為負值，行距範圍加大。
- File Browser 非根目錄第一列為 `..`；短按返回上一層，在 `/` 才回首頁，長按返回可直接回首頁。
- 章節選單 footer 使用「離開 / 上層 / 前頁 / 後頁」，不存在的操作會隱藏。


## V1.8.4 reader background and guide lines

Place decorative PNG files in `/bg` on the SD card. Enable Reader background PNG from Settings to render the first visible PNG behind EPUB text. Optional guide lines can be set to Off, Solid, Dashed, or Dotted. Horizontal reading uses baselines below rows; vertical reading uses column guides to the left of text.

## V1.8.4 reader background picker and guide lines

Place reader background PNG files under `/bg`. The selector supports files directly inside `/bg` and one nested level, for example `/bg/ink/reading.png`. Open Settings > Display > Reader background PNG, then choose a file or `None`.

Guide lines are drawn from the actual reader row/column positions. Horizontal layout draws a line below each text row; vertical layout draws a line to the left of each text column. This also keeps guide lines visible when the page margin is increased to 40 px.

Custom sleep images are scanned from `/.sleep`, `/cover`, and `/sleep`; root legacy files such as `/sleep.png` are still supported.

## V1.8.4 r34i release additions

- Four-pose BMI270 calibration is required before automatic orientation or sensor lock is used.
- Reader orientation supports fixed normal, fixed 180 degrees, and automatic 0/180 degrees.
- Comic `.cbz` / `.zip` reading includes natural sorting, preload, pending page turns, and configurable full-refresh intervals.
- Battery display uses USB detect and smoothed ADC tracking.
- The reader has an invisible 64x64 top-left power-off hotspot.
- File Browser uses `..`, short Back for parent navigation, and long Back for Home.
