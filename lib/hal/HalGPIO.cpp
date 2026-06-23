#include <HalGPIO.h>
#include <Logging.h>
#include <SPI.h>

// Touch zones in LOGICAL portrait coordinates (540 wide x 960 tall).
// The physical display is 960x540 landscape; GfxRenderer rotates to portrait.
// GT911 on M5PaperS3 reports in portrait: x[0-539], y[0-959].
static constexpr int16_t PORT_W = 540;
static constexpr int16_t PORT_H = 960;

static void getLogicalDimensions(uint8_t orientation, int16_t* outWidth, int16_t* outHeight) {
  switch (orientation) {
    case 1:  // LandscapeClockwise
    case 3:  // LandscapeCounterClockwise
      *outWidth = PORT_H;
      *outHeight = PORT_W;
      break;
    case 0:  // Portrait
    case 2:  // PortraitInverted
    default:
      *outWidth = PORT_W;
      *outHeight = PORT_H;
      break;
  }
}

void HalGPIO::transformTouchPoint(int16_t rawX, int16_t rawY, int16_t* outX, int16_t* outY) const {
  switch (touchOrientation) {
    case 1:  // LandscapeClockwise
      *outX = PORT_H - 1 - rawY;
      *outY = rawX;
      break;
    case 2:  // PortraitInverted
      *outX = PORT_W - 1 - rawX;
      *outY = PORT_H - 1 - rawY;
      break;
    case 3:  // LandscapeCounterClockwise
      *outX = rawY;
      *outY = PORT_W - 1 - rawX;
      break;
    case 0:  // Portrait
    default:
      *outX = rawX;
      *outY = rawY;
      break;
  }
}

// 3-zone vertical split: each zone is 1/3 of screen width (180px)
static constexpr int16_t ZONE_LEFT_END = PORT_W / 3;         // 180
static constexpr int16_t ZONE_RIGHT_START = PORT_W * 2 / 3;  // 360

void HalGPIO::begin() {
  // Initialize SD card SPI bus with PaperS3 pins
  SPI.begin(PAPERS3_SD_SCK, PAPERS3_SD_MISO, PAPERS3_SD_MOSI, PAPERS3_SD_CS);
  // Initialize GT911 touch on I2C1 (SDA=41, SCL=42, INT=48)
  if (!touch.begin(41, 42, 48, 0x14)) {
    LOG_ERR("GPIO", "GT911 touch init failed");
  }
}

int16_t HalGPIO::getLastTouchX() const {
  int16_t logicalX = 0;
  int16_t logicalY = 0;
  transformTouchPoint(lastTouchX, lastTouchY, &logicalX, &logicalY);
  return logicalX;
}

int16_t HalGPIO::getLastTouchY() const {
  int16_t logicalX = 0;
  int16_t logicalY = 0;
  transformTouchPoint(lastTouchX, lastTouchY, &logicalX, &logicalY);
  return logicalY;
}

int16_t HalGPIO::getContentTapX() const { return lastContentTapX; }

int16_t HalGPIO::getContentTapY() const { return lastContentTapY; }

int HalGPIO::touchZoneToButton(int16_t touchX, int16_t touchY) const {
  // GT911 on M5PaperS3 reports portrait coordinates directly: x[0-539], y[0-959].
  int16_t logicalX = 0;
  int16_t logicalY = 0;
  transformTouchPoint(touchX, touchY, &logicalX, &logicalY);

  int16_t logicalW = 0;
  int16_t logicalH = 0;
  getLogicalDimensions(touchOrientation, &logicalW, &logicalH);

  if (logicalX < 0 || logicalX >= logicalW || logicalY < 0 || logicalY >= logicalH) return -1;

  // Visible on-screen power button in the top-left corner of non-reader screens.
  // Check this before suppressing content-area taps in footer mode. Reader screens
  // keep footerHeight == 0, so normal page-tap zones are not affected.
  if (footerHeight > 0 && logicalX < POWER_HOTSPOT_SIZE && logicalY < POWER_HOTSPOT_SIZE) {
    return BTN_POWER;
  }

  // Footer nav bar: bottom footerHeight pixels are split into 4 equal tap zones
  // mapping to Back / Confirm / Previous / Next (matches drawButtonHints layout).
  // Previous / Next use BTN_LEFT / BTN_RIGHT so list screens can treat them as
  // page-level navigation rather than single-row Up / Down.
  if (footerHeight > 0) {
    if (logicalY >= logicalH - footerHeight) {
      const int16_t quarter = logicalW / 4;
      if (logicalX < quarter) return BTN_BACK;
      if (logicalX < quarter * 2) return BTN_CONFIRM;
      if (logicalX < quarter * 3) return BTN_LEFT;
      return BTN_RIGHT;
    }
    // Content-area tap in footer mode does not map to a virtual button.
    // HalGPIO::update exposes it separately for Direct Touch Selection.
    return -1;
  }

  // Simple 3-zone vertical split across the content area
  const int16_t zoneLeftEnd = logicalW / 3;
  const int16_t zoneRightStart = logicalW * 2 / 3;
  if (logicalX < zoneLeftEnd) return BTN_LEFT;
  if (logicalX >= zoneRightStart) return BTN_RIGHT;
  return BTN_CONFIRM;
}

