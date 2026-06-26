# Changelog

## 1.7.0 — 2026-06-27 — Stable Paper S3 page turn cache and waveform profile

- 定版 Paper S3 reader page-turn flow：visible page 顯示完成後等待 EPD idle，再短延遲啟動背景鄰頁 framebuffer cache。
- 翻頁輸入改為單一 pending 指令；pending 執行前不接受新的翻頁，避免連續 swipe 累積造成跳頁或頁碼狀態混亂。
- 有 pending 翻頁時會跳過一般背景 cache idle / cooldown，優先準備 pending 方向頁面，cache ready 後立即執行。
- cache miss 可見頁改為整頁 render 後一次 display，不再使用前景 10 段 progressive refresh。
- 定版 Paper S3 band-scan waveform 參數：band rows 540、pass count 8、first pass 1ms、normal pass 5ms、black/white drive 0x55/0xAA、special drive 0xFF、black-to-white lighter passes 5。
- UI 回內文、圖片頁轉內文等情境會 request full refresh，降低跨畫面殘影。
- Home screen 移除無作用的左上角電源熱區，改成可見的 `Power Off` / `關機` 選單項目。
- Fixed Home Power Off handling to request real deep sleep from the main loop, avoiding recursive ActivityManager loop calls and loopTask stack overflow.
- Added and corrected the Home Power Off icon for Lyra/Lyra 3 Covers themes, including Paper S3 portrait orientation.
- Lyra Extended home screen: fixed touch hit-testing for the second and third recent-book cards.
- Moved the Lyra 3-covers Home framebuffer snapshot buffer to PSRAM to reduce internal heap pressure.
- Hardened EPUB progress restore/save guards so invalid saved spine/page values cannot reopen directly at End of Book.
- Added Home recent-book selection logging to make future hit-test issues easier to diagnose.
- 新增可調 page-turn waveform compile-time options：第一 pass delay、白→白 lighter pass 開關、黑→黑 darker pass 開關，以及相關 pass 數。
- 清理 GitHub 發布用 source package：移除本機 `.vscode/`、過時 `.gitmodules`、AI agent local guide、舊工作目錄與可由 build script 產生的檔案。

## V1.6.0 foreground grouped progressive render v32

- Fix: gestures detected while a foreground progressive cache-miss render is active are now drained/ignored so they cannot become delayed page turns and skip a page.
- Change: vertical progressive reveal direction is reversed from v31: swipe-left previous reveals from the right side first; swipe-right next reveals from the left side first.
- Kept: foreground cache miss only, grouped refresh every two PageElements, calculated physical row ranges per group, final full consistency refresh, display-idle wait before cache store, and internal heap logs.


## V1.6.0 foreground grouped progressive render v31

- Grouped foreground progressive render: every 2 PageElements trigger one row-range refresh.
- Progressive path remains cache-miss only; cache hits keep the fast framebuffer path.
- Added display-idle wait before framebuffer cache store.
- Added internal heap diagnostics at progressive start / before cache store / done.
- Changed vertical-reading swipe mapping: swipe right = next page, swipe left = previous page.


## V1.6.0-page-render-element-progressive-v30

- Changed foreground cache-miss progressive render to render `PageElement`s directly instead of wrapping the full-page `page->render()` path.
- After each text element/vertical column is rendered, only that element's physical row range is refreshed.
- Vertical page-turn order follows reading direction: next page right-to-left, previous page left-to-right.
- Added `PRG` logs for element-level draw and row-range refresh timings.
- Cache hits, image pages, and horizontal pages keep the existing render path.


## v29 - Foreground Progressive Render Experiment

- Added an experimental cache-miss foreground progressive render path for Paper S3 vertical reader pages.
- Text-only vertical pages in Band-scan mode are split into 10 physical row stripes.
- Each stripe is refreshed as soon as its first intersecting text elements are rendered.
- Added low-level physical row-range display support in EPD_Painter, HalDisplay and GfxRenderer.
- Cache hits, image pages, horizontal layout and original refresh mode keep the existing render path.


## v28 - Page render profiling lite

