# PaperPoint S3 Reader（中文優先版）

**PaperPoint S3 Reader** 是基於 [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) 的 **M5Stack Paper S3 / M5Paper S3** 移植版本。

這個 fork 的維護方向是 **中文優先**：主要目標是讓繁體中文使用者可以舒服地在 M5Paper S3 上閱讀 EPUB、TXT、圖片與自訂睡眠封面。原專案的多語系架構仍可能保留，但本 fork 不承諾維護所有語言翻譯；後續功能、文件、測試與介面調整會以中文閱讀體驗為主。

專案使用 **PlatformIO** 編譯，目標晶片為 **ESP32-S3**，搭配 Paper S3 的 960x540 電子紙螢幕、GT911 觸控、SD 卡與 AXP2101 電源管理。

## 快速連結

- [使用說明與畫面圖解](USER_GUIDE.md)
- [線上燒錄頁](docs/install/index.html)
- [Release 發布前檢查](RELEASE_AUDIT.md)
- [授權與二進位發布合規檢查表](RELEASE_COMPLIANCE_CHECKLIST.md)

## 目前版本與本版重點

- 目前 GitHub 發布版本：**1.9.0**（整合內部修訂至 **r34i**）。
- 支援 EPUB 2/3、TXT、圖片，以及以 `.zip` / `.cbz` 封裝的漫畫圖片閱讀。
- **電量顯示穩定化**：改用 Paper S3 USB 偵測腳位判斷外部供電，電池 ADC 採多次取樣與平滑追蹤；USB 插拔後會先等待電壓穩定，避免百分比瞬間大幅跳動。
- **BMI270 閱讀方向感應**：提供固定正向、固定 180° 與自動 0°/180°；使用前須完成四姿勢校正。閱讀內容、背景、狀態列、閱讀選單與觸控座標會同步旋轉。
- **閱讀感應上鎖**僅能在固定方向模式使用；自動方向模式會強制關閉上鎖，未校正時不執行方向偵測或感應上鎖。
- **漫畫閱讀改善**：專用 CBZ/ZIP 瀏覽與閱讀、自然排序、下一頁預載、預載期間 pending 翻頁、漫畫全刷頻率與選單觸控位置修正。
- **休眠圖片選擇器**：支援 `/.sleep` 與 `/cover` 多層資料夾、隨機或固定圖片、圖片預覽及透明 PNG 融合；背景會在閒置時預先建立快取。
- **閱讀排版**：支援 `/font` 與 `/fonts`、外部 TTF/OTF/BIN、負字距、更寬行距、段落首行空兩個字、閱讀背景 PNG、背景淡化及文字輔助線。
- **檔案瀏覽導覽**：非根目錄第一列固定顯示 `..`；短按返回回上一層，在 `/` 才回首頁；長按返回可從任意層級直接回首頁。
- 閱讀畫面左上角保留 **64×64 隱藏關機熱區**，不顯示圖示且優先於翻頁區；0°/180° 時會跟隨邏輯畫面方向。
- 穩定性優先：停用 Paper S3 閒置降至 80 MHz，並加強 ZIP 解壓、圖片解碼、觸控排隊、背景快取與 watchdog cooperative yield。
- 提供 GitHub Actions 自動編譯、Release 合規附件與瀏覽器線上燒錄頁。

完整更新內容請參閱 [CHANGELOG.md](CHANGELOG.md) 與 [V1.9.0 Release Notes](RELEASE_NOTES_v1.9.0.md)。

## 線上燒錄

如果 GitHub Pages 已啟用，使用者可以直接用 Chrome 或 Microsoft Edge 開啟安裝頁，接上 M5Paper S3 後按下安裝按鈕，不需要 VS Code、PlatformIO 或 PowerShell。

安裝頁網址通常是：

```text
https://montaigne7j.github.io/paperpoint-s3-reader/install/
```

使用方式：

1. 使用支援 Web Serial 的桌面版 Chrome 或 Microsoft Edge。
2. 用 USB-C 傳輸線接上 M5Paper S3。
3. 長按 Paper S3 的電源鍵，讓裝置進入可燒錄狀態。
4. 打開安裝頁並按下「安裝 / 更新韌體」。
5. 選擇名稱包含 `USB JTAG/serial debug unit` 的 M5Paper S3 序列埠。
6. 等待燒錄完成後重新啟動裝置。

若沒有看到序列埠或連線失敗，請確認使用的是可傳輸資料的 USB-C 線，重新長按電源鍵開機，或拔插 USB-C 後再試一次。

