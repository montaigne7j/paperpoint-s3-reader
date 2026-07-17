# Changelog

## 1.9.0 — 2026-07-17 — Public release

- Published the complete Paper S3 feature set previously consolidated during the 1.8.4 development cycle as semantic version 1.9.0.
- Updated firmware, web-installer, documentation, and SBOM version metadata to 1.9.0.
- Added a complete GitHub release asset set with merged firmware, source archive, LGPL compliance packages, SPDX SBOM, and SHA-256 checksums.

See `RELEASE_NOTES_v1.9.0.md` for the user-facing release summary.

## 1.8.4 — 2026-07-16 — Paper S3 orientation, battery, comics, sleep images and navigation

- Consolidated internal revisions through r34i into the V1.8.4 GitHub release.
- Stabilized battery percentage across USB plug/unplug transitions using real USB detect, filtered ADC sampling, a settle window and rate-limited display tracking.
- Added guided four-pose BMI270 calibration, fixed normal / fixed 180° / automatic 0°–180° reader orientation, and fixed-mode sensor lock with enforced mutual exclusion.
- Rotated reader content, backgrounds, status bars, reader/comic menus, chapter screens, footer controls and touch coordinates together; orientation is now included in EPUB and CBZ framebuffer cache validity.
- Added CBZ/ZIP comic browsing and reading, natural page sorting, PSRAM next-page preload, pending page-turn handling, comic-specific full-refresh intervals and corrected menu hit testing.
- Added hierarchical custom sleep-image selection under `/.sleep` and `/cover`, random/fixed mode, preview/confirm flow, transparent PNG composition and idle cache preparation.
- Added paragraph first-line two-character indent, reader background fade, guide-line stability, inverted background/guide-line rendering and status-bar margin spacing.
- Disabled Paper S3 idle 80 MHz down-clocking for stability and improved watchdog-friendly yields during directory scanning, ZIP extraction and image decode.
- Added an invisible 64×64 reader power-off hotspot at logical top-left without showing an icon.
- File Browser now shows `..` in non-root directories; short Back opens the parent directory, Back at `/` returns Home, and long Back returns Home from any directory.

See `RELEASE_NOTES_v1.8.4.md` for the user-facing release summary. The sections below preserve the internal revision history used during development and debugging.

## v1.8.4-r34h

- 保留閱讀畫面左上角 64×64 隱藏關機熱區，但不再顯示關機圖示。
- EPUB、TXT、CBZ/ZIP、XTC 閱讀頁及 EPUB 閱讀選單均不繪製關機符號。
- 隱藏熱區仍優先於翻頁觸控，並跟隨 0°/180° 畫面方向。

## r34i - File browser parent navigation

- Added a fixed `..` entry as the first row in every non-root directory.
- Short Back now opens the parent directory; Back at `/` returns Home.
- Long Back returns Home from any directory.


## v1.8.4-r34g

- Paper S3 閱讀畫面左上角新增與首頁相同的關機按鈕。
- 關機熱區固定為邏輯座標左上角 64×64，優先於閱讀翻頁觸控區。
- EPUB、TXT、CBZ/ZIP 漫畫與 XTC 閱讀畫面均支援，0°/180° 旋轉時圖示與觸控區同步旋轉。

## r34f - Fourth-step IMU calibration stability fix

- Fixed the face-down calibration step getting stuck because loop iterations without a fresh BMI270 data-ready sample were incorrectly treated as a posture change and reset the 1.2-second hold timer.
- Added a tri-state face-down poll result: no fresh sample, fresh non-face-down sample, or fresh face-down sample. Only a confirmed posture change resets the timer.
- Added a 600 ms stale-sensor guard and explicit calibration transition logs.
- The fourth step now displays a sampling state before taking the final averaged reading.
- Wrapped calibration instructions to the Paper S3 screen width, preventing the long fourth-step message from producing framebuffer out-of-range writes.
- Corrected stable averaging validation to check the unnormalized acceleration magnitude before normalizing the calibration vector.


## r34d - BMI270 shared-I2C calibration and immediate orientation redraw