void HalGPIO::update() {
  previousState = currentState;
  currentState = 0;
  contentTapReleased = false;

  // During cooldown (after activity transition), drain touch events but don't act on them
  if (millis() < cooldownUntil) {
    touch.update();  // Drain pending touch reports
    touchActive = false;
    sawMultiTouch = false;
    return;
  }

  bool touching = touch.update();
  uint8_t numPoints = touch.getNumPoints();

  if (touching) {
    lastTouchX = touch.getX();
    lastTouchY = touch.getY();

    // Track multi-touch: if we ever see 2+ fingers during this sequence, remember it
    if (numPoints >= 2) {
      sawMultiTouch = true;
    }

    // Record start position on initial touch-down
    if (!touchActive) {
      touchActive = true;
      touchStartX = lastTouchX;
      touchStartY = lastTouchY;
    }

    // While finger is down, report the zone button as pressed (for held-time detection).
    // Use touchStart (not current) position so the zone stays locked to the initial tap
    // intent and doesn't bounce across zone boundaries when the finger drifts slightly.
    int btn = touchZoneToButton(touchStartX, touchStartY);
    if (btn >= 0 && btn < HALGPIO_NUM_BUTTONS) {
      currentState |= (1 << btn);
    }
  } else if (touchActive) {
    // Finger just lifted — classify the gesture
    lastHeldTime = millis() - pressStartTime;
    touchActive = false;

    if (footerHeight > 0) {
      // Footer active (non-reader): bottom footer taps still drive the four
      // virtual buttons. Content-area single-finger taps are exposed separately
      // for Direct Touch Selection and intentionally do not map to buttons.
      int btn = touchZoneToButton(touchStartX, touchStartY);
      if (btn >= 0 && btn < HALGPIO_NUM_BUTTONS) {
        currentState |= (1 << btn);
      } else if (!sawMultiTouch) {
        int16_t startLogicalX = 0;
        int16_t startLogicalY = 0;
        int16_t lastLogicalX = 0;
        int16_t lastLogicalY = 0;
        transformTouchPoint(touchStartX, touchStartY, &startLogicalX, &startLogicalY);
        transformTouchPoint(lastTouchX, lastTouchY, &lastLogicalX, &lastLogicalY);

        int16_t logicalW = 0;
        int16_t logicalH = 0;
        getLogicalDimensions(touchOrientation, &logicalW, &logicalH);
        const int16_t dx = lastLogicalX - startLogicalX;
        const int16_t dy = lastLogicalY - startLogicalY;
        const bool insideContent = startLogicalX >= 0 && startLogicalX < logicalW && startLogicalY >= 0 &&
                                   startLogicalY < logicalH - footerHeight;
        const bool lowDrift = dx >= -TAP_DRIFT_THRESHOLD && dx <= TAP_DRIFT_THRESHOLD &&
                              dy >= -TAP_DRIFT_THRESHOLD && dy <= TAP_DRIFT_THRESHOLD;
        if (insideContent && lowDrift) {
          lastContentTapX = startLogicalX;
          lastContentTapY = startLogicalY;
          contentTapReleased = true;
          LOG_DBG("TOUCH", "content tap at (%d,%d) (footer mode)", startLogicalX, startLogicalY);
        }
      }
      LOG_DBG("TOUCH", "tap at (%d,%d) btn=%d (footer mode)", touchStartX, touchStartY, btn);
    } else if (sawMultiTouch) {
      // 2-finger tap → BACK (reader only)
      currentState |= (1 << BTN_BACK);
      currentState |= (1 << BTN_TWO_FINGER);
      LOG_DBG("TOUCH", "2-finger tap -> BACK");
    } else {
      // Single finger: check for swipe vs tap (reader only)
      int16_t startLogicalX = 0;
      int16_t startLogicalY = 0;
      int16_t lastLogicalX = 0;
      int16_t lastLogicalY = 0;
      transformTouchPoint(touchStartX, touchStartY, &startLogicalX, &startLogicalY);
      transformTouchPoint(lastTouchX, lastTouchY, &lastLogicalX, &lastLogicalY);
      int16_t deltaY = lastLogicalY - startLogicalY;

      if (deltaY < -SWIPE_THRESHOLD) {
        // Swiped up (finger moved upward)
        currentState |= (1 << BTN_SWIPE_UP);
        currentState |= (1 << BTN_UP);
        LOG_DBG("TOUCH", "swipe up dy=%d", deltaY);
      } else if (deltaY > SWIPE_THRESHOLD) {
        // Swiped down (finger moved downward)
        currentState |= (1 << BTN_SWIPE_DOWN);
        currentState |= (1 << BTN_DOWN);
        LOG_DBG("TOUCH", "swipe down dy=%d", deltaY);
      } else {
        // Tap — map to zone based on touch-down position
        int btn = touchZoneToButton(touchStartX, touchStartY);
        if (btn >= 0 && btn < HALGPIO_NUM_BUTTONS) {
          currentState |= (1 << btn);
        }
        LOG_DBG("TOUCH", "tap at (%d,%d) btn=%d", touchStartX, touchStartY, btn);
      }
    }

    sawMultiTouch = false;
  }

  // Track press timing
  uint16_t newPresses = currentState & ~previousState;
  if (newPresses) {
    pressStartTime = millis();
    for (uint8_t i = 0; i < HALGPIO_NUM_BUTTONS; i++) {
      if (newPresses & (1 << i)) {
        lastPressedButton = i;
        break;
      }
    }
  }
}

