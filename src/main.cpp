#include <Arduino.h>
#include <Epub.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <FontManager.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <I18n.h>
#include <Logging.h>
#include <SPI.h>
#include <SleepImageManager.h>
#include <builtinFonts/all.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ButtonNavigator.h"
#include "util/ScreenshotUtil.h"

#include <Bitmap.h>
#if defined(ESP32)
#include <esp_heap_caps.h>
#endif

namespace {

/*
 * GC16 實機測試專用。
 *
 * true：
 *   開機後顯示 16 階灰條，完成後永久停住。
 *
 * false：
 *   正常啟動 CrossPoint。
 */
constexpr bool ENABLE_GC16_BOOT_TEST =
    false;

constexpr bool GC16_TEST_BITMAP =
    false;

constexpr const char*
    GC16_TEST_BITMAP_PATH =
        "/.sleep/"
        "papers3_sleep_540x960.bmp";

}  // namespace

constexpr bool GC16_ENABLE_FLOYD_STEINBERG =
    false;

HalDisplay display;
HalGPIO gpio;
MappedInputManager mappedInputManager(gpio);
GfxRenderer renderer(display);
ActivityManager activityManager(renderer, mappedInputManager);
FontDecompressor fontDecompressor;
FontCacheManager fontCacheManager(renderer.getFontMap());

// Fonts

// Keep only the built-in reader fonts that are useful for PaperPoint's
// Chinese-first reader.  Removed ReaderDyslexic and unused italic / 12 / 18 px
// NotoSans variants to recover flash space for the embedded CJK bitmap font.
EpdFont notosans14RegularFont(&notosans_14_regular);
EpdFont notosans14BoldFont(&notosans_14_bold);
EpdFontFamily notosans14FontFamily(&notosans14RegularFont, &notosans14BoldFont);

EpdFont notosans16RegularFont(&notosans_16_regular);
EpdFont notosans16BoldFont(&notosans_16_bold);
EpdFontFamily notosans16FontFamily(&notosans16RegularFont, &notosans16BoldFont);

EpdFont smallFont(&notosans_8_regular);
EpdFontFamily smallFontFamily(&smallFont);

EpdFont ui10RegularFont(&ubuntu_derivative_paperpoint_10_regular);
EpdFont ui10BoldFont(&ubuntu_derivative_paperpoint_10_bold);
EpdFontFamily ui10FontFamily(&ui10RegularFont, &ui10BoldFont);

EpdFont ui12RegularFont(&ubuntu_derivative_paperpoint_12_regular);
EpdFont ui12BoldFont(&ubuntu_derivative_paperpoint_12_bold);
EpdFontFamily ui12FontFamily(&ui12RegularFont, &ui12BoldFont);

// Embedded Traditional Chinese fallback. The sparse/cropped bitmap data is
// always available, so Chinese UI and EPUB text work without an SD font.
EpdFont paperpointSansTcMediumFont(&paperpoint_sans_tc_15_5_medium);
EpdFontFamily paperpointSansTcFallbackFamily(&paperpointSansTcMediumFont);

// measurement of power button press duration calibration value
unsigned long t1 = 0;
unsigned long t2 = 0;