- Explicitly initializes M5Unified's Paper S3 internal I2C bus on controller 1 (SDA 41 / SCL 42) before starting BMI270, because this firmware does not call the full `M5.begin()` path.
- GT911 touch now joins the same I2C controller in shared-bus mode, allowing touch and BMI270 sampling to coexist.
- IMU calibration now displays a visible sampling state before collecting data and shows an explicit BMI270 initialization error instead of appearing frozen.
- Added a calibration session guard: automatic rotation and pocket lock are suspended during calibration; cancelling restores the last valid stored calibration, while four-step success overwrites it.
- Reader direction selection now performs a synchronous full-screen redraw and clears pre-rotation touch state, so Settings and reader menus rotate immediately instead of waiting for Back.


## r34c - IMU calibration entry, reader-menu rotation, comic touch alignment

- Fixed the Controls > IMU Calibration row so a direct tap opens the guided calibration immediately.
- Reader sub-screens now inherit the reader's 0°/180° orientation, including reader menus, nested settings, chapter screens and calibration screens.
- Corrected the Paper S3 orientation helper from the legacy 0°/90° pair to the required 0°/180° pair, preventing the EPUB menu from normalizing an inverted reader back to normal.
- Paper S3 footer buttons are drawn in the active logical orientation, keeping their artwork and touch zones aligned after a 180° rotation.
- Fixed Comic Reader Menu touch hit testing: its list hit box now starts at the same Y coordinate as the rendered rows instead of 34 px too high.
- Added orientation metadata to EPUB decorative-background cache, EPUB page-frame cache and CBZ preloaded-frame cache. Returning from 180° to normal now regenerates the background/page instead of restoring an inverted cached frame.
- In-progress EPUB background frame warming aborts when orientation changes, preventing mixed-orientation cache frames.


## r33 - Full-speed stability, hierarchical sleep image picker, paragraph indent

- Disabled Paper S3 idle CPU down-clocking. The main loop now stays at the normal boot frequency and uses only a short cooperative delay; `HalPowerManager` also rejects future Paper S3 low-frequency requests. Deep sleep / power-off remains available.
- Added **Settings > Display > Custom Sleep Image**. Choose Random, enter `/.sleep` or `/cover`, browse nested folders, and select BMP/JPG/PNG images.
- Image rows use a two-step interaction: select the row, then select it again (or press Confirm) to open a preview. Preview provides Cancel and Confirm; either returns to the same folder, while Confirm saves the image.
- Random mode recursively scans `/.sleep`, `/cover`, and legacy `/sleep`. A selected image is used exclusively until Random is selected again.
- Added **Settings > Reader > Indent Paragraph First Line by Two Characters** for EPUB and TXT. Horizontal and vertical EPUB layout use two U+3000 cells; TXT pagination and its index cache account for the same indent.
- Bumped EPUB section cache version to 40 and TXT page-index cache version to 4 so changing the indent option cannot reuse incompatible pagination.



## r32 - Comic refresh options and inverted reader backgrounds

- Removed the comic 4-level grayscale option. Comic pages now always use the smoother grayscale/dither path; the old `comicGrayLevels` setting is retained only for compatibility and forced to 1 on load.
- Added Comic Reader Menu option `全刷頻率` with choices: 每頁 / 每2頁 / 每5頁 / 每10頁 / 不全刷.
- CBZ page display now uses the comic-specific full-refresh cycle instead of the global novel-reader refresh frequency.
- EPUB/TXT novel reader black/white inversion now also applies to reader background PNGs: the background is rendered first, then inverted and cached with the invert state.
- Reader guide lines are no longer suppressed in inverted mode; they render through the inverted drawing path so they appear as light guide lines on a dark page.


## r31 - CBZ preload pending page-turn fix

- Restored the intended CBZ preload behavior: background preload is allowed, but page-turn input during preload is captured as pending.
- During preload, the reader polls input after ZIP extraction, after image decode, and once more before returning to the loop.
- If the user taps next/previous while preload is running, the pending page delta is applied immediately after preload finishes, so the user does not need to tap a second time.
- Added logs: `Queued pending next-page input during preload` and `Applying pending page delta ... after preload`.
- Reduced r30's overly conservative preload idle delay back to 1 second so cache remains useful while preserving pending input handling.


## r30 - Comic input reliability fixes

- Fixed ComicReaderMenu touch behavior: one content tap now selects and activates the tapped menu row instead of requiring a second tap.
- Prevented ComicReaderMenu from entering 80 MHz idle power-saving mode, because logs showed the menu dropping to low power between taps.
- Made Comic File Browser Back robust by detecting the raw footer-left touch zone as Back in addition to the logical mapped Back button.
- Increased CBZ background preload idle delay from 1.5 s to 5 s so ordinary next-page taps are handled first instead of being swallowed while the hidden preload is extracting/decoding.
- Kept the r29 guard that prevents preload from starting before a visible page has finished rendering or while the render task is busy.


## r29 - Comic reader menu, preload pending, and footer fixes

- Fixed footer-mode button mapping so visible bottom buttons use fixed Back / Select / Previous / Next zones instead of applying reader front-button remap. This fixes comic file browser Back not responding and Select triggering the wrong action.
- Disabled long-press delete in Comic File Browser to avoid accidental comic deletion from the manga browser.
- Fixed r28 CBZ entry freeze: hidden next-page preload now waits until the visible current page has actually displayed and will not run while the render task is busy. This prevents simultaneous extraction of page N and page N+1 during initial comic open.
- Comic reader now forces a full refresh on entry and after comic display setting changes.
- Added delayed next-page preload and pending page-turn handling after preload completes.
- Added Comic Reader Menu on center tap with:
  - Return Home
  - Gray enhancement from -50% to +50%
  - 4-level / 16-level grayscale preference
- Gray enhancement and grayscale preference are persisted in JSON settings.


## r28 - CBZ fast page cache and manga tap direction

- Fixed CBZ comic status bar out-of-range drawing by using inclusive screen coordinates correctly.
- Tuned ZIP streaming yield interval so ZIP_STORED CBZ pages copied from converter output do not pay unnecessary delay.
- Added CBZ decoded next-page frame cache in PSRAM: after the current page is shown, the next page is extracted and decoded in the background; when the user advances to that page it can be displayed directly from framebuffer cache.
- Preserved the visible current framebuffer after hidden next-page predecode so sleep-image capture and UI transitions do not accidentally use the preloaded page.
- Changed comic touch rule so the left side advances to the next page, while the right side goes to the previous page. Hardware/virtual PageForward still advances.

## r26g - CBZ task watchdog reset fix

## r27 - Comic reader direct-page mode and pocket lock

- Added Home menu separation for Novel Reader and Comic Reader.
- Comic Reader opens a file browser that lists only `.zip` / `.cbz` comic archives plus folders.
- CBZ reader now draws Paper S3 converter output at native size without fit-to-long-edge scaling; 540 px wide pages are drawn flush to the image area and shorter pages leave white space above the fixed status bar.
- CBZ extraction chunk increased to 16 KB for converted files.
- Added optional upside-down pocket lock for reader screens using M5Unified IMU when available; while inverted, reader input is ignored and auto-sleep timing continues to count.


- Fixed repeated `E task_wdt: esp_task_wdt_reset(...): task not found` spam while opening image ZIP / CBZ files.
- CBZ, ZIP extraction, JPEG decode, PNG decode, and File Browser cooperative paths now yield to FreeRTOS using `vTaskDelay(1)` / `delay(1)` without calling `esp_task_wdt_reset()` from tasks that are not registered with the task watchdog.
- Keeps r26f PNG compile fix, r26e CBZ `int pagesUntilFullRefresh` fix, and r26d FileBrowser idle stability changes.


## r26f - PNG converter compile fix

- Fixed a build error in `PngToFramebufferConverter.cpp` where `cooperativePngYield()` was used by PNG file callbacks before it was declared.
- Keeps the r26e CBZ compile fix (`pagesUntilFullRefresh` as `int`) and r26d FileBrowser idle stability changes.

## r26e - CBZ Compile Verification Fix

- Reverified `CbzReaderActivity::pagesUntilFullRefresh` is declared as `int`, matching `ReaderUtils::displayWithRefreshCycle()`'s `int&` parameter.
- This package is intended to avoid accidental build from older r26/r26b sources where the member was still `uint8_t`.
- Includes the r26d FileBrowser idle stability changes.


## r26d - File Browser idle stability fix

- Keep File Browser at full CPU speed on Paper S3 instead of entering the 80 MHz idle mode while browsing folders.
- Add cooperative watchdog resets/yields during File Browser directory scans and sorting.
- Preserve the r25/r26 80 MHz idle policy for screens that explicitly allow idle power saving; CBZ reader remains full-speed during image work.

