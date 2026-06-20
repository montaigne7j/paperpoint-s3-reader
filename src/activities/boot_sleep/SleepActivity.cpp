#include "SleepActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <HalDisplay.h>
#include <SleepImageManager.h>
#include <Txt.h>
#include <Xtc.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "images/Logo120.h"

void SleepActivity::onEnter() {
  Activity::onEnter();

  /*
   * 睡眠圖片固定使用直式座標。
   *
   * 不先顯示 Going to sleep，避免：
   * 1. 使用者等待提示畫面；
   * 2. 電子紙多做一次不必要的畫面切換；
   * 3. 提示框殘留在最終睡眠圖片中。
   */
  const bool rotate180 = SETTINGS.sleepScreenRotate180 != 0;

  renderer.setOrientation(
      rotate180
          ? GfxRenderer::Orientation::PortraitInverted
          : GfxRenderer::Orientation::Portrait
  );

  LOG_DBG(
      "SLP",
      "Sleep screen rotation: %s",
      rotate180 ? "180" : "0"
  );

  switch (SETTINGS.sleepScreen) {
    case CrossPointSettings::
        SLEEP_SCREEN_MODE::BLANK:
      return renderBlankSleepScreen();

    case CrossPointSettings::
        SLEEP_SCREEN_MODE::CUSTOM:
      return renderCustomSleepScreen();

    case CrossPointSettings::
        SLEEP_SCREEN_MODE::COVER:
      return renderCoverSleepScreen();

    case CrossPointSettings::
        SLEEP_SCREEN_MODE::COVER_CUSTOM:
      if (APP_STATE.lastSleepFromReader) {
        return renderCoverSleepScreen();
      }

      return renderCustomSleepScreen();

    default:
      return renderDefaultSleepScreen();
  }
}

void SleepActivity::renderCustomSleepScreen() const {
#if CROSSPOINT_PAPERS3
  // JPG/PNG/arbitrary-size BMP files are converted ahead of time by the
  // low-priority SleepImageManager task. Sleeping never waits for decoding:
  // use the prepared cache, then the previous valid cache, then the built-in
  // fallback.
  if (SleepImages.displayPreparedOrPrevious(
          display,
          APP_STATE.lastSleepFromReader,
          SETTINGS.sleepScreenRotate180 != 0,
          SETTINGS.transparentSleepPngBackground)) {
    return;
  }

  renderDefaultSleepScreen();
  return;
#else
  // Check if we have a /.sleep (preferred) or /sleep directory
  const char* sleepDir = nullptr;
  auto dir = Storage.open("/.sleep");
  if (dir && dir.isDirectory()) {
    sleepDir = "/.sleep";
  } else {
    dir = Storage.open("/sleep");
    if (dir && dir.isDirectory()) {
      sleepDir = "/sleep";
    }
  }

  if (sleepDir) {
    std::vector<std::string> files;
    char name[500];
    // collect all valid BMP files
    for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
      if (file.isDirectory()) {
        continue;
      }
      file.getName(name, sizeof(name));
      auto filename = std::string(name);
      if (filename[0] == '.') {
        continue;
      }

      if (!FsHelpers::hasBmpExtension(filename)) {
        LOG_DBG("SLP", "Skipping non-.bmp file name: %s", name);
        continue;
      }
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() != BmpReaderError::Ok) {
        LOG_DBG("SLP", "Skipping invalid BMP file: %s", name);
        continue;
      }
      files.emplace_back(filename);
    }
    const auto numFiles = files.size();
    if (numFiles > 0) {
      // Pick a random wallpaper, excluding recently shown ones.
      // Window: up to SLEEP_RECENT_COUNT entries, capped at numFiles-1.
      const uint16_t fileCount = static_cast<uint16_t>(std::min(numFiles, static_cast<size_t>(UINT16_MAX)));
      const uint8_t window =
          static_cast<uint8_t>(std::min(static_cast<size_t>(APP_STATE.recentSleepFill), numFiles - 1));
      auto randomFileIndex = static_cast<uint16_t>(random(fileCount));
      for (uint8_t attempt = 0; attempt < 20 && APP_STATE.isRecentSleep(randomFileIndex, window); attempt++) {
        randomFileIndex = static_cast<uint16_t>(random(fileCount));
      }
      APP_STATE.pushRecentSleep(randomFileIndex);
      APP_STATE.saveToFile();
      const auto filename = std::string(sleepDir) + "/" + files[randomFileIndex];
      FsFile file;
      if (Storage.openFileForRead("SLP", filename, file)) {
        LOG_DBG("SLP", "Randomly loading: %s/%s", sleepDir, files[randomFileIndex].c_str());
        delay(100);
        Bitmap bitmap(file, true);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          renderBitmapSleepScreen(bitmap);
          return;
        }
      }
    }
  }
  // Look for sleep.bmp on the root of the sd card to determine if we should
  // render a custom sleep screen instead of the default.
  FsFile file;
  if (Storage.openFileForRead("SLP", "/sleep.bmp", file)) {
    Bitmap bitmap(file, true);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_DBG("SLP", "Loading: /sleep.bmp");
      renderBitmapSleepScreen(bitmap);
      return;
    }
  }

  renderDefaultSleepScreen();