// Verify power button press duration on wake-up from deep sleep
// Pre-condition: isWakeupByPowerButton() == true
void verifyPowerButtonDuration() {
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP) {
    // Fast path for short press
    // Needed because inputManager.isPressed() may take up to ~500ms to return the correct state
    return;
  }

  // Give the user up to 1000ms to start holding the power button, and must hold for SETTINGS.getPowerButtonDuration()
  const auto start = millis();
  bool abort = false;
  // Subtract the current time, because inputManager only starts counting the HeldTime from the first update()
  // This way, we remove the time we already took to reach here from the duration,
  // assuming the button was held until now from millis()==0 (i.e. device start time).
  const uint16_t calibration = start;
  const uint16_t calibratedPressDuration =
      (calibration < SETTINGS.getPowerButtonDuration()) ? SETTINGS.getPowerButtonDuration() - calibration : 1;

  gpio.update();
  // Needed because inputManager.isPressed() may take up to ~500ms to return the correct state
  while (!gpio.isPressed(HalGPIO::BTN_POWER) && millis() - start < 1000) {
    delay(10);  // only wait 10ms each iteration to not delay too much in case of short configured duration.
    gpio.update();
  }

  t2 = millis();
  if (gpio.isPressed(HalGPIO::BTN_POWER)) {
    do {
      delay(10);
      gpio.update();
    } while (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getHeldTime() < calibratedPressDuration);
    abort = gpio.getHeldTime() < calibratedPressDuration;
  } else {
    abort = true;
  }

  if (abort) {
    // Button released too early. Returning to sleep.
    // IMPORTANT: Re-arm the wakeup trigger before sleeping again
    powerManager.startDeepSleep(gpio);
  }
}

void waitForPowerRelease() {
  gpio.update();
  while (gpio.isPressed(HalGPIO::BTN_POWER)) {
    delay(50);
    gpio.update();
  }
}

// Enter deep sleep mode
void enterDeepSleep() {
  HalPowerManager::Lock powerLock;  // Ensure we are at normal CPU frequency for sleep preparation
#if CROSSPOINT_PAPERS3
  // Direct sleep from the reader does not pass through a pushed menu, so save
  // the current page here as well.
  if (activityManager.isReaderActivity()) {
    RenderLock snapshotLock;
    SleepImages.captureReaderFrame(
        display.getFrameBuffer(),
        static_cast<uint8_t>(renderer.getOrientation()));
  }
#endif
  APP_STATE.lastSleepFromReader = activityManager.isReaderContextActive();
  LOG_DBG("SLP", "Sleep reader context: %d", APP_STATE.lastSleepFromReader ? 1 : 0);
  APP_STATE.saveToFile();
  FontMgr.flushPersistentCaches();

  activityManager.goToSleep();

  // display.deepSleep() 會等待背景 EPD waveform 完整結束，
  // 並依正確順序關閉面板電源；不再依賴固定 delay。
  display.deepSleep();
  LOG_DBG("MAIN", "Power button press calibration value: %lu ms", t2 - t1);
  LOG_DBG("MAIN", "Entering deep sleep");

  powerManager.startDeepSleep(gpio);
}
void setupExternalFonts() {
  // 確保 SD 卡根目錄有 /fonts。
  // 資料夾已存在時可忽略 mkdir 的回傳值。
  Storage.mkdir("/fonts");

  FontManager& fontManager = FontManager::getInstance();

  // 必須先掃描，loadSettings() 才能用保存的檔名
  // 對應到這次掃描出的字型 index。
  fontManager.scanFonts();

  const int fontCount = fontManager.getFontCount();

  LOG_INF(
      "MAIN",
      "External font scan complete: %d font(s)",
      fontCount
  );

  if (fontCount <= 0) {
    LOG_INF(
        "MAIN",
        "No external fonts found in /fonts; using built-in fonts"
    );
    return;
  }

  // 嘗試恢復 /.crosspoint/font_settings.bin 中的選擇。
  fontManager.loadSettings();

  // The embedded PaperPoint Sans TC font is now the default Chinese UI
  // fallback. loadSettings() may still restore an explicitly selected external
  // UI font, but no SD-card font is auto-forced on every boot.
  if (fontManager.isUiFontEnabled() &&
      fontManager.getActiveUiFont() != nullptr) {
    const int uiIndex = fontManager.getUiSelectedIndex();
    const FontInfo* uiInfo = fontManager.getFontInfo(uiIndex);
    if (uiInfo != nullptr) {
      LOG_INF("MAIN", "Saved external UI font active: %s (%dpt, %dx%d)",
              uiInfo->filename, uiInfo->size, uiInfo->width, uiInfo->height);
    }
  } else {
    LOG_INF("MAIN", "Using embedded Traditional Chinese UI fallback");
  }

  // Preserve an explicit "Built-in font" selection. Only choose the first
  // external font on a truly fresh install with no font settings file.
  if (!fontManager.hasLoadedSettings() && !fontManager.isExternalFontEnabled()) {
    // Keep first-boot behaviour fast: prefer an existing bitmap/EPDF reader
    // font. Runtime TTF remains opt-in from Settings.
    int initialFontIndex = 0;
    for (int i = 0; i < fontManager.getFontCount(); ++i) {
      const FontInfo* info = fontManager.getFontInfo(i);
      if (info != nullptr && info->type != FontFileType::TrueType) {
        initialFontIndex = i;
        break;
      }
    }

    LOG_INF("MAIN", "No saved reader font; selecting initial font index %d", initialFontIndex);
    if (fontManager.previewFont(initialFontIndex)) {
      fontManager.saveSettings();
    } else {
      LOG_ERR("MAIN", "Failed to load initial external font; using built-in font");
    }
  }

  const int selectedIndex = fontManager.getSelectedIndex();
  const FontInfo* selectedFont =
      fontManager.getFontInfo(selectedIndex);

  if (selectedFont != nullptr &&
      fontManager.isExternalFontEnabled()) {

    LOG_INF(
        "MAIN",
        "External reader font active: %s "
        "(file=%s, size=%u, cell=%ux%u)",
        selectedFont->name,
        selectedFont->filename,
        selectedFont->size,
        selectedFont->width,
        selectedFont->height
    );
  } else {
    LOG_INF("MAIN", "Built-in reader font active");
  }
}