- Reduced page render profiling from per-word/per-glyph logs to summary-only logs.
- Added `PageRenderProfiler` scoped switch so detailed `PGE`/`TXB` logs run only during foreground `page->render()`.
- Background cooperative frame-cache warm no longer emits TextBlock profiling logs.
- Added `PAGE_RENDER_PROFILING_LITE.md`.


## V1.6.0 page-render-profiling v27

- Added detailed `Page::render()` profiling logs (`PGE`) for element count, element type, per-element render time, cumulative time, and slowest element.
- Added `TextBlock::render()` profiling logs (`TXB`) for layout mode, word/glyph count, draw time, slow word/glyph count, and slowest word/glyph.
- Added `EpubReaderActivity::renderContents()` phase logs (`ERS`) for background preparation, content render wrapper, page render, status bar, cache store, and display breakdown.
- Added `PAGE_RENDER_PROFILING.md` with log reading notes.


## V1.6.0 page-turn modes invert UI v26 compile fix

- Fixed `EpubReaderMenuActivity.cpp` LARGE_TEXT render build error by defining `pageItems` from the large-text list height and row height before deriving footer page navigation hints.
- No behavior changes from v25.


## v25 - Page turn mode, invert reader, and UI footer fixes

- Added Controls setting for Page Turn Refresh Mode: Original Refresh Mode / Band-scan Mode.
- Band-scan page-turn direction now follows reading layout and previous/next direction.
- Fixed inverted reader content background so black reaches the screen edges.
- Status bar now follows reader content inversion.
- UI footer Previous/Next page buttons are hidden when the current list has no previous/next page.


## v23 - Horizontal page-turn gesture

- Added reader horizontal swipe gestures.
- Horizontal reading layout: swipe right = next page, swipe left = previous page.
- Vertical reading layout: swipe left = next page, swipe right = previous page.
- Horizontal gesture fires while the finger is still down once the threshold is reached; it does not wait for finger release.
- Added `BTN_SWIPE_LEFT` and `BTN_SWIPE_RIGHT` virtual buttons.
- Suppresses tap classification on release after a horizontal swipe, preventing accidental reader menu opening.
- Keeps v22 cooperative framebuffer cache behavior unchanged.


## V1.6.0 page-frame-cache-cooperative-v22 compile fix

- Fixed build error caused by `std::unique_ptr<Page>` in `EpubReaderActivity.h` seeing `Page` only as an incomplete type in translation units such as `ReaderActivity.cpp`.
- Added `#include <Epub/Page.h>` to `EpubReaderActivity.h` so the default `unique_ptr<Page>` deleter can see the complete `Page` definition.
- No behavioral changes from v21 cooperative cache warm.


## v21 - Page frame cache cooperative warm experiment

- Split opportunistic page-frame cache warm into cooperative chunks instead of rendering a whole background page in one blocking step.
- Added input checks between cache chunks; any touch/button input aborts the partial cache warm job so the page turn path can run first.
- Kept four-slot framebuffer cache with metadata lookup by `spineIndex + pageNumber`; cache slots are not shifted or copied during page turns.
- Kept cache-miss behavior as immediate visible-page render, not waiting for a background cache job.
- Added `PAGE_FRAME_CACHE_COOPERATIVE.md`.


## v20 Page Frame Cache Idle-Safe Experiment

- Made reader page-frame cache warming idle-safe: input is checked before cache work, cache warm waits for an idle window, and cache warm has a cooldown between background renders.
- Kept four metadata-addressed framebuffer cache slots; slots are not shifted or copied on page turns.
- Cache warm priority is now next page, current page, next-next page, previous page, skipping pages that are already cached.
- Cache misses still render synchronously through the normal reader path instead of waiting for cache warm.
- Updated page-turn experimental parameters: band rows 560, pass count 8, pass delay 8 ms.
- Increased PlatformIO upload speed to 1500000.
- Added a reader full-refresh interval option for no scheduled full refresh (`Never` / `不全刷`) and made it the default.


## Page framebuffer cache priority v19

