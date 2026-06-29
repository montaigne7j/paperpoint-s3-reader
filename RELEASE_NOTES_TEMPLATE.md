# PaperPoint S3 Reader V1.8.0 release summary

## Highlights

- FreeType / OpenFontRender PSRAM-preferred allocator for TTF rasterization.
- Background frame cache no longer stores fallback-rendered pages after TTF glyph misses.
- Idle glyph prewarm for adjacent pages with stricter memory gates.
- Boot-time page-turn darker-pass counter: first 10 turns use gentler black driving, later turns use stable black driving.
- Updated built-in bilingual EPUB manual and documentation.

## Recommended test log checks

```text
Starting CrossPoint version 1.8.0
FT allocator summary: context=after-new-library
Page-turn waveform profile: turn=1 ... profile=boot
Page-turn waveform profile: turn=11 ... profile=stable
```

# PaperPoint S3 Reader vX.Y.Z

## 下載建議

一般使用者請下載：

- `merged-firmware.bin`：可直接從 `0x0` 燒錄，內含 bootloader、partition table、boot app 與 firmware。
- `SHA256SUMS.txt`：用於確認下載檔案完整性。

進階使用者與授權合規附件：

- `complete-application-source.zip`
- `lgpl-component-sources.zip`
- `lgpl-relink-kit.zip`
- `license-bundle.zip`
- `SBOM.spdx.json`

## 本版重點

- 中文優先 Paper S3 閱讀體驗。
- 內建繁體中文字型 fallback。
- 支援 EPUB 橫排 / 直排閱讀。
- 直排圖片獨立成頁，版面更穩定。
- 改善章節與圖片快取速度。
- 支援閱讀字級、行距、字距數字化調整。

## 安裝方式

### 線上燒錄

使用桌面版 Chrome 或 Microsoft Edge 開啟 GitHub Pages 安裝頁，接上 Paper S3，按下「安裝 / 更新韌體」。

### 手動燒錄

```bash
python -m esptool --chip esp32s3 --port COM5 --baud 921600 write_flash -z 0x0 merged-firmware.bin
```

請把 `COM5` 換成你的實際序列埠。

## 升級注意

第一次開啟 EPUB 時會重新建立章節快取；更改字級、行距、字距或直排 / 橫排後，也會重新建立相關章節快取。

## 已知限制

- Wi‑Fi 傳輸、OTA、KOReader Sync 仍建議視為進階功能，發布前請以實機驗證。
- 大型 EPUB 或大量圖片書籍第一次開啟可能需要較長索引時間。