void setupDisplayAndFonts() {
  display.begin();
  renderer.begin();
  activityManager.begin();
  LOG_DBG("MAIN", "Display initialized");

  // Initialize font decompressor for compressed reader fonts
  if (!fontDecompressor.init()) {
    LOG_ERR("MAIN", "Font decompressor init failed");
  }
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  renderer.setFontCacheManager(&fontCacheManager);
  renderer.insertFont(NOTOSANS_14_FONT_ID, notosans14FontFamily);
  renderer.insertFont(NOTOSANS_16_FONT_ID, notosans16FontFamily);
  renderer.insertFont(UI_10_FONT_ID, ui10FontFamily);
  renderer.insertFont(UI_12_FONT_ID, ui12FontFamily);
  renderer.insertFont(SMALL_FONT_ID, smallFontFamily);
  renderer.setBuiltinFallbackFont(&paperpointSansTcFallbackFamily);
  LOG_INF("MAIN", "Built-in Traditional Chinese fallback active: PaperPoint Sans TC Medium (21x30); status text bottom-aligned");
  LOG_DBG("MAIN", "Fonts setup");
}

void setup() {
  t1 = millis();

  // Always start Serial first — with ARDUINO_USB_CDC_ON_BOOT=1 it's USB CDC.
  Serial.begin(115200);

#if !CROSSPOINT_PAPERS3
  // Other boards still use USB enumeration state while determining the wake-up
  // reason. Paper S3 does not need to block here: its PMIC power-button path
  // proceeds normally even before the USB CDC host finishes enumerating.
  {
    const unsigned long serialWaitStart = millis();
    while (!Serial && (millis() - serialWaitStart) < 2000) {
      delay(10);
    }
  }
#endif

  HalSystem::begin();
  gpio.begin();
  powerManager.begin();
  halClock.begin();

  // SD Card Initialization
  // We need 6 open files concurrently when parsing a new chapter
  if (!Storage.begin()) {
    LOG_ERR("MAIN", "SD card initialization failed");
    setupDisplayAndFonts();
    activityManager.goToFullScreenMessage("SD card error", EpdFontFamily::BOLD);
    return;
  }

  HalSystem::checkPanic();

  SETTINGS.loadFromFile();
  I18N.loadSettings();
  KOREADER_STORE.loadFromFile();
  UITheme::getInstance().reload();
  ButtonNavigator::setMappedInputManager(mappedInputManager);

  // SD 卡已完成初始化，現在可以掃描 /fonts。
  setupExternalFonts();

  switch (gpio.getWakeupReason()) {
    case HalGPIO::WakeupReason::PowerButton:
#if CROSSPOINT_PAPERS3
      // Paper S3: power button is handled by the PMIC, not a GPIO.
      // BTN_POWER is never set in the touch-only HalGPIO, so skip verification.
      LOG_DBG("MAIN", "Wakeup reason: Power button (PMIC)");
#else
      // For normal wakeups, verify power button press duration
      LOG_DBG("MAIN", "Verifying power button press duration");
      verifyPowerButtonDuration();
#endif
      break;
    case HalGPIO::WakeupReason::AfterUSBPower:
      // If USB power caused a cold boot, go back to sleep
      LOG_DBG("MAIN", "Wakeup reason: After USB Power");
      powerManager.startDeepSleep(gpio);
      break;
    case HalGPIO::WakeupReason::AfterFlash:
      // After flashing, just proceed to boot
    case HalGPIO::WakeupReason::Other:
    default:
      break;
  }

  // First serial output only here to avoid timing inconsistencies for power button press duration verification
  LOG_DBG("MAIN", "Starting CrossPoint version " CROSSPOINT_VERSION);

  setupDisplayAndFonts();

  #if CROSSPOINT_PAPERS3
  if (ENABLE_GC16_BOOT_TEST) {
    LOG_INF(
        "GC16",
        "Boot GC16 test mode enabled"
    );

    /*
    * display.begin() 已經完成面板初始化。
    * 稍微等待，讓 Serial log 與先前白畫面刷新穩定。
    */
    delay(250);

    const uint32_t gc16Start =
        millis();

    bool gc16Success = false;

    if (GC16_TEST_BITMAP) {
      FsFile gc16File;

      if (!Storage.openFileForRead(
              "GC16",
              GC16_TEST_BITMAP_PATH,
              gc16File)) {
        LOG_ERR(
            "GC16",
            "Failed to open test BMP: %s",
            GC16_TEST_BITMAP_PATH
        );
      } else {
        Bitmap bitmap(
            gc16File,
            false
        );

        const BmpReaderError parseResult =
            bitmap.parseHeaders();

        if (parseResult !=
            BmpReaderError::Ok) {
          LOG_ERR(
              "GC16",
              "BMP parse failed: %s",
              Bitmap::errorToString(
                  parseResult
              )
          );
        } else {
          gc16Success =
            display.showGc16Bitmap(
                bitmap,
                true,
                GC16_ENABLE_FLOYD_STEINBERG
                    ? HalDisplay::
                          Gc16DitherMode::
                              FloydSteinberg
                    : HalDisplay::
                          Gc16DitherMode::
                              None
            );
        }

        gc16File.close();
      }
    } else {
      gc16Success =
          display.showGc16TestBars(
              true
          );
    }

    LOG_INF(
        "GC16",
        "Boot test finished: success=%d, "
        "time=%lu ms",
        gc16Success ? 1 : 0,
        millis() - gc16Start
    );

    /*
    * 絕對不要繼續執行：
    *
    *   activityManager.goToBoot();
    *   activityManager.goHome();
    *   renderer.displayBuffer();
    *
    * 因為現有 2bpp screenbuffer 不知道面板目前
    * 顯示的是 GC16 圖片。
    */
    LOG_INF(
        "GC16",
        "System halted on GC16 test screen"
    );

    for (;;) {
      /*
      * 不進 deep sleep，也不再更新面板。
      * delay() 會讓 FreeRTOS idle task 運作，
      * 避免 watchdog reset。
      */
      delay(1000);
    }
  }
  #endif

  // Skip the Boot activity. Loading these small state files before selecting
  // the initial activity avoids a redundant boot-logo EPD refresh and lets the
  // first visible screen be Home, Crash Report, or the previously open reader.
  APP_STATE.loadFromFile();
  RECENT_BOOKS.loadFromFile();
#if CROSSPOINT_PAPERS3
  SleepImages.begin();
#endif

  if (HalSystem::isRebootFromPanic()) {
    // If we rebooted from a panic, go to crash report screen to show the panic info
    activityManager.goToCrashReport();
  } else if (APP_STATE.openEpubPath.empty() || !APP_STATE.lastSleepFromReader ||
             mappedInputManager.isPressed(MappedInputManager::Button::Back) || APP_STATE.readerActivityLoadCount > 0) {
    // Boot to home screen if no book is open, last sleep was not from reader, back button is held, or reader activity
    // crashed (indicated by readerActivityLoadCount > 0)
    activityManager.goHome();
  } else {
    // Clear app state to avoid getting into a boot loop if the epub doesn't load
    const auto path = APP_STATE.openEpubPath;
    APP_STATE.openEpubPath = "";
    APP_STATE.readerActivityLoadCount++;
    APP_STATE.saveToFile();
    activityManager.goToReader(path);
  }

#if !CROSSPOINT_PAPERS3
  // Ensure we're not still holding the power button before leaving setup
  // (Paper S3 has no detectable power button GPIO, so skip this)
  waitForPowerRelease();
#endif
}