- Adjusted the Paper S3 reader framebuffer cache warmer priority.
- Missing pages are now warmed in this order: next page, current page, next-next page, previous page.
- Existing cached pages are skipped; current/previous are not rendered again if their slot metadata already matches.
- Cache slots continue to be metadata-addressed; framebuffers are not shifted or moved on page turns.


## v18 - EPUB Page Framebuffer Cache Experiment

- Added a four-slot EPUB reader framebuffer cache: current, previous, next, and next-next page.
- Cache slots are selected by metadata (`spineIndex` + `pageNumber`) and are not shifted when turning pages.
- Idle reader loop opportunistically pre-renders next and next-next pages into PSRAM-backed framebuffers.
- Cache hits copy the cached framebuffer directly to the renderer and immediately enter the normal reader refresh cycle.
- Disabled the ineffective font prewarm pass for this experiment.
- Added `PAGE_FRAME_CACHE_EXPERIMENT.md`.


## Page Turn Transition-Aware Band Scan Experiment (v17)

- Updated the page-turn band scan experiment from previous-only override to previous->current transition-aware rules.
- White->white now uses one lighter pulse then neutral.
- White->gray/black now uses the current-frame schedule so new text is not suppressed.
- Black->white now uses multiple lighter pulses for old-text ghost cleanup.
- Black->black now uses one darker pulse then neutral to reduce over-darkening and black spread.
- Added `PAGE_TURN_TRANSITION_AWARE_EXPERIMENT.md`.

## v16 - Page Turn Previous Override Experiment

- Based on `v14 - Page Turn Soft Gray Band Scan Experiment`.
- Kept the reader-page-turn-only 6-row band scan and per-pixel/column drive generation.
- Added the requested previous-frame override before the current-frame schedule:
  - Previous `00` white: lighter × 1, neutral × 7.
  - Previous `11` black: darker × 1, neutral × 7.
  - Previous `01` / `10` gray: keep the current-frame schedule.
- Updated the current-frame schedule for previous-gray pixels:
  - Current `00` white: lighter × 5, special × 1, neutral × 2.
  - Current `01` gray1: lighter × 5, darker × 1, neutral × 2.
  - Current `10` gray2: lighter × 5, darker × 2, neutral × 1.
  - Current `11` black: darker × 5, special × 1, neutral × 2.
- Added `PAGE_TURN_PREVIOUS_OVERRIDE_EXPERIMENT.md`.


## v14 - Page Turn Soft Gray Band Scan Experiment

- Added a reader-page-turn-only soft grayscale current-frame band scan experiment.
- The page-turn path still ignores the previous screen state, but no longer treats all non-white pixels as black.
- Each physical pixel/column chooses its drive from the current target frame value, without the normal 64-pixel chunk darker/lighter scheduler.
- Default page-turn band parameters in `lib/EPD_Painter/EPD_Painter.cpp`:
  - `EPD_PAGE_TURN_BAND_ROWS` = 6
  - `EPD_PAGE_TURN_PASS_COUNT` = 8
  - `EPD_PAGE_TURN_PASS_DELAY_MS` = 1
  - `EPD_PAGE_TURN_BLACK_DRIVE` = `0x55`
  - `EPD_PAGE_TURN_WHITE_DRIVE` = `0xAA`
  - `EPD_PAGE_TURN_SPECIAL_DRIVE` = `0xFF`
- Default 8-pass current-frame drive schedule:
  - White `00`: lighter × 5, special × 1, neutral × 2
  - Gray 1 `01`: lighter × 5, darker × 1, special × 2
  - Gray 2 `10`: lighter × 5, darker × 3
  - Black `11`: darker × 5, special × 3
- Purpose: test whether reducing continuous black drive and adding soft/special phases reduces over-thick black text and severe next-page ghosting.
- Added `PAGE_TURN_SOFT_GRAY_BAND_EXPERIMENT.md`.


## v13 - Page Turn Monochrome Band Scan Experiment