## 自行編譯

### 需求

- Python 3
- PlatformIO Core
- USB-C 傳輸線
- M5Paper S3

### 取得專案

```sh
git clone --recursive https://github.com/montaigne7j/paperpoint-s3-reader.git
cd paperpoint-s3-reader
```

### 編譯

```sh
pio run -e default
```

### 編譯並上傳

```sh
pio run -e default -t upload
```

### 監看序列輸出

```sh
pio device monitor
```

## 手動燒錄

如果你從 GitHub Actions 或 Releases 下載到 `merged-firmware.bin`，可以用 `esptool` 從 `0x0` 燒錄：

```sh
python -m pip install esptool
python -m esptool --chip esp32s3 --port COM5 --baud 921600 write_flash -z 0x0 merged-firmware.bin
```

請把 `COM5` 換成你的實際序列埠。macOS / Linux 通常會像：

```text
/dev/ttyACM0
/dev/cu.usbmodemXXXX
```

如果燒錄不穩，請把 baud rate 改成 `460800` 或 `115200`。

## 休眠圖片

自訂休眠圖片可放在 SD 卡的：

```text
/.sleep/
/cover/
```

兩個資料夾都可再建立下一層或多層子資料夾。進入 `設定 > 顯示 > 自訂休眠圖片` 後，可選擇「隨機」、進入 `/.sleep` 或 `/cover`，再像選擇字型一樣逐層瀏覽。圖片列先點一次選取，再點第二次開啟預覽；預覽下方提供「取消 / 確認」，兩者都會回到原圖片資料夾，確認會儲存該圖片。

Paper S3 版本支援：

```text
.bmp
.jpg / .jpeg
.png
```

圖片不必預先製作成 `540×960`。不透明圖片會保持比例並以中央裁切方式填滿螢幕；含實際透明像素的 PNG 則會完整縮放、置中並保留 Alpha，可與目前閱讀頁或白色背景融合。

「隨機」會從 `/.sleep`、`/cover` 與舊版 `/sleep` 內遞迴挑選；自選圖片則固定使用該檔案，直到切回隨機。裝置會在閒置時於背景預先建立 GC16 快取。關機時不等待圖片解碼：優先使用本次已完成快取，其次使用上一張有效快取，最後使用內建休眠圖。

完整規格、限制、快取格式與診斷 LOG 請參閱 [docs/development-notes/SLEEP_IMAGE_CACHE_README.md](docs/development-notes/SLEEP_IMAGE_CACHE_README.md)。

## 硬體資訊

| 項目 | M5Paper S3 |
|---|---|
| MCU | ESP32-S3，雙核心，240 MHz |
| Flash / PSRAM | 16 MB / 8 MB OPI |
| 螢幕 | 960x540 parallel e-ink，IT8951 |
| 觸控 | GT911 電容觸控 |
| SD 卡 | SPI，CS GPIO47 |
| 電源 | Li-Po，AXP2101 PMIC |
| RTC | BM8563 |

穩定性優先版本已停用 Paper S3 閒置時降到 80 MHz 的方式；主頻維持開機正常值，省電改由自動休眠／關機流程處理。

## 主要功能

- EPUB 2/3 解析與閱讀。
- TXT 閱讀。
- CBZ / ZIP 圖片漫畫閱讀，包含自然排序、預載與漫畫專用全刷頻率。
- 圖片與封面顯示。
- 閱讀設定可啟用「段落首行空兩個字」，EPUB 橫排／直排與 TXT 都會重新分頁並套用兩個全形空格。
- 檔案瀏覽器與最近閱讀清單；支援 `..`、短按返回上一層及長按返回首頁。
- 閱讀進度記錄。
- 可調整字體、版面、顯示與睡眠設定。
- TTF 字型 FreeType PSRAM allocator、glyph cache miss 防護與 idle glyph prewarm。
- 開機後翻頁 darker pass counter，讓初期與穩定後的黑色刷新 pass 可分開調整。
- 大字介面主題。
- 直接觸控選取首頁、設定分頁、清單與多封面首頁項目。
- 閱讀狀態列跟隨頁邊距模式，並自動保留狀態列與正文之間的安全間距。
- 內建雙語 EPUB 使用手冊。
- Wi-Fi 書籍上傳（未驗證功能）。
- OTA 韌體更新。
- KOReader Sync 整合。
- Paper S3 專用觸控與底部按鈕導覽。
- BMI270 四姿勢校正、固定／自動 0°/180° 閱讀方向與固定方向感應上鎖。
- USB 插拔補償與平滑電量百分比顯示。

