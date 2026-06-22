# GitHub Release Audit Notes

本檔記錄 `crosspoint-reader-papers3(11).zip` 發布前檢查結果與建議調整。

## 已檢查項目

- `scripts/validate_embedded_cjk_font.py`：通過。
- `scripts/check_license_compliance.py`：通過。
- `scripts/generate_sbom.py`：可產生有效 SPDX JSON。
- 專案大小約 38 MB，未發現超過 GitHub 單檔 50 MiB warning 門檻的檔案。
- 目前最大檔案是內建 CJK 字型轉換出的 header 與來源 BIN，已低於 GitHub 一般 repo 單檔限制。
- 已包含 `LICENSE`、`LICENSES/`、`THIRD_PARTY_NOTICES.md`、`BINARY_RELEASE_LGPL_COMPLIANCE.md`、`RELEASE_COMPLIANCE_CHECKLIST.md`、`SBOM.spdx.json`。

## 建議已處理

### 移除本機 VS Code IntelliSense 設定

原 ZIP 內含 `.vscode/c_cpp_properties.json`，其中有本機路徑：

```text
D:/program/paperS3/...
C:/Users/monta/.platformio/...
```

這些路徑對其他使用者無效，也會暴露本機帳號名稱。發布版已移除 `.vscode/`。

### 移除過時 submodule 設定

原 ZIP 內有 `.gitmodules` 指向 `open-x4-sdk`，但目前專案不再使用該 SDK，且 ZIP 內也沒有 `open-x4-sdk/`。發布版已移除 `.gitmodules`，避免 GitHub 顯示缺失 submodule。

### 移除原作者 Funding 設定

原 ZIP 內 `.github/FUNDING.yml` 指向原 CrossPoint Reader 作者帳號。若本 fork 要使用自己的贊助連結，請重新建立；若不需要，建議先不要發布 Funding 設定。

### 移除可由 build 產生的生成檔

下列檔案已在 `.gitignore` 中，且會由 PlatformIO pre-script 產生，發布版已移除：

```text
lib/I18n/I18nKeys.h
lib/I18n/I18nStrings.h
lib/I18n/I18nStrings.cpp
src/network/html/*.generated.h
```

## 仍建議發布前確認

1. `platformio.ini` 的 `[crosspoint] version` 是否要從 `1.4.0` 更新到正式發布 tag，例如 `1.4.1` 或 `1.5.0`。
2. `README.md` 中的 GitHub Pages 安裝網址是否已對應到正確 repo。
3. GitHub Pages 是否已啟用，且 `docs/install/manifest.json` 能找到 release workflow 產生的 `merged-firmware.bin`。
4. 若公開發佈 firmware binary，請使用 release workflow 輸出的完整附件，而不要只上傳單獨 `firmware.bin`。
5. 若要保留 `.skills/` AI 開發指南，建議確認內容沒有個人資訊；若只給自己使用，可不放進 public repo。

## 建議 release asset

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