## r26c - Image ZIP / CBZ stability fix

- Keep CBZ reader at full CPU speed while active; it no longer enters the Paper S3 80 MHz idle path.
- Add more aggressive cooperative yielding and watchdog reset points during ZIP extraction and JPEG/PNG image decode.
- Reduce CBZ extraction chunk size to 1024 bytes to give FreeRTOS/IDLE more chances to run on large manga pages.
- This is a stability-first patch for repeated watchdog resets while opening large comic ZIP files.



## r26b image ZIP / CBZ watchdog fix

- Fixed image ZIP / CBZ opening triggering the ESP32 task watchdog during first-page extraction or JPEG/PNG decode.
- Added cooperative delays/yields during ZIP streaming inflate and image decode callbacks so the ActivityManager does not monopolize CPU0 while large manga pages are being decoded.
- Added CBZ progress logs around page extraction and image decode to make future failures easier to locate in device logs.
- No light sleep or 10 MHz mode is introduced; this only makes long ZIP/image work watchdog-safe.


## r26a image ZIP / CBZ compile fix

- Fixed a PlatformIO compile error in `CbzReaderActivity`: `pagesUntilFullRefresh` now uses `int`, matching `ReaderUtils::displayWithRefreshCycle()`'s `int&` parameter, consistent with EPUB/TXT/XTC readers.
- No intended behavior change from r26.

## r26 image ZIP / CBZ reader update

- Added image ZIP / CBZ reader support for `.zip` and `.cbz` files.
- File Browser now lists `.zip` / `.cbz`; Reader dispatches these files to a dedicated CBZ/image-ZIP reader instead of the EPUB reader.
- The CBZ reader scans ZIP central directory entries, filters JPG/JPEG/PNG/BMP images, applies natural filename sorting, extracts one image at a time to a temporary cache file, and saves/restores page progress.
- ZIP/CBZ rendering is image-first: reader background PNG and reader content margins are not applied.
- A fixed bottom status area is reserved for image ZIP/CBZ pages; status content follows the current status-bar settings for page count, percentage, title, battery, clock, and progress bar.
- Added ZipFile central-directory entry listing helper used by the image ZIP/CBZ reader.

## r25 power idle / WiFi cleanup update

- Enabled first-stage Paper S3 idle power saving: after 3 seconds of inactivity the CPU drops to 80 MHz and the main loop touch polling delay increases from 2 ms to 20 ms.  Render, input, WiFi, and skip-delay activity restore full speed; no light sleep or 10 MHz mode is used.
- Changed unplugged logging behavior: ERR logs are still formatted and kept in the RTC ring buffer, but INF/DBG logs are runtime-gated before argument evaluation and string formatting when no USB serial monitor is connected.
- Changed release and release-candidate PlatformIO log level to ERR-only; detailed INF/DBG logging remains available in the default development build.
- Fixed standalone Settings > WiFi Networks cleanup: scans/connections launched from Settings now turn WiFi off on exit, while network parent activities still keep WiFi on for WebServer, OPDS, OTA, Calibre, and KOReader workflows.

## r24 fast tap / chapter footer update

- Added Paper S3 reader fast-tap page-skip behavior with tentative page status such as `4.../23`.
- Accumulates repeated page-turn taps within a known chapter and renders only the final target page after a short debounce.
- Stops accumulation at unknown chapter boundaries and absorbs further taps while the chapter is loading/indexing.
- Pauses frame-cache warming while tentative page status is active to avoid storing tentative status bars in cache entries.
- Starts silent next-chapter indexing earlier near the last five pages.
- Changed chapter selection footer buttons to Exit / Parent / Previous page / Next page; the second footer button no longer selects chapters.


### V1.8.4-r23 page-turn responsiveness fix

- Fixed page turns feeling stuck when reader background PNG and frame cache are enabled. A page-turn command now executes with a visible render fallback instead of waiting for background frame-cache readiness.
- Added a PSRAM-backed reader background framebuffer cache. The same `/bg` PNG + fade level is decoded once, then copied into later visible renders and background frame-cache jobs, removing the repeated ~415 ms PNG decode cost seen in device logs.
- Pending page turns created while the e-paper panel is still busy refreshing are executed as soon as the render lock is free; they no longer wait for the target page frame cache to finish.