void HalGPIO::clearState() {
  previousState = 0;
  currentState = 0;
  pressStartTime = 0;
  lastHeldTime = 0;
  touchActive = false;
  sawMultiTouch = false;
  contentTapReleased = false;
  cooldownUntil = millis() + 200;  // Suppress input for 200ms after activity transition
}

bool HalGPIO::isPressed(uint8_t buttonIndex) const {
  if (buttonIndex >= HALGPIO_NUM_BUTTONS) return false;
  return currentState & (1 << buttonIndex);
}

bool HalGPIO::wasPressed(uint8_t buttonIndex) const {
  if (buttonIndex >= HALGPIO_NUM_BUTTONS) return false;
  // Rising edge: pressed now but not before
  return (currentState & (1 << buttonIndex)) && !(previousState & (1 << buttonIndex));
}

bool HalGPIO::wasAnyPressed() const { return (currentState & ~previousState) != 0; }

bool HalGPIO::wasReleased(uint8_t buttonIndex) const {
  if (buttonIndex >= HALGPIO_NUM_BUTTONS) return false;
  // Falling edge: not pressed now but was before
  return !(currentState & (1 << buttonIndex)) && (previousState & (1 << buttonIndex));
}

bool HalGPIO::wasAnyReleased() const { return (previousState & ~currentState) != 0; }

unsigned long HalGPIO::getHeldTime() const {
  if (currentState == 0) return lastHeldTime;
  return millis() - pressStartTime;
}

bool HalGPIO::isUsbConnected() const {
  // With ARDUINO_USB_CDC_ON_BOOT=1, Serial is USB CDC.
  // operator bool() returns true when a USB host has connected (DTR asserted).
  // Serial.begin() must be called before this for reliable results.
  return Serial;
}

HalGPIO::WakeupReason HalGPIO::getWakeupReason() const {
  const bool usbConnected = isUsbConnected();
  const auto wakeupCause = esp_sleep_get_wakeup_cause();
  const auto resetReason = esp_reset_reason();

  if (wakeupCause == ESP_SLEEP_WAKEUP_EXT0 || wakeupCause == ESP_SLEEP_WAKEUP_EXT1 ||
      wakeupCause == ESP_SLEEP_WAKEUP_GPIO) {
    return WakeupReason::PowerButton;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON && !usbConnected) {
    return WakeupReason::PowerButton;
  }
  // ESP32-S3: After flash, esptool hard-resets via RTS → ESP_RST_POWERON (not ESP_RST_UNKNOWN like ESP32-C3).
  // On M5PaperS3, the PMIC handles charging independently, so any cold boot with USB connected
  // should proceed to boot normally (treat as AfterFlash).
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && usbConnected &&
      (resetReason == ESP_RST_UNKNOWN || resetReason == ESP_RST_POWERON)) {
    return WakeupReason::AfterFlash;
  }
  return WakeupReason::Other;
}