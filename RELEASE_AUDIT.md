# GitHub Release Audit Notes — V1.8.0

本檔記錄 `PaperPoint S3 Reader` V1.8.0 發布前檢查與整理結果。

## V1.8.0 定版重點

- 版本號已更新為 `1.8.0`：`platformio.ini`、`README.md`、`docs/install/manifest.json`。
- README / USER_GUIDE / web installer / built-in EPUB manual 已補充 V1.8.0 TTF memory、idle glyph prewarm 與 boot page-turn waveform 說明。
- `CHANGELOG.md` 已新增 V1.8.0 條目。
- 保留 r15/r16 行為：FreeType PSRAM allocator、background TTF miss abort、idle glyph prewarm、visible low-memory guard、frame cache start gate、開機後 darker pass counter。
- 發布 source package 已移除本機 debug logs (`logs/`)；若需要保留分析紀錄，建議另放 GitHub issue 或 release discussion。

## Release checklist

1. 本地編譯：`pio run -e default`。
2. 發布編譯：`pio run -e gh_release`。
3. 燒錄實機後確認開機版本顯示 `1.8.0`。
4. 檢查序列 LOG 是否出現：
   - `Starting CrossPoint version 1.8.0`
   - `OpenFontRender FreeType PSRAM allocator patch ...`
   - `FT allocator summary: context=after-new-library`
   - `Page-turn waveform profile: turn=... profile=boot/stable`
5. 實機測試至少 20 次 reader 翻頁，確認：
   - 前 10 次 darker pass 使用 boot profile。
   - 第 11 次後切換 stable profile。
   - frame cache hit 時翻頁約 0.6 秒級。
   - internal `MaxAlloc` 不再像 r14a 一路下降到 CRITICAL / EMERGENCY。
6. 建立 Git tag：`v1.8.0`。
7. 確認 GitHub Actions 產生 `firmware.bin`、`merged-firmware.bin`、source archive、LGPL relink kit、SBOM 與 license bundle。

## Known limitations

- Wi-Fi 上傳、OTA 與 KOReader Sync 保留原 CrossPoint 架構，仍建議視為進階 / 實驗功能。
- FreeType allocator redirection 需要確認 PlatformIO pre-build patch 已成功套用至 OpenFontRender；若 LOG 沒有 `FTALLOC`，請先檢查 `scripts/patch_openfontrender_ft_allocator.py` 是否有執行。
- TTF 新字第一次 rasterize 仍可能比 cache hit 慢；V1.8.0 的目標是降低 internal heap 壓力並提升後續 cache hit 穩定性。

---


## r16 page-turn boot waveform settling audit

- Scope: EPD page-turn waveform timing only. No changes to EPUB parsing, TTF rasterization policy, frame cache memory guards, or FreeType allocator behavior.
- Added a boot-session page-turn counter inside `EPD_Painter::paintRowMajor()`, which is the reader page-turn band-scan refresh path.
- Default profile:
  - turns 1-10 after boot: target-black / white-to-black darker passes = 4
  - turns 11+ after boot: target-black / white-to-black darker passes = 5
- Compile-time tunables:
  - `EPD_PAGE_TURN_BOOT_SETTLE_TURNS`
  - `EPD_PAGE_TURN_BOOT_BLACK_DARKER_PASSES`
  - `EPD_PAGE_TURN_STABLE_BLACK_DARKER_PASSES`
- Added serial diagnostics to confirm the active profile and turn counter during device testing.

## Experimental r14a audit notes

- Fixed duplicate `ExternalFont::isTtfFormat()` declaration in `lib/ExternalFont/ExternalFont.h`.
- Behavior is unchanged from r14; this is a compile-only fix.
- PlatformIO compile was not run in this environment because `pio` is unavailable here.

## Experimental r14 audit notes

- Base: r13a TTF raster guard and frame-cache dirty/abort behavior.
- Added `TtfFontEngine::loadCachedGlyph()` / `hasCachedGlyph()` so background frame-cache rendering can load persistent SD glyph-cache hits without invoking OpenFontRender/FreeType.
- Added `ExternalFont::isGlyphAvailableWithoutRasterize()` for safe prewarm planning and background cache eligibility checks.
- Added reader idle glyph prewarm: next/previous page text is scanned during idle, and up to three truly missing TTF glyphs are rasterized per pass only under healthy memory thresholds.
- Memory guard for prewarm: internalFree >= 80000, internalMaxAlloc >= 40000, PSRAM free >= 3 MiB; stop/pause on per-glyph drops below -6000 free or -4096 max-alloc.
- Expected logs: `Idle glyph prewarm: page=... warmed=...`, `Frame cache cooldown cleared after idle glyph prewarm...`, `glyph-cache-disk-hit-background ...`, and fewer `Frame cache job aborted: reason=ttf-miss-suppressed ...` after adjacent-page glyphs are warmed.
- PlatformIO compile was not run in this environment because `pio` is unavailable here.

# Historical GitHub Release Audit Notes — V1.7.0