### V1.8.4-r22 status-bar margin spacing

- Fixed reader text sitting too close to the status bar when **Status Bar Follows Margin** / **狀態列跟隨頁邊距** is enabled. EPUB and TXT layout now reserve the actual drawn status-bar text/progress height plus a 12 px safety gap instead of relying only on `getStatusBarHeight()`.
- The same bottom-reserve calculation is used by EPUB visible rendering and background page-frame cache warming, so cached pages and freshly rendered pages keep the same last-line spacing.
- Auto-page-turn toggling now reflows the current EPUB section only when the status-bar reserve changes, preserving the current reading position.

### V1.8.4-r21 reader background fade and guide-line stability

- Added **Reader background fade** in Settings > Display. It fades the selected `/bg` PNG toward white from 0% to 90% in 10% steps before dithering, making decorative backgrounds easier to read through.
- Fixed intermittent missing reader guide lines. The old 1px light-gray dither pattern could disappear entirely on odd horizontal/vertical coordinates; guide lines now use a coordinate-independent faint pixel pattern.
- Re-draw guide lines after the second pass on image pages with text anti-aliasing, so image-page refresh paths keep the same guide-line overlay as normal text pages.

### V1.8.4-r19a compile fix

- Fixed SettingsList enum labels for Reader background PNG and Reader guide lines by using the existing `STR_NONE_OPT` label for the Off/None value instead of the non-existent `STR_OFF`.
- No behavior change from r19; this is a PlatformIO compile fix.


## 1.8.4 development baseline — 2026-06-30 — Font folder compatibility and navigation refinements

- Font scanning now supports both `/font` and `/fonts`; duplicate filenames prefer `/font`.
- External `.bin` / `.epdf` font filename parsing is more tolerant: CJK names, spaces, symbols, `x`, `X`, and `×` dimension separators are accepted.
- External legacy `.bin` reader glyphs now scale with Reader Font Size instead of only changing layout space.
- Reader character spacing can be negative for experimental external bitmap fonts, and line spacing range is wider.
- File Browser footer Back initially left File Browser in one tap; this development behavior was later superseded by r34i parent-directory navigation.
- EPUB chapter selection footer now uses Exit / Select / Parent / Next-page semantics.
- Lyra home menu icons for Browse Files, Recent Books, File Transfer, and Settings are rotated 90° left; Power remains unchanged.

## 1.8.1 — 2026-06-29 — Paper S3 input responsiveness diagnostics

- Promoted key touch / button diagnostics to `INF` level so release logs still show tap, swipe, footer tap, and two-finger reader input events.
- Added reader input counters: `touchDetected`, `inputQueued`, `inputIgnoredBusy`, `inputIgnoredPending`, and `inputExecuted`.
- Kept exactly one pending page-turn command while render/cache is busy; later page-turn inputs are counted as pending ignores instead of silently disappearing.
- Added `INF` warnings for long render/cache busy windows, pending page-turn waits, and display idle waits over 500 ms / 1000 ms.
- Scope intentionally avoids memory allocator, glyph prewarm, frame-cache policy, and EPD waveform changes from V1.8.0.

## 1.8.0 — 2026-06-29 — Paper S3 TTF memory stabilization and boot waveform settling

- Promoted the PaperPoint S3 Reader release line to `1.8.0` for GitHub publication.
- Added a FreeType / OpenFontRender PSRAM-preferred allocator: FreeType allocations >= 512 bytes prefer `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`, while smaller allocations prefer internal RAM with fallback.
- Added FT allocator diagnostics (`FTALLOC`) for allocation location, summary counters, PSRAM/internal requested bytes, failures, and FreeType library lifecycle hooks.
- Kept the r14a background frame-cache safety rule: background rendering may use RAM/disk TTF glyph hits, but true TTF rasterization misses abort the cache job and do not store fallback-rendered frames.
- Added idle glyph prewarm for adjacent pages and tightened the release default to `internalFree >= 100000`, `internalMaxAlloc >= 50000`, and at most one glyph per prewarm pass.
- Added a boot-time reader page-turn counter for Paper S3 band-scan waveform settling.  The first 10 reader page turns use 4 target-black darker passes; later turns use 5 passes.
- Updated README, USER_GUIDE, built-in EPUB manual, web installer copy, release audit notes, and code comments for the V1.8.0 GitHub release.