- Added a reader-page-turn-only monochrome current-frame band scan experiment.
- The page-turn path now ignores the previous screen state and treats the current target frame as black/white text.
- Each physical pixel/column directly chooses black drive or white drive from the current target frame; the page-turn path no longer uses the normal 64-pixel chunk darker/lighter scheduler.
- Added tunable constants in `lib/EPD_Painter/EPD_Painter.cpp`:
  - `EPD_PAGE_TURN_BAND_ROWS` = 6
  - `EPD_PAGE_TURN_PASS_COUNT` = 13
  - `EPD_PAGE_TURN_PASS_DELAY_MS` = 1
  - `EPD_PAGE_TURN_BLACK_DRIVE` = `0x55`
  - `EPD_PAGE_TURN_WHITE_DRIVE` = `0xAA`
- Added `PAGE_TURN_MONO_BAND_EXPERIMENT.md`.

## Page Turn Refresh v12-band6-experiment

- Reworked the v11 row-major page-turn experiment into a 6-row band-major experiment.
- Reader page turns process physical rows in 6-row bands: rows 0..5 receive the full waveform sequence, then rows 6..11, and so on.
- Kept `EPD_Painter::QUALITY_HIGH` waveform data for this test.
- Reduced the experimental band-major path to one paint stage.
- Forced the band-major inter-pass delay to 1 ms instead of the previous HIGH-mode 8 ms delay.
- Purpose: validate whether a small completed-row band gives visible scan feeling with more usable speed than v11.
- Non-reader UI screens continue to use the normal painter.

## Page Turn Refresh v11-row-major-experiment

- Added an experimental `EPD_Painter::paintRowMajor()` path for reader page turns only.
- `PAGE_TURN_REFRESH` now calls `paintRowMajor(frameBuffer)` instead of normal `paint(frameBuffer)`.
- The experiment drives physical row 0 through the full waveform sequence first, then row 1, and so on.
- Uses `EPD_Painter::QUALITY_HIGH` in this test build.
- This is intentionally much slower than the normal pass-major painter and is only for validating scan-direction behavior.
- Non-reader UI screens continue to use the normal painter.

## Page Turn Refresh v10-high-test

- Test build based on the v6 page-turn path.
- Changed `HalDisplay::PAGE_TURN_REFRESH` from `QUALITY_NORMAL` to `QUALITY_HIGH`.
- Keeps a single full-frame `paint()` call: no segmented/striped refresh and no cue frames.
- Purpose: verify whether the native scan-direction feeling remains visible with a cleaner high-quality waveform.

## Page Turn Refresh v6

- Added `HalDisplay::PAGE_TURN_REFRESH` for reader page turns.
- Reader page turns now use `EPD_Painter::QUALITY_NORMAL` instead of pure fast waveform.
- This preserves the one-pass physical row-scan sweep feeling while reducing the heavy ghosting observed with `QUALITY_FAST`.
- Non-reader UI refresh behavior remains conservative and unchanged.
- EPUB image + anti-aliased text, TXT, and XTC reader paths now share the same page-turn refresh cycle.


## 1.6.0 — 2026-06-23 — Direct touch, reader typography, and firmware slimming

- Added direct-touch selection for the Home screen, Settings tabs, list-style screens, and multi-cover home layouts.
- Redesigned Paper S3 footer navigation as `Back / Select / Previous / Next`; Previous and Next now move by page instead of row.
- Added Reader Status Bar Margin Mode so the reader status bar can either stay at the bottom or follow the reading margins.
- Added a built-in bilingual EPUB user manual installed as `/book/CrossPoint_User_Manual.epub`.
- Updated the built-in Traditional Chinese reader fallback using the maintainer-supplied larger CJK raster source, with scalable reader metrics and improved vertical punctuation alignment.
- Slimmed firmware by removing embedded ReaderDyslexic families, reducing Noto Sans variants, and keeping only English hyphenation data.
- Kept Chinese-first documentation and web installer guidance aligned with the 1.6.0 browser flashing flow.

### Direct Touch Selection v4

- Added Reader Status Bar Margin Mode (`Status Bar Follows Margin` / `狀態列跟隨頁邊距`).
  - Off: status bar stays at the bottom; reading content uses `max(screen margin, status bar height)`.
  - On: status bar follows the bottom page margin; reading content uses `screen margin + status bar height`.
  - Left/right reader margins also apply to the status bar.
