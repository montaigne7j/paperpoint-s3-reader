# PaperPoint S3 Reader（中文優先版）

**PaperPoint S3 Reader** 是基於 [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) 的 **M5Stack Paper S3 / M5Paper S3** 移植版本。

這個 fork 的維護方向是 **中文優先**：主要目標是讓繁體中文使用者可以舒服地在 M5Paper S3 上閱讀 EPUB、TXT、XTC、圖片與自訂睡眠封面。原專案的多語系架構仍可能保留，但本 fork 不承諾維護所有語言翻譯；後續功能、文件、測試與介面調整會以中文閱讀體驗為主。

專案使用 **PlatformIO** 編譯，目標晶片為 **ESP32-S3**，搭配 Paper S3 的 960x540 電子紙螢幕、GT911 觸控、SD 卡與 AXP2101 電源管理。

## 目前重點

- 中文優先的 M5Paper S3 電子紙閱讀器韌體。
- 針對 M5Paper S3 的顯示、觸控、SD 卡與電源流程調整。
- 支援 EPUB 2/3 閱讀、最近書籍、閱讀進度、封面與睡眠畫面。
- 自訂休眠圖片支援 BMP、JPG/JPEG、PNG，並會在閒置時預先建立電子紙快取。
- 透明 PNG 休眠圖可選擇疊在目前閱讀頁面或白色背景上。
- 支援 Wi-Fi 上傳書籍、OTA 更新與 KOReader Sync。
- 支援可調整閱讀字體、版面、顯示與睡眠設定。
- 支援電子紙灰階顯示，適合封面與睡眠圖片。
- 提供 GitHub Actions 自動編譯與瀏覽器線上燒錄頁。

## 最新更新

- 休眠圖片不再限於 `540×960` BMP；`.bmp`、`.jpg`、`.jpeg`、`.png` 可放在 `/.sleep/`，裝置會自動縮放、中央裁切或保留透明圖完整比例。
- Paper S3 會在閒置時背景產生 GC16 快取，按下關機後不等待圖片解碼；若新快取尚未完成，會使用上一張有效快取或內建休眠圖。
- 新增「透明 PNG 休眠圖背景」設定，可選「目前閱讀頁面」或「白色背景」。從首頁、設定等非閱讀畫面休眠時會固定用白色背景。

## 線上燒錄

如果 GitHub Pages 已啟用，使用者可以直接用 Chrome 或 Microsoft Edge 開啟安裝頁，接上 M5Paper S3 後按下安裝按鈕，不需要 VS Code、PlatformIO 或 PowerShell。

安裝頁網址通常是：

```text
https://montaigne7j.github.io/paperpoint-s3-installer/install/
```

使用方式：

1. 使用支援 Web Serial 的桌面版 Chrome 或 Microsoft Edge。
2. 用 USB-C 傳輸線接上 M5Paper S3。
3. 長按電源鍵，讓 Paper S3 開機並被電腦偵測。
4. 打開安裝頁並按下「安裝 / 更新韌體」。
5. 選擇名稱包含 `USB JTAG/serial debug unit` 的序列埠。
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
```

Paper S3 版本支援：

```text
.bmp
.jpg / .jpeg
.png
```

圖片不必預先製作成 `540×960`。不透明圖片會保持比例並以中央裁切方式填滿螢幕；含實際透明像素的 PNG 則會完整縮放、置中並保留 Alpha，可與目前閱讀頁或白色背景融合。

裝置會在閒置時於背景預先建立 GC16 快取。關機時不等待圖片解碼：優先使用本次已完成快取，其次使用上一張有效快取，最後使用內建休眠圖。

完整規格、限制、快取格式與診斷 LOG 請參閱 [SLEEP_IMAGE_CACHE_README.md](SLEEP_IMAGE_CACHE_README.md)。

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

## 主要功能

- EPUB 2/3 解析與閱讀。
- TXT / XTC 閱讀。
- 圖片與封面顯示。
- BMP / JPG / PNG 自訂休眠圖片，含透明 PNG 休眠 Overlay 與背景快取。
- 檔案瀏覽器與最近閱讀清單。
- 閱讀進度記錄。
- 可調整字體、版面、顯示與睡眠設定。
- Wi-Fi 書籍上傳。
- OTA 韌體更新。
- KOReader Sync 整合。
- Paper S3 專用觸控與底部按鈕導覽。

## 操作方式

### 一般畫面

除了閱讀頁之外，多數畫面會在底部顯示操作按鈕：

```text
+--------+---------+--------+--------+
|  Back  | Confirm |  Prev  |  Next  |
+--------+---------+--------+--------+
```

| 按鈕 | 功能 |
|---|---|
| Back | 返回或離開目前畫面 |
| Confirm | 選擇或確認目前項目 |
| Prev | 上一頁 |
| Next | 下一頁 |

### 閱讀頁

閱讀頁使用全螢幕觸控區域：

| 區域 | 功能 |
|---|---|
| 左側 | 上一頁 |
| 中間 | 開啟閱讀設定選單 |
| 右側 | 下一頁 |

閱讀頁手勢：

| 手勢 | 功能 |
|---|---|
| 雙指點擊 | 離開閱讀頁 |
| 向上滑 | 上一頁 |
| 向下滑 | 下一頁 |

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