## 1.7.0 page-turn boot waveform settling experimental r16

- Added a boot-time reader page-turn counter for the Paper S3 band-scan waveform.
- The first 10 full reader page-turn refreshes use a gentler target-black / white-to-black darker schedule: 4 darker passes.
- Starting from page turn 11 after boot, the waveform automatically switches to the stable target-black schedule: 5 darker passes.
- Added a per-page-turn serial diagnostic line: `Page-turn waveform profile: turn=... settleTurns=... blackDarkerPasses=... profile=boot/stable`.
- Kept the existing r15 FreeType PSRAM allocator, r14a background TTF-miss abort, idle glyph prewarm, visible low-memory guard, and frame-cache start gate behavior.

## Experimental r14a - Compile fix

- Fixed PlatformIO compile failure in `ExternalFont.h` caused by a duplicated inline `ExternalFont::isTtfFormat()` declaration introduced while adding the r14 glyph availability helpers.
- No behavior change from r14 idle glyph prewarm / r13a frame-cache dirty-abort behavior.

## Experimental r14 - Idle TTF glyph prewarm

- Kept r13a's background frame-cache safety rule: background cache may use RAM/disk TTF glyph hits but must abort on a true TTF rasterization miss and must not store a fallback-rendered frame.
- Fixed background TTF lookup to allow persistent SD glyph-cache hits without treating them as misses, improving frame-cache hit rate after glyphs have been rasterized once.
- Added idle glyph prewarm for adjacent reader pages: when the reader is idle and memory is healthy, it scans next/previous page text and rasterizes up to three missing TTF glyphs per pass.
- Idle prewarm only runs when internalFree >= 80 KB, internalMaxAlloc >= 40 KB, PSRAM free >= 3 MB, and no page turn/render work is pending.
- A single glyph prewarm pass stops and pauses if internal heap drops more than 6 KB or largest block drops more than 4 KB.
- When idle prewarm successfully warms glyphs for a page that was cooling down after `ttf-miss-suppressed`, the cooldown is cleared so frame-cache warming can retry sooner.


## 1.7.0 memory-guard diagnostics experimental r13

- Added a runtime TTF rasterization guard for the reader font path.
- Background page-frame cache rendering now draws only glyphs already present in the active glyph cache or persistent disk cache; if a TTF cache miss would require OpenFontRender/FreeType rasterization, the background cache job aborts and arms a cooldown for that spine/page.
- Visible rendering now disables new TTF rasterization when internal heap is critically low (`internalFree < 12000` or `internalMaxAlloc < 4096`), allowing renderer fallback/placeholder behavior instead of forcing another risky glyph rasterization.
- Preserved the r12 frame-cache start gate and low-memory cooldown to prevent retry storms.
- Documented that large TTF buffers already use PSRAM, while deeper OpenFontRender/FreeType allocator redirection remains a follow-up item.


## 1.7.0 memory-guard diagnostics experimental r12a

- Fixed PlatformIO library build failure where `ReaderMemoryDiagnostics.h` was not visible to `lib/ExternalFont`, `lib/TtfFont`, `lib/GfxRenderer`, and `lib/Epub`.
- Added an explicit project include path and a header-only local helper library shim for `ReaderMemoryDiagnostics`.
- No behavior change from r12 memory guard / glyph diagnostics logic.


## 1.7.0 memory-guard diagnostics experimental r12

- Prevents reader background frame-cache retry storms in low internal heap conditions. Ordinary, non-pending frame-cache warming now runs only in `NORMAL` memory state.
- Adds a hard start gate for background frame-cache jobs: `internalFree >= 30000` and `internalMaxAlloc >= 12000`.
- Adds an 8-second low-memory cooldown for the same `spine/page` after `CRITICAL`/`EMERGENCY` frame-cache aborts.
- Pending page turns that cannot safely start a cache job now fall back to visible render instead of waiting for a cache frame.
- Adds deeper `MEMD` diagnostics for TextBlock rendering, vertical text draw, external glyph lookup, glyph cache miss/load, TTF rasterization, bitmap cache/disk store, and temporary preload vector allocation.


## 1.7.0 adaptive-memory-cache experimental r10