- Redesigned Paper S3 footer navigation as `Back / Select / Previous / Next`.
  - Previous/Next are page-level navigation actions in list-style screens.
  - Up/Down row movement is no longer used by the Paper S3 footer.
  - The Home screen hides the footer and is direct-touch driven.
- Fixed Traditional Chinese footer Back label.
  - `返回` no longer includes `<<` or `«`.
  - Chinese Large Text labels stay large; compact text is only used for long Latin labels.
- Added a built-in bilingual EPUB user manual.
  - Installed automatically as `/book/CrossPoint_User_Manual.epub` when browsing `/book`.

### Footer navigation and Settings tab layout

- Changed bottom virtual button wording from Up/Down style navigation to Back / Select / Previous / Next labels.
- Large Text theme now draws real footer labels instead of symbolic icons (`<<`, `o`, `^`, `v`).
- Large Text theme uses compact Latin labels for English footer buttons and Settings tabs, while keeping Chinese tab/footer labels large.
- Settings tab bars are now four equal-width cells in Classic, Lyra, and Large Text themes so visual tabs match direct-touch hit areas.
- Updated Traditional Chinese footer navigation labels to 前頁 / 後頁.


### Direct Touch Selection follow-up

- Added direct touch support for the Home Continue Reading card.
- Added direct touch support for the Settings category tab bar.
- Improved mixed Chinese/English UI label rendering by preferring the active UI font for printable ASCII and centering built-in Latin fallback glyphs within the UI line box.
- Hid the legacy Sleep Screen Cover Mode and Sleep Screen Cover Filter settings from the device/web settings list while retaining backward-compatible settings-file parsing.


### Larger CJK source and vertical spacing tuning

### Built-in CJK 31x39 source
- Replaced the embedded PaperPoint Sans TC source raster with the maintainer-supplied Noto Sans CJK TC Medium 23.5pt 31x39 bitmap derivative.
- Kept the historical 21x30 logical layout target and resampled the larger 31x39 source into 21x30 * reader-scale targets.
- Preserved the reader CJK scale range at 0.8x..2.5x with default 1.5x at reader font size 36.
- Resampled UI fallback glyphs back to the old logical size so Classic/Lyra UI is not globally enlarged by the larger source raster.

### Vertical spacing
- Changed vertical CJK layout to use a tighter visible-ink advance so character spacing 0px is visibly closer while avoiding overlap.
- Bumped EPUB section cache version to 38.


### Flash footprint reduction for larger CJK font experiments

### Reader font slimming
- Removed the embedded ReaderDyslexic font families from firmware. Existing settings that selected ReaderDyslexic now migrate back to the built-in NotoSans reader fallback.
- Reduced embedded NotoSans reader fonts to NotoSans 14 Regular/Bold, NotoSans 16 Regular/Bold, and NotoSans 8 Regular for small UI text.
- Removed embedded NotoSans Italic/BoldItalic, 12 px, and 18 px reader variants. Italic EPUB styling now falls back to Regular; BoldItalic falls back to Bold.

### Hyphenation slimming
- Kept only English Liang hyphenation data in firmware. Other language tries are excluded because Chinese reading does not need them and English remains useful for mixed Latin text.


### Vertical punctuation and scalable CJK reader font

### Vertical punctuation and alignment fix
- Use vertical presentation forms for CJK brackets/quotes before falling back to rotation.
- Center built-in primary font glyphs in vertical cells so ASCII letters and numbers align with CJK glyphs.

### Built-in CJK scalable reader font
- Scale the embedded CJK fallback in reader layout from 0.8x to 2.5x, with default 1.5x at reader font size 36.
- Apply the scaled CJK metrics to horizontal and vertical layout calculations.
- Bump section cache version to 37.


## 1.5.0 — 2026-06-22 — Reader value adjust compile fix

- Fixed `ReaderValueAdjustActivity` dynamic title translation by replacing `tr(titleId)` with `I18N.get(titleId)`.


## 2026-06-21 — Reader spacing/status-bar fix v2