void loop() {
  static unsigned long maxLoopDuration = 0;
  const unsigned long loopStartTime = millis();
  static unsigned long lastMemPrint = 0;

  gpio.update();

  renderer.setFadingFix(SETTINGS.fadingFix);

  if (Serial && millis() - lastMemPrint >= 10000) {
#if defined(ESP32)
    const uint32_t internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t internalMaxAlloc = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t internalMinFree = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const uint32_t psramMaxAlloc = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    LOG_INF(
        "MEM",
        "Internal Free: %lu bytes, Total: %d bytes, Min Free: %lu bytes, MaxAlloc: %lu bytes; PSRAM Free: %lu bytes, MaxAlloc: %lu bytes",
        static_cast<unsigned long>(internalFree),
        ESP.getHeapSize(),
        static_cast<unsigned long>(internalMinFree),
        static_cast<unsigned long>(internalMaxAlloc),
        static_cast<unsigned long>(psramFree),
        static_cast<unsigned long>(psramMaxAlloc)
    );
#else
    LOG_INF("MEM", "Free: %d bytes, Total: %d bytes, Min Free: %d bytes, MaxAlloc: %d bytes", ESP.getFreeHeap(),
            ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
#endif
    lastMemPrint = millis();
  }

  // Handle incoming serial commands,
  // nb: we use logSerial from logging to avoid deprecation warnings
  if (logSerial.available() > 0) {
    String line = logSerial.readStringUntil('\n');
    if (line.startsWith("CMD:")) {
      String cmd = line.substring(4);
      cmd.trim();
      if (cmd == "SCREENSHOT") {
        logSerial.printf("SCREENSHOT_START:%d\n", HalDisplay::BUFFER_SIZE);
        uint8_t* buf = display.getFrameBuffer();
        logSerial.write(buf, HalDisplay::BUFFER_SIZE);
        logSerial.printf("SCREENSHOT_END\n");
      }
    }
  }

  // Check for any user activity (button press or release) or active background work
  static unsigned long lastActivityTime = millis();
  if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || activityManager.preventAutoSleep()) {
    lastActivityTime = millis();         // Reset inactivity timer
    powerManager.setPowerSaving(false);  // Restore normal CPU frequency on user activity
#if CROSSPOINT_PAPERS3
    SleepImages.noteUserActivity();
#endif
  }

  static bool screenshotButtonsReleased = true;
  if (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.isPressed(HalGPIO::BTN_DOWN)) {
    if (screenshotButtonsReleased) {
      screenshotButtonsReleased = false;
      {
        RenderLock lock;
        ScreenshotUtil::takeScreenshot(renderer);
      }
    }
    return;
  } else {
    screenshotButtonsReleased = true;
  }

#if CROSSPOINT_PAPERS3
  // Paper S3 has no readable physical power-button GPIO. The top-left
  // on-screen power button is therefore mapped to BTN_POWER. On release,
  // reuse the normal deep-sleep path so the configured sleep picture is
  // rendered completely before panel and system power are shut down.
  if (mappedInputManager.wasReleased(MappedInputManager::Button::Power)) {
    LOG_DBG("SLP", "On-screen power button tapped");
    enterDeepSleep();
    return;
  }
#endif

  // Refresh screen when power button is short-pressed with FORCE_REFRESH setting.
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::FORCE_REFRESH &&
      mappedInputManager.wasReleased(MappedInputManager::Button::Power)) {
    LOG_DBG("MAIN", "Manual screen refresh triggered");
    RenderLock lock;
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }

  const unsigned long sleepTimeoutMs = SETTINGS.getSleepTimeoutMs();
  if (millis() - lastActivityTime >= sleepTimeoutMs) {
    LOG_DBG("SLP", "Auto-sleep triggered after %lu ms of inactivity", sleepTimeoutMs);
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

#if !CROSSPOINT_PAPERS3
  // Paper S3 has no detectable power button GPIO; sleep is only via auto-sleep timeout
  if (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getHeldTime() > SETTINGS.getPowerButtonDuration()) {
    // If the screenshot combination is potentially being pressed, don't sleep
    if (gpio.isPressed(HalGPIO::BTN_DOWN)) {
      return;
    }
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }
#endif

  const unsigned long activityStartTime = millis();
  activityManager.loop();
  if (activityManager.consumeDeepSleepRequest()) {
    LOG_DBG("SLP", "Deep sleep requested by UI");
    enterDeepSleep();
    return;
  }
#if CROSSPOINT_PAPERS3
  const bool renderBusy = RenderLock::peek();
  if (renderBusy) {
    SleepImages.noteUserActivity();
  }
  SleepImages.loop(
      millis() - lastActivityTime,
      activityManager.preventAutoSleep() || renderBusy);
#endif
  const unsigned long activityDuration = millis() - activityStartTime;

  const unsigned long loopDuration = millis() - loopStartTime;
  if (loopDuration > maxLoopDuration) {
    maxLoopDuration = loopDuration;
    if (maxLoopDuration > 50) {
      LOG_DBG("LOOP", "New max loop duration: %lu ms (activity: %lu ms)", maxLoopDuration, activityDuration);
    }
  }

  // Add delay at the end of the loop to prevent tight spinning
  // When an activity requests skip loop delay (e.g., webserver running), use yield() for faster response
  // Otherwise, use longer delay to save power
  if (activityManager.skipLoopDelay()) {
    powerManager.setPowerSaving(false);  // Make sure we're at full performance when skipLoopDelay is requested
    yield();                             // Give FreeRTOS a chance to run tasks, but return immediately
  } else {
#if CROSSPOINT_PAPERS3
    // PaperS3: minimal delay for fast touch response (~500Hz polling).
    // Power management is handled by the PMIC, not CPU throttling.
    delay(2);
#else
    if (millis() - lastActivityTime >= HalPowerManager::IDLE_POWER_SAVING_MS) {
      // If we've been inactive for a while, increase the delay to save power
      powerManager.setPowerSaving(true);  // Lower CPU frequency after extended inactivity
      delay(50);
    } else {
      // Short delay to prevent tight loop while still being responsive
      delay(10);
    }
#endif
  }
}