- Fixed a pending page-turn stall found in r9: if a queued target-page warm job enters `CRITICAL` memory state and aborts before producing a cache frame, the queued turn now bypasses the cache gate and proceeds through the normal visible render path.
- Prevented the warm loop from repeatedly restarting the same low-memory pending target every few milliseconds after such an abort.
- Added explicit logs for the fallback: `Pending page turn will use visible render after low-memory cache abort` and `Page turn cache gate bypassed after low-memory warm abort`.

## 1.7.0 adaptive-memory-cache experimental

- Added reader memory-state classification for Paper S3: `NORMAL`, `WARNING`, `CRITICAL`, and `EMERGENCY`.
- Silent next-chapter indexing now runs only in `NORMAL` memory state.
- Background page-frame cache warming now adapts by memory state: `NORMAL` keeps normal adjacent caching, `WARNING` warms only the next page or queued pending target, and `CRITICAL`/`EMERGENCY` stops background warming.
- In `CRITICAL`/`EMERGENCY`, frame-cache slots unrelated to the current page or next page are released; `EMERGENCY` also frees their backing buffers.
- Visible cache-miss renders skip storing a new framebuffer cache entry in `CRITICAL`/`EMERGENCY`.
- Added periodic PSRAM/internal heap diagnostics and reader memory-state logs.
- Renamed misleading reader input logs from “non-page-turn” wording to neutral “input event ignored” wording.

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

## Diagnostic experimental - internal heap trace
- Added Paper S3 reader diagnostic logs for large buffer allocation location (`PSRAM` / `INTERNAL` / `UNKNOWN`).
- Added `MEMD` internal heap delta logs around frame-cache warm start, page object load, glyph/page render, cache store, and frame-cache job abort.

## Experimental r13a - Frame-cache TTF miss dirty/abort fix

- Fixed background frame-cache TTF miss suppression so the suppression flag survives policy restoration and is visible to the reader cache job.
- Background frame-cache jobs now treat any suppressed TTF glyph miss as a dirty render: abort the job, skip cache-store, add target cooldown, and force pending page turns to visible render when needed.
- Direct frame-cache render also skips cache-store when a TTF miss is suppressed.
- This prevents fallback/system-font partial renders from being stored as valid frame-cache hits while preserving the r13 guard against background TTF rasterization.

## Experimental r15 - FreeType PSRAM allocator redirection

- Added a patched OpenFontRender build step that replaces `FT_Init_FreeType()` with a custom `FT_New_Library()` allocator path.
- Added `CrossPointFtPsramAllocator` with `ft_psram_alloc`, `ft_psram_realloc`, and `ft_psram_free` equivalents:
  - allocations >= 512 bytes prefer `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`.
  - allocations < 512 bytes prefer internal 8-bit heap, with generic fallback.
- Added FT allocator diagnostics:
  - large `FT alloc` / `FT realloc` / `FT free` detail logs.
  - `FT allocator summary` counters with PSRAM/internal allocation counts and requested bytes.
- Kept r14a strategy intact:
  - background TTF miss aborts and does not store frame cache.
  - idle glyph prewarm remains guarded.
  - visible low-memory TTF guard and frame-cache start gate remain enabled.
- Tightened idle glyph prewarm while allocator redirection is being validated:
  - `internalFree >= 100000`.
  - `internalMaxAlloc >= 50000`.
  - max 1 glyph per prewarm pass.

## PaperPoint r34 orientation sensing and battery stability

- Replaced USB CDC connection checks with the Paper S3 USB_DET GPIO5 signal.
- Added median-filtered battery ADC sampling, a Li-Po discharge curve, a 30-second USB transition hold, and rate-limited percentage changes.
- Added BMI270 four-pose calibration stored in ESP32 NVS; direction sensing remains disabled until calibration succeeds.
- Added Controls settings for fixed normal, fixed 180-degree, and automatic 0/180-degree reader direction.
- Added an upside-down reader input lock available only in fixed direction modes; automatic direction always disables the lock.
- Added a guided four-step gyroscope calibration screen with automatic face-down completion after a stable reading.

### r34a compile fix

- Added the missing `CrossPointSettings::READER_ORIENTATION_MODE` declaration before the settings data members.
- Fixes library compilation errors reporting `READER_ORIENTATION_FIXED_NORMAL was not declared in this scope`.