- Reverted global compact scaling of `UI_10_FONT_ID` / `SMALL_FONT_ID` CJK fallback glyphs.
- Fixed reader status-bar CJK ghosting by bottom-aligning status title text instead of shrinking global UI fonts.
- Separated vertical column spacing from vertical character spacing.
- Made reader font size, line spacing, and character spacing +/- pickers apply immediately.
- Bumped EPUB section cache version to 36.


## 2026-06-21 — Reader spacing and Paper S3 shortcut tuning

- Restored compact runtime CJK scaling for UI_10/SMALL fonts and expanded status bar vertical reserve to reduce Chinese status-bar bottom ghosting.
- Converted reader line spacing to numeric percent and added separate numeric reader character spacing.
- Applied vertical layout spacing to both vertical character advance and column advance.
- Removed non-applicable side-button layout and short-press power-button behavior settings from the settings list.
- Added reader touch shortcuts: middle-upper opens Settings > Reader, middle-lower opens reader page menu.
- Moved Go Home directly below Select Chapter in the reader menu.
- Bumped EPUB section cache version to 35.

## 1.5.0 — 2026-06-22 — Chapter cache performance and vertical image layout fix

- Added detailed Section cache mismatch logs so cache invalidation shows the exact changed parameter.
- Added EPUB-wide shared image extraction cache for repeated resources such as chapter ornaments.
- Added size-qualified `.pxc` image render cache names so one source image can be cached at multiple display sizes safely.
- Changed vertical reading layout images to standalone centered image pages.
- Reduced indexing popup refresh frequency; small chapters now use a static popup only.
- Delayed silent next-chapter indexing until the reader has been idle near the end of a chapter.

## 1.4.0 — 2026-06-21 — Large UI and Traditional Chinese release

- Added the **Large Text** UI theme for settings, file browser, recent books, reader menu, and reader status bar.
- In the Large Text theme, the reader clock and chapter/book title share the same left status-bar slot; enabling one hides the other.
- Changed Large Text bottom hints to compact symbols (`<<`, `o`, `^`, `v`) to keep the oversized UI readable.
- Added the built-in **PaperPoint Sans TC Medium** Traditional Chinese fallback font and made it the built-in UI CJK fallback.
- Updated the Chinese README and web installer page with version 1.4.0 notes, browser flashing steps, supported sleep image formats, and current feature limitations.

## 2026-06-21 — EPUB image cache reliability fix

- Prevented silent next-chapter indexing from persisting transient image failures as permanent `[Image: alt]` section caches.
- Added three-attempt retry handling for EPUB PNG/JPEG extraction and image-dimension reads.
- Increased the section cache format from version 28 to 29 so previously degraded caches rebuild automatically.
- Kept foreground parsing tolerant for genuinely unsupported or damaged images.

## 2026-06-21 — Built-in CJK compile fix

- Restored the missing `#ifndef OMIT_FONTS` guard removed during the legacy font cleanup.
- Kept Noto Sans 14 available outside the optional-font guard so reduced builds retain a default reader font.
- Moved Noto Sans 14 registration outside the matching `OMIT_FONTS` block.
- Revalidated all conditional-compilation directives in `src/main.cpp`.

## 2026-06-20 — Licence compliance remediation

- Removed the proprietary built-in reading font and all generated/source references.
- Migrated the default reader font to Noto Sans with backward-compatible settings conversion.
- Renamed generated OpenDyslexic and Ubuntu derivatives to comply with font naming terms.
- Added EPD_Painter, GC16, hyphenation, font, LGPL release, asset, SBOM and third-party notices.
- Pinned PlatformIO library versions and added automated compliance/SBOM/relink-kit checks.
- Added packaging of the exact resolved ArduinoWebSockets and Arduino-ESP32 source trees for each binary release.

## 2026-06-21 — Earlier Paper S3 baseline work