本檔記錄 `PaperPoint S3 Reader` V1.7.0 發布前檢查與整理結果。

## 已處理的發布清理

- 移除舊工作目錄 `v29_progressive_work/`，避免把早期完整專案副本一起上傳。
- 移除 `.vscode/`，其中 `c_cpp_properties.json` 與 `launch.json` 含本機 Windows 絕對路徑，對其他使用者無效。
- 移除 `.gitmodules`，目前專案不再使用 `open-x4-sdk` submodule，保留會讓 GitHub 顯示缺失 submodule。
- 移除 `.skills/`、`.github/skills/` 與 `CLAUDE.md`，這些是本機 AI agent 工作指南，不屬於正式 source release 必要內容。
- 移除 `.github/FUNDING.yml`，該檔指向原 CrossPoint Reader 作者帳號；若本 fork 之後需要贊助連結，請另行建立。
- 移除 `.gitignore` 中列出的可生成檔：`lib/I18n/I18nKeys.h`、`lib/I18n/I18nStrings.h`、`lib/I18n/I18nStrings.cpp` 與 `src/network/html/*.generated.h`。
- 移除 `test/epubs/T0253.epub`，該檔已列入 `.gitignore`，不納入公開 source package。

## V1.7.0 定版重點

- 版本號已更新為 `1.7.0`：`platformio.ini`、`README.md`、`docs/install/manifest.json`。
- README / USER_GUIDE / web installer 頁面已補充 V1.7.0 page-turn cache 與 waveform 說明。
- `CHANGELOG.md` 已新增 V1.7.0 條目。
- Paper S3 page-turn waveform 預設值已固定在 `lib/EPD_Painter/EPD_Painter.cpp`，`platformio.ini` 保留 override 範例但不預設覆蓋。

## 建議發布前本機確認

1. 清除舊 build：`pio run -t clean`。
2. 產生生成檔並驗證授權：
   `python scripts/gen_i18n.py`
   `python scripts/generate_sbom.py --output SBOM.spdx.json`
   `python scripts/validate_embedded_cjk_font.py`
   `python scripts/check_license_compliance.py`
3. 編譯 release：`pio run -e gh_release`。
4. 實機測試：連續上一頁 / 下一頁、UI 回內文、圖片頁回文字頁、休眠圖、檔案瀏覽與最近閱讀。
5. 建立 tag：`v1.7.0`，讓 GitHub Actions 產生正式 release artifacts。

## 建議 release assets

正式 tag release 建議至少包含：

```text
merged-firmware.bin
firmware.bin
bootloader.bin
partitions.bin
complete-application-source.zip
lgpl-component-sources.zip
lgpl-relink-kit.zip
license-bundle.zip
SBOM.spdx.json
SHA256SUMS.txt
```

`merged-firmware.bin` 是一般使用者最適合下載與燒錄的檔案；其他附件主要用於授權合規、可重建與進階除錯。

## 建議 GitHub Topics

```text
m5stack
m5paper-s3
esp32-s3
epaper
ebook-reader
epub
traditional-chinese
platformio
arduino-esp32
```


## Logo orientation / root documentation cleanup

- `GfxRenderer::drawImage()` now renders built-in 1-bit assets through the normal orientation-aware pixel path, so `Logo120` appears upright on the portrait sleep screen.
- Historical experiment and fix notes were moved from the repository root to `docs/development-notes/` to keep the GitHub project root focused on release-facing files.
- `docs/assets/guide/` screenshots were inspected. They remain 540×960 PNG reference images for the V1.7.0 guide.
- The built-in manual EPUB remains embedded and was updated for V1.7.0 page-turn/cache behavior.


## r5 release-candidate checks

- Home Power Off no longer calls ActivityManager::goToSleep() from inside HomeActivity::loop(); it requests deep sleep and the main loop performs the real sleep sequence.
- Lyra Home cover-buffer snapshot uses PSRAM to avoid exhausting internal heap during the 3-covers theme.
- EPUB progress restore rejects out-of-range spine/page values and skips invalid progress saves.
- Power Off menu item has a visible Power icon in Lyra/Lyra 3 Covers themes.

## V1.7.0 r6 check

- Home Power Off icon now uses logical transparent icon rendering so it is not rotated 90 degrees on Paper S3 portrait UI.


## Adaptive memory cache experimental notes

- Reader logs now include `Reader memory state=NORMAL/WARNING/CRITICAL/EMERGENCY` with internal heap and PSRAM free/largest-block values.
- Silent indexing is gated to `NORMAL` only.
- `WARNING` preserves page-turn feel by warming only the next page or the active pending target.
- `CRITICAL`/`EMERGENCY` prune unrelated page-frame cache slots; `EMERGENCY` frees backing buffers.
- Cache-miss visible renders skip frame-cache storage when memory is already `CRITICAL` or `EMERGENCY`.


## adaptive-memory-cache r10 stall fix

