# Paper S3 自訂休眠圖片與背景快取

本功能讓 Paper S3 的自訂休眠畫面支援 BMP、JPG/JPEG 與 PNG，並在裝置閒置時預先轉換成 GC16 快取，避免按下關機後才等待圖片解碼與縮放。

## 放置位置

優先掃描：

```text
/.sleep/
```

若不存在或沒有支援的圖片，再掃描：

```text
/sleep/
```

若兩個資料夾都沒有圖片，最後檢查 SD 卡根目錄：

```text
/sleep.bmp
/sleep.jpg
/sleep.jpeg
/sleep.png
```

副檔名不分大小寫。

## 支援格式

- 未壓縮 BMP：1、2、4、8、24、32 bpp。
- 一般 RGB 或灰階 JPG/JPEG。
- 常見 8-bit 灰階、RGB、索引色、灰階 Alpha、RGBA PNG。

目前不保證支援 CMYK JPEG、16-bit PNG、APNG、WebP、GIF 或損壞的圖片檔。JPEG EXIF 方向目前不會自動套用，圖片也不會因為是橫向而自動旋轉 90°。

## 尺寸與版面規則

目標畫面固定為：

```text
540 × 960（9:16）
```

來源圖片不需要是相同解析度。

### 不透明 JPG、PNG、BMP

```text
保持比例
→ 以圖片中央為基準裁切成 9:16
→ 縮放至 540×960
→ 轉為 GC16 16 階灰階
```

- 圖片太寬時裁掉左右兩側。
- 圖片太窄時裁掉上下兩側。
- 不留白邊、不拉伸變形、不自動旋轉 90°。

### 透明 PNG

只有實際包含透明或半透明像素的 PNG 才會被視為 Overlay；雖然帶 Alpha channel、但所有像素都完全不透明的 PNG，仍會使用一般中央裁切流程。

```text
保持比例
→ 完整縮放至 540×960 範圍內
→ 不裁切
→ 水平與垂直置中
→ 保留灰階與 Alpha
```

透明區域的背景由設定控制：

```text
設定 → 顯示 → 透明 PNG 休眠圖背景
├─ 目前閱讀頁面
└─ 白色背景
```

- 從閱讀正文或閱讀選單關機時，可將 Overlay 與最後一張完整閱讀頁融合。
- 從首頁、設定、檔案瀏覽器等非閱讀畫面關機時，一律改用白色背景，不會把選單畫面留在電子紙上。
- 若閱讀方向為橫向，閱讀頁會完整縮放並置中於直式休眠畫面，不會拉伸。

半透明像素使用 Alpha blending，不是單純保留舊電子紙內容。完成合成後仍會執行一次完整 GC16 更新。

## 背景預處理

開機完成後會先隨機選定下一張休眠圖。裝置閒置約 3 秒、且前景沒有進行畫面更新時，建立低優先級背景工作：

```text
驗證來源圖片
→ 解碼
→ 裁切或完整縮放
→ 灰階與抖動處理
→ 寫入 .tmp
→ 完成並驗證後 rename 為正式 .sgc
```

使用者操作後，背景轉換會暫停約 1.2 秒，讓閱讀、翻頁與 UI 操作優先。每次開機最多執行一輪準備；若所有圖片都失敗，下一次開機才重新嘗試，避免裝置閒置時持續重複解碼壞檔。

## 關機時不等待

按下關機後不會等待尚未完成的 JPG／PNG 處理：

```text
1. 本次預選圖片快取已完成 → 使用新快取
2. 尚未完成或失敗         → 使用上一張有效快取
3. 沒有上一張有效快取     → 使用內建休眠圖
```

未完成的 `.tmp` 永遠不會拿來顯示，並會在下次開機時清除。

## 180° 旋轉

既有的「休眠畫面旋轉 180°」仍套用於最終輸出。中央裁切、透明 Overlay 與閱讀頁合成都先在標準 `540×960` 座標完成，最後才做 180° 映射。

## 快取位置

```text
/.crosspoint/sleepcache/
├─ AAAAAAAA_BBBBBBBB.sgc
├─ ...
└─ last.txt
```

- `.sgc`：Paper S3 專用 GC16 或 Overlay 快取。
- `last.txt`：上一張成功顯示的有效快取路徑。
- `.tmp`：背景工作尚未完成的暫存檔，不會被顯示。

快取會檢查格式版本、畫面尺寸、資料長度與 payload fingerprint。來源路徑、檔案大小及頭尾內容 fingerprint 共同決定快取名稱；替換同名圖片後會建立新快取。

清除 `/.crosspoint/sleepcache/` 可強制重新建立所有休眠圖片快取。

## 記憶體與尺寸限制

為避免占滿 ESP32-S3 記憶體：

- JPEG 原始寬高上限為 4096，會優先使用 JPEGDEC 的 1/2、1/4 或 1/8 解碼縮放。
- PNG 支援常見 8-bit 格式，寬高上限為 4096，完整解碼工作影像上限約 2.5 百萬像素。
- BMP 沿用 Bitmap 解析器限制，最大約 2048×3072。
- 大型工作 buffer 與閱讀頁快照配置於 PSRAM。

常見的 `1080×1920` JPG／PNG 可處理；超大型 PNG、非常寬的 RGBA PNG或不支援的編碼會跳過，並嘗試下一張圖片。

## 診斷 LOG

正常準備：

```text
[SLPCACHE] Selected next sleep image: /.sleep/photo.jpg
[SLPCACHE] Preparing in background: /.sleep/photo.jpg
[SLPCACHE] Prepared successfully in ... ms: /.crosspoint/sleepcache/....sgc
```

關機使用新快取：

```text
[SLP] Using prepared sleep cache: ...
```

新快取尚未完成，使用上一張：

```text
[SLP] Using previous valid sleep cache: ...
```

沒有可用快取：

```text
[SLP] No valid prepared sleep cache; using built-in fallback
```

透明 PNG：

```text
[SLP] Transparent PNG background: current reading page
```

或：

```text
[SLP] Transparent PNG background: white
```