### UI / UX
- **Vertical text centering** in all list rows (file browser, settings, recent books) — text, icons, subtitles, and values are now properly centered within their row height using dynamic font metrics instead of hardcoded pixel offsets
- **Tap-friendly row heights** for in-book menus on PaperS3: chapter selection, footnotes, and reader menu rows increased from 30–36px to 75px with vertically centered text
- **Lyra 3-Covers theme PaperS3 fix** — added PaperS3-specific metrics (120px menu/list rows, 16px spacing) so touch hit-testing matches the rendered layout; previously all taps mapped to wrong items due to metrics mismatch (64px vs 120px)
- **Multi-cover touch selection** — tapping a specific book cover in the 3-cover home layout now opens that book instead of always opening the first one (uses touch X coordinate)
- **Boot splash footer** aligned with in-app status bar position and pushed 2px higher to prevent descender ghosting (letters like j, g)
- **Reader status bar footer** pushed 2px higher to avoid descender mirroring at the screen edge
- **First-open cover skip** — books now skip the cover page on first open; uses a proper `isFirstOpen` flag (progress.bin existence) instead of the unreliable `spineIndex == 0` check, with fallback to spine 1 when no text reference is found

### Rendering
- **Background-prepared custom sleep images** on Paper S3: `.bmp`, `.jpg`/`.jpeg`, and `.png` files are decoded and converted to validated GC16 caches while the device is idle, so shutdown never waits for image processing
- **Arbitrary-size sleep images**: opaque images preserve aspect ratio and use fixed center-crop fill for the 540×960 screen; transparent PNG overlays preserve aspect ratio, fit entirely without cropping, and blend with the current reading page or a white background
- **Fail-safe sleep fallback**: shutdown uses the newly prepared cache, then the previous valid cache, then the built-in sleep screen; incomplete `.tmp` files are never displayed
- **Force full e-ink refresh** on every activity transition (PaperS3) to eliminate ghosting artifacts
- **VIEWABLE_MARGIN_BOTTOM** reduced from 22 to 16 to reclaim empty space under the footer

### Performance / Stability
- **JPEGDEC stack overflow fix** — the ~16KB JPEGDEC object is now heap-allocated in PSRAM via placement new instead of overflowing the 8KB render task stack
- **Fast JPEG thumbnail path** — on PaperS3, cover thumbnails are decoded directly from PSRAM using JPEGDEC at 1/8 scale with Floyd-Steinberg dithering, bypassing the slow picojpeg + temp file path
- **JPEGDEC patches** for progressive JPEG support (skip AC Huffman tables) and MCU_SKIP guard to prevent crashes on grayscale chroma skip
- **Build optimization** — `-O2` with `-Os` removed from build_unflags so the speed optimization actually takes effect

### CI / Release
- **GitHub Actions**: fixed `upload-artifact@v6` → `@v4` in CI workflow (v6 doesn't exist)
- **GitHub Release creation**: release workflow now creates a GitHub Release with firmware binaries attached via `softprops/action-gh-release@v2`

### Credits
- Display driver: [EPD_Painter](https://github.com/nickoala/EPD_Painter) by nickoala
### Built-in Traditional Chinese font

- Embedded a compact 21×30 **PaperPoint Sans TC Medium** fallback generated from the maintainer-supplied Noto Sans CJK TC Medium raster.
- Added exact-glyph lookup so proportional Latin families no longer hide missing CJK glyphs behind U+FFFD.
- Added mixed Latin/CJK measurement and rendering for horizontal and vertical text.
- Replaced the 5.9 MB direct-index raster at runtime with 31,338 sparse cropped glyphs (about 1.86 MiB including metadata), with no runtime decompression.
- Stopped auto-forcing the legacy SD-card UI font; saved external choices still override the embedded fallback.
- Added OFL licence, provenance, generator, SBOM entry and compliance checks.
- Restored the referenced `partitions.csv` with two 7 MiB OTA application slots so the larger firmware can still use OTA.

## v24 - Touch classifier and inverted reader content experiment

- Fixed reader touch classification so swipe and tap are mutually exclusive.
- Horizontal swipe still fires as soon as threshold is reached, but tap-zone actions are suppressed for that touch sequence.
- Added Controls > Swipe Page Turn setting (`swipePageTurnEnabled`, default on).
- Added Display > Invert Reader Content setting (`readerContentInvert`, default off).
- Reader inversion fills only the content area black and renders text/images inverted; status bar remains normal.