- Reviewed r9 field log showing repeated `WARNING` at `page-turn-cache-gate` followed by `CRITICAL` at `frame-cache-job`, with the same pending target cache job aborting and restarting every few milliseconds.
- Added a pending-turn visible-render fallback so a queued page turn cannot wait forever for a target framebuffer cache that memory policy keeps aborting.
- Preserved r9 memory-state diagnostics and adaptive cache policy.

## Diagnostic experimental audit
- Reader frame-cache buffers now log pointer, size, and detected memory location at allocation/reuse/free/store points.
- Reader diagnostic `MEMD` logs now report internal heap free/max-alloc deltas and PSRAM free delta around frame-cache warm start, page object load, glyph render, cache store, and job abort.


## memory-guard diagnostics r12 audit

- Added first-stage stall protection for Paper S3 reader frame-cache work:
  - ordinary background frame-cache warming is `NORMAL`-only;
  - `WARNING` does not run non-pending cache jobs;
  - `CRITICAL`/`EMERGENCY` stop cache jobs;
  - cache-job start requires `internalFree >= 30000` and `internalMaxAlloc >= 12000`.
- Added low-memory same-page cooldown: a `CRITICAL`/`EMERGENCY` abort arms an 8-second cooldown for the same `spine/page`, preventing ~50 ms retry storms.
- Pending page-turn targets that hit the cooldown or hard start gate are marked for visible render fallback, so user page turns do not wait forever for a framebuffer cache.
- Added second-stage diagnostics to isolate internal heap loss inside the text/glyph path:
  - `textblock-drawVerticalText[...]`;
  - `textblock-render-total[...]`;
  - `drawVerticalText-total`;
  - `glyph-lookup-*`;
  - `glyph-cache-miss-load-*`;
  - `glyph-rasterize-drawString`;
  - `glyph-rasterize-total`;
  - `glyph-bitmap-store-*`;
  - `temporary-vector-preloadGlyphs-sorted`.
- The added diagnostic helper logs internal free/largest-block and PSRAM deltas, and hot-path glyph logs are thresholded to reduce noise.

### r12a compile-visibility fix

- Root cause: `ReaderMemoryDiagnostics.h` was included by several `lib/*` components, but PlatformIO did not expose the project `include/` directory to those library compilation units in the user's build.
- Fix: add `-Iinclude` to the shared build flags and add a header-only `lib/ReaderMemoryDiagnostics` shim so PlatformIO's Library Dependency Finder can resolve `<ReaderMemoryDiagnostics.h>`.
- Scope: compile fix only; r12 memory guard thresholds, low-memory cooldown, and diagnostics are unchanged.



## r13 memory guard audit

- Background frame-cache TTF miss suppression implemented in `ExternalFont::getGlyph()` via a runtime rasterize policy. Cache hits still render; misses are not inserted as `notFound`, so visible rendering can rasterize later.
- `EpubReaderActivity` now wraps background frame-cache render chunks with rasterization disabled and aborts the page-frame cache job when a miss is suppressed. Pending page turns fall back to visible render.
- Visible page rendering checks internal heap before page render and disables new TTF rasterization below the low-memory guard threshold.
- TTF glyph cache entries, glyph hash table, raster scratch, disk index and index read buffer already allocate in PSRAM. OpenFontRender/FreeType internal allocator redirection was not attempted in r13 because that requires modifying/wrapping the third-party renderer allocator.
- Compliance scripts passed after this change. PlatformIO compile must still be verified on the target development machine.

## Experimental r13a audit notes

- Base: r13 TTF raster guard.
- Fix: `ExternalFont::setRuntimeTtfRasterizeAllowed()` no longer clears the TTF-miss-suppressed flag during policy restoration. The reader explicitly resets the flag at the start of each scoped render and consumes it after render completion.
- Fix: cooperative and direct background frame-cache renders abort with `dirty=1 store=0` when a suppressed TTF miss occurs, so fallback/system-font frames are not stored or used as cache hits.
- Expected log on background miss: `TTF glyph miss rasterize suppressed...` followed by `Frame cache job aborted: reason=ttf-miss-suppressed ... dirty=1 store=0`, with no `Frame cache stored` for that target.
- PlatformIO compile was not run in this environment because `pio` is unavailable here.

## r15 allocator redirection audit

- Added `scripts/patch_openfontrender_ft_allocator.py` as a PlatformIO pre-build patch for the downloaded OpenFontRender v1.2 dependency.
- The patch is idempotent and changes OpenFontRender initialization from `FT_Init_FreeType(&g_FtLibrary)` to `CrossPointFtPsramAllocator::newLibrary(&g_FtLibrary)`.
- The allocator uses `FT_New_Library(customMemory, &library)` and `FT_Add_Default_Modules(library)`, so FreeType cache-manager and glyph-render allocations should go through the project allocator.
- Large allocations are routed to PSRAM first; small allocations prefer internal heap to avoid PSRAM overhead for tiny metadata.
- Runtime validation should look for `FT allocator summary` and large `FT alloc` logs showing `newLoc=PSRAM` during `glyph-rasterize-drawString`.
- r14a frame-cache safety behavior is retained; this release changes allocator plumbing and idle-prewarm thresholds only.