#endif
}

void SleepActivity::renderDefaultSleepScreen() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawImage(Logo120, (pageWidth - 120) / 2, (pageHeight - 120) / 2, 120, 120);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, tr(STR_CROSSPOINT), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, tr(STR_SLEEPING));

  // Make sleep screen dark unless light is selected in settings
  if (SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT) {
    renderer.invertScreen();
  }

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void SleepActivity::renderBitmapSleepScreen(
    const Bitmap& bitmap
) const {
  const auto pageWidth =
      renderer.getScreenWidth();

  const auto pageHeight =
      renderer.getScreenHeight();

#if CROSSPOINT_PAPERS3
  /*
   * GC16 第一版只處理：
   *
   *   540 × 960
   *   24-bit / 32-bit
   *   No filter
   *
   * 其他尺寸、1-bit、2-bit、反相黑白，
   * 自動退回原本 renderer 路徑。
   */
  const uint16_t bitmapBpp =
      bitmap.getBpp();

  const bool gc16Eligible =
      SETTINGS.sleepScreenCoverFilter ==
          CrossPointSettings::
              SLEEP_SCREEN_COVER_FILTER::
                  NO_FILTER &&
      bitmap.getWidth() ==
          pageWidth &&
      bitmap.getHeight() ==
          pageHeight &&
      (
          bitmapBpp == 24 ||
          bitmapBpp == 32
      );

  if (gc16Eligible) {
    LOG_INF(
        "SLP",
        "Rendering GC16 sleep bitmap: "
        "%dx%d, %u bpp",
        bitmap.getWidth(),
        bitmap.getHeight(),
        static_cast<unsigned>(
            bitmapBpp
        )
    );

    const bool gc16Success =
        renderer.displayGc16Bitmap(
            bitmap,
            true,
            HalDisplay::
                Gc16DitherMode::
                    FloydSteinberg,
            SETTINGS.sleepScreenRotate180 != 0
        );

    if (gc16Success) {
      LOG_INF(
          "SLP",
          "GC16 sleep bitmap completed"
      );

      /*
       * 到此不可再執行任何一般
       * renderer.displayBuffer()。
       *
       * main.cpp 接下來會呼叫
       * display.deepSleep()。
       */
      return;
    }

    LOG_ERR(
        "SLP",
        "GC16 sleep bitmap failed; "
        "falling back to standard renderer"
    );

    /*
     * GC16 嘗試期間 Bitmap 的檔案位置
     * 可能已經移到 pixel data 結尾。
     *
     * 退回普通 renderer 前必須 rewind。
     */
    if (bitmap.rewindToData() !=
        BmpReaderError::Ok) {
      LOG_ERR(
          "SLP",
          "Failed to rewind bitmap "
          "after GC16 failure"
      );

      renderDefaultSleepScreen();
      return;
    }
  }
#endif

  int x = 0;
  int y = 0;

  float cropX = 0;
  float cropY = 0;

  LOG_DBG("SLP", "bitmap %d x %d, screen %d x %d", bitmap.getWidth(), bitmap.getHeight(), pageWidth, pageHeight);
  if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
    // image will scale, make sure placement is right
    float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
    const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

    LOG_DBG("SLP", "bitmap ratio: %f, screen ratio: %f", ratio, screenRatio);
    if (ratio > screenRatio) {
      // image wider than viewport ratio, scaled down image needs to be centered vertically
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropX = 1.0f - (screenRatio / ratio);
        LOG_DBG("SLP", "Cropping bitmap x: %f", cropX);
        ratio = (1.0f - cropX) * static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
      }
      x = 0;
      y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
      LOG_DBG("SLP", "Centering with ratio %f to y=%d", ratio, y);
    } else {
      // image taller than viewport ratio, scaled down image needs to be centered horizontally
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropY = 1.0f - (ratio / screenRatio);
        LOG_DBG("SLP", "Cropping bitmap y: %f", cropY);
        ratio = static_cast<float>(bitmap.getWidth()) / ((1.0f - cropY) * static_cast<float>(bitmap.getHeight()));
      }
      x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
      y = 0;
      LOG_DBG("SLP", "Centering with ratio %f to x=%d", ratio, x);
    }
  } else {
    // center the image
    x = (pageWidth - bitmap.getWidth()) / 2;
    y = (pageHeight - bitmap.getHeight()) / 2;
  }

  LOG_DBG(
    "SLP",
    "drawing to %d x %d",
    x,
    y
);

const uint32_t renderStart = millis();

/*
 * 除非使用者選擇黑白或反相黑白 filter，
 * 否則 Custom／Cover BMP 一律使用原生 4 階灰階。
 *
 * 不再依賴 bitmap.hasGreyscale() 或 is1Bit()，
 * 因為 24-bit 灰階 BMP 可能沒有被這些函式
 * 判定成「灰階格式」。
 */
const bool useDirectGrayscale =
    SETTINGS.sleepScreenCoverFilter ==
    CrossPointSettings::
        SLEEP_SCREEN_COVER_FILTER::
            NO_FILTER;