## 操作方式

### 一般畫面

除了閱讀頁之外，多數畫面會在底部顯示操作按鈕：

```text
+--------+---------+--------+--------+
|  Back  | Select  |  Prev  |  Next  |
+--------+---------+--------+--------+
```

| 按鈕 | 功能 |
|---|---|
| Back | 一般畫面返回或離開；檔案瀏覽中短按回上一層、長按直接回首頁 |
| Select | 選擇或確認目前項目 |
| Prev | 上一頁清單或上一頁內容 |
| Next | 下一頁清單或下一頁內容 |

### 閱讀頁

閱讀頁使用全螢幕觸控區域：

| 區域 | 功能 |
|---|---|
| 左上角 64×64 | 隱藏關機熱區，不顯示圖示；優先於翻頁觸控 |
| 左側 | 上一頁 |
| 中間 | 開啟閱讀設定選單 |
| 右側 | 下一頁 |

固定 180° 或自動旋轉至 180° 時，閱讀內容、背景、選單、狀態列及觸控區會一起旋轉。

閱讀頁手勢：

| 手勢 | 功能 |
|---|---|
| 雙指點擊 | 離開閱讀頁 |

## 內部資料

CrossPoint 會把章節資料快取到 SD 卡的 `.crosspoint/`，以降低 RAM 使用量。

```text
.crosspoint/
  epub_<hash>/
    progress.bin
    cover.bmp
    book.bin
    sections/
      0.bin
      1.bin
      ...
```

刪除 `.crosspoint/` 可以清除所有快取。

## GitHub Actions

本專案包含自動化流程：

- `CI (build)`：編譯韌體並輸出 `firmware.bin` 與 `merged-firmware.bin`。
- `Compile Release`：建立 tag 時產生 release 韌體。
- `Build Web Installer`：產生瀏覽器燒錄用的 `merged-firmware.bin`，並部署到 GitHub Pages。

`merged-firmware.bin` 是最適合一般使用者的燒錄檔，因為它已經包含 bootloader、partition table、boot app 與 firmware，可直接從 `0x0` 寫入。

## 與原專案的關係

PaperPoint S3 Reader 是 CrossPoint Reader 的 M5Paper S3 移植 fork。這個 repo 會保留原專案 attribution，但名稱與維護目標會與原專案區分：這裡主要照顧 Paper S3 硬體與中文使用情境。

## 致謝

- 原始專案：[crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader)
- 顯示驅動：[EPD_Painter](https://github.com/tonywestonuk/EPD_Painter)
- 靈感來源：[diy-esp32-epub-reader by atomic14](https://github.com/atomic14/diy-esp32-epub-reader)

## Licensing and compliant releases

The project source is primarily MIT-licensed, but bundled libraries, generated
font data, hyphenation data, and visual assets retain their own licences. See:
- `BUILTIN_CJK_FONT.md` — 內建繁中字型來源、轉換方式、大小與 OFL 合規說明。

- `THIRD_PARTY_NOTICES.md` and `LICENSES/`
- `HYPHENATION_LICENSES.md`
- `ASSETS_LICENSES.md`
- `BINARY_RELEASE_LGPL_COMPLIANCE.md`
- `RELEASE_COMPLIANCE_CHECKLIST.md`

Do not publish a firmware binary by itself. Public releases must also publish
the matching application source archive, exact LGPL component source archive,
SPDX SBOM, licence bundle, and LGPL relink kit generated by the release
workflow.


## Reader background PNG and guide lines

V1.8.4 adds an optional decorative reading background. Place PNG files in `/bg` on the SD card and enable **Reader background PNG** in Settings. The selector supports PNG files directly inside `/bg` and one nested level, for example `/bg/ink/reading.png`. Use **Reader background fade** to fade the selected PNG toward white from 0% to 90% in 10% steps before dithering. V1.8.4-r23 caches the decoded background framebuffer in PSRAM, so the same background and fade level is reused across page turns instead of decoding the PNG for every cached page.

The reader also supports optional light guide lines behind text: solid, dashed, or dotted. In horizontal layout the lines are drawn below text rows; in vertical layout the lines are drawn to the left of text columns. The guide-line pattern is coordinate-independent, so 1px faint lines remain visible even when rows or columns land on odd pixel coordinates.
