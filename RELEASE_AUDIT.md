# GitHub Release Audit Notes — V1.7.0

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