LOG_DBG(
    "SLP",
    "Sleep render mode: filter=%u directGray=%d",
    static_cast<unsigned>(
        SETTINGS.sleepScreenCoverFilter
    ),
    useDirectGrayscale ? 1 : 0
);

/*
 * 睡眠畫面清洗開關。
 *
 * true：
 *   先實際顯示全白，再顯示睡眠圖片。
 *   比較慢，但殘影較少。
 *
 * false：
 *   只刷新一次睡眠圖片。
 */
constexpr bool CLEAN_SLEEP_REFRESH = true;

if (CLEAN_SLEEP_REFRESH) {
  renderer.setRenderMode(
      GfxRenderer::BW
  );

  // 先建立純白 framebuffer。
  renderer.clearScreen(0xFF);

  // 實際讓面板顯示純白，洗掉上一畫面。
  renderer.displayBuffer(
      HalDisplay::FULL_REFRESH
  );

  // 讓白色清洗波形穩定一下。
  delay(150);
}

/*
 * 建立最終睡眠圖片 framebuffer。
 */
renderer.clearScreen(0xFF);

/*
 * 每次 drawBitmap 前都必須把檔案位置
 * 重設到 BMP pixel data 開始處。
 */
bitmap.rewindToData();

if (useDirectGrayscale) {
  renderer.setRenderMode(
      GfxRenderer::GRAYSCALE_DIRECT
  );
} else {
  renderer.setRenderMode(
      GfxRenderer::BW
  );
}

renderer.drawBitmap(
    bitmap,
    x,
    y,
    pageWidth,
    pageHeight,
    cropX,
    cropY
);

/*
 * 只有非灰階路徑才執行黑白反相。
 */
if (!useDirectGrayscale &&
    SETTINGS.sleepScreenCoverFilter ==
        CrossPointSettings::
            SLEEP_SCREEN_COVER_FILTER::
                INVERTED_BLACK_AND_WHITE) {
  renderer.invertScreen();
}

/*
 * 最終睡眠圖片使用完整、高品質刷新。
 */
renderer.displayBuffer(
    HalDisplay::FULL_REFRESH
);

renderer.setRenderMode(
    GfxRenderer::BW
);

LOG_DBG(
    "SLP",
    "Sleep screen completed in %lu ms "
    "(directGray=%d clean=%d)",
    millis() - renderStart,
    useDirectGrayscale ? 1 : 0,
    CLEAN_SLEEP_REFRESH ? 1 : 0
);

/*
 * 到此直接結束。
 * 函式後面不能再有第二組 drawBitmap、
 * GRAYSCALE_LSB、GRAYSCALE_MSB 或 displayGrayBuffer。
 */
return;

}

void SleepActivity::renderCoverSleepScreen() const {
  void (SleepActivity::*renderNoCoverSleepScreen)() const;
  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      renderNoCoverSleepScreen = &SleepActivity::renderCustomSleepScreen;
      break;
    default:
      renderNoCoverSleepScreen = &SleepActivity::renderDefaultSleepScreen;
      break;
  }

  if (APP_STATE.openEpubPath.empty()) {
    return (this->*renderNoCoverSleepScreen)();
  }

  std::string coverBmpPath;
  bool cropped = SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP;

  // Check if the current book is XTC, TXT, or EPUB
  if (FsHelpers::hasXtcExtension(APP_STATE.openEpubPath)) {
    // Handle XTC file
    Xtc lastXtc(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastXtc.load()) {
      LOG_ERR("SLP", "Failed to load last XTC");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastXtc.generateCoverBmp()) {
      LOG_ERR("SLP", "Failed to generate XTC cover bmp");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastXtc.getCoverBmpPath();
  } else if (FsHelpers::hasTxtExtension(APP_STATE.openEpubPath)) {
    // Handle TXT file - looks for cover image in the same folder
    Txt lastTxt(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastTxt.load()) {
      LOG_ERR("SLP", "Failed to load last TXT");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastTxt.generateCoverBmp()) {
      LOG_ERR("SLP", "No cover image found for TXT file");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastTxt.getCoverBmpPath();
  } else if (FsHelpers::hasEpubExtension(APP_STATE.openEpubPath)) {
    // Handle EPUB file
    Epub lastEpub(APP_STATE.openEpubPath, "/.crosspoint");
    // Skip loading css since we only need metadata here
    if (!lastEpub.load(true, true)) {
      LOG_ERR("SLP", "Failed to load last epub");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastEpub.generateCoverBmp(cropped)) {
      LOG_ERR("SLP", "Failed to generate cover bmp");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastEpub.getCoverBmpPath(cropped);
  } else {
    return (this->*renderNoCoverSleepScreen)();
  }

  FsFile file;
  if (Storage.openFileForRead("SLP", coverBmpPath, file)) {
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_DBG("SLP", "Rendering sleep cover: %s", coverBmpPath.c_str());
      renderBitmapSleepScreen(bitmap);
      return;
    }
  }

  return (this->*renderNoCoverSleepScreen)();
}

void SleepActivity::renderBlankSleepScreen() const {
  renderer.clearScreen();
}
