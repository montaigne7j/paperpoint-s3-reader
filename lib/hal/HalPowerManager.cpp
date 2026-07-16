#include "HalPowerManager.h"

#include <Logging.h>
#include <WiFi.h>
#include <esp_sleep.h>

#include <cassert>
#include <algorithm>

#include "HalGPIO.h"

// M5PaperS3 power-off pulse pin (active-high pulse turns off PMIC)
static constexpr int PWROFF_PULSE_PIN = 44;

// M5PaperS3 battery voltage ADC pin (hardware voltage divider, ~2.04x ratio)
static constexpr int BAT_ADC_PIN = 3;
static constexpr int USB_DET_PIN = 5;
static constexpr int BAT_ADC_SAMPLES = 16;         // Number of ADC samples to average
static constexpr uint16_t BAT_HYSTERESIS_MV = 30;  // Only update if voltage changed by ≥30mV (~3%)
static uint16_t lastBattMv = 0;                    // Cached smoothed voltage

HalPowerManager powerManager;  // Singleton instance

void HalPowerManager::begin() {
  normalFreq = getCpuFrequencyMhz();
  modeMutex = xSemaphoreCreateMutex();
  assert(modeMutex != nullptr);

#if CROSSPOINT_PAPERS3
  // Battery voltage is read via ADC on GPIO 3 (hardware voltage divider).
  // analogReadMilliVolts() handles ESP32-S3 ADC calibration internally.
  pinMode(BAT_ADC_PIN, INPUT);
  pinMode(USB_DET_PIN, INPUT);
  analogSetAttenuation(ADC_11db);
#endif
}

void HalPowerManager::setPowerSaving(bool enabled) {
#if CROSSPOINT_PAPERS3
  // r33 stability policy: Paper S3 CPU down-clocking is disabled. Keep this
  // guard here as well as in main.cpp so future callers cannot accidentally
  // re-enable the unstable 80 MHz transition.
  enabled = false;
#endif

  if (normalFreq <= 0) {
    return;  // invalid state
  }

  auto wifiMode = WiFi.getMode();
  if (wifiMode != WIFI_MODE_NULL) {
    // WiFi is active, force full speed so network services remain responsive
    enabled = false;
  }

  // Note: We don't use mutex here to avoid too much overhead,
  // it's not very important if we read a slightly stale value for currentLockMode
  const LockMode mode = currentLockMode;

  if (mode == None && enabled && !isLowPower) {
    LOG_DBG("PWR", "Going to low-power mode: %d MHz", LOW_POWER_FREQ);
    if (!setCpuFrequencyMhz(LOW_POWER_FREQ)) {
      LOG_DBG("PWR", "Failed to set CPU frequency = %d MHz", LOW_POWER_FREQ);
      return;
    }
    isLowPower = true;

  } else if ((!enabled || mode != None) && isLowPower) {
    LOG_DBG("PWR", "Restoring normal CPU frequency: %d MHz", normalFreq);
    if (!setCpuFrequencyMhz(normalFreq)) {
      LOG_DBG("PWR", "Failed to set CPU frequency = %d MHz", normalFreq);
      return;
    }
    isLowPower = false;
  }

  // Otherwise, no change needed
}

void HalPowerManager::startDeepSleep(HalGPIO& gpio) const {
  // Ensure that the power button has been released to avoid immediately turning back on if you're holding it
  while (gpio.isPressed(HalGPIO::BTN_POWER)) {
    delay(50);
    gpio.update();
  }

  // Power off via GPIO44 pulse to PMIC
  // This turns off the device completely; wakeup is via power button through PMIC
  pinMode(PWROFF_PULSE_PIN, OUTPUT);
  digitalWrite(PWROFF_PULSE_PIN, HIGH);
  delay(100);
  digitalWrite(PWROFF_PULSE_PIN, LOW);

  // If powerOff doesn't halt (e.g., USB connected), fall back to deep sleep
  // with a 5-second timer wakeup as safety net — without a wakeup source the
  // device would be stuck in unrecoverable deep sleep.
  esp_sleep_enable_timer_wakeup(5 * 1000 * 1000);  // 5 seconds in microseconds
  esp_deep_sleep_start();
}

uint16_t HalPowerManager::getBatteryPercentage() const {
#if CROSSPOINT_PAPERS3
  static int displayedPercent = -1;
  static bool previousUsb = false;
  static uint32_t usbChangedAt = 0;
  static uint32_t lastStepAt = 0;

  if (isLowPower && displayedPercent >= 0) return displayedPercent;

  // Median sampling rejects the occasional large ESP32-S3 ADC outlier better
  // than a simple average.
  uint16_t samples[BAT_ADC_SAMPLES];
  for (int i = 0; i < BAT_ADC_SAMPLES; ++i) samples[i] = analogReadMilliVolts(BAT_ADC_PIN);
  std::sort(samples, samples + BAT_ADC_SAMPLES);
  const uint16_t adcMv = samples[BAT_ADC_SAMPLES / 2];
  const uint16_t battMv = static_cast<uint16_t>((adcMv * 204UL) / 100UL);
  const bool usbPowered = analogReadMilliVolts(USB_DET_PIN) > 200;

  if (lastBattMv == 0 || abs((int)battMv - (int)lastBattMv) >= BAT_HYSTERESIS_MV) lastBattMv = battMv;

  // Piecewise Li-Po discharge curve. Charging voltage is deliberately not
  // allowed to make the displayed state-of-charge jump immediately.
  struct Point { uint16_t mv; uint8_t pct; };
  static constexpr Point curve[] = {
      {3300,0},{3500,5},{3600,10},{3700,20},{3750,30},{3800,42},
      {3850,55},{3900,68},{4000,82},{4100,93},{4200,100}};
  int rawPercent = 0;
  if (lastBattMv >= curve[10].mv) rawPercent = 100;
  else {
    for (size_t i = 1; i < sizeof(curve)/sizeof(curve[0]); ++i) {
      if (lastBattMv <= curve[i].mv) {
        const auto& a=curve[i-1]; const auto& b=curve[i];
        rawPercent = a.pct + (lastBattMv-a.mv)*(b.pct-a.pct)/(b.mv-a.mv);
        break;
      }
    }
  }

  const uint32_t now = millis();
  if (displayedPercent < 0) {
    displayedPercent = rawPercent;
    previousUsb = usbPowered;
    usbChangedAt = lastStepAt = now;
  }
  if (usbPowered != previousUsb) {
    previousUsb = usbPowered;
    usbChangedAt = now;
    LOG_INF("PWR", "USB power changed=%d; hold battery display for stabilization", usbPowered ? 1 : 0);
  }

  // Hold for 30 seconds after cable changes. Afterwards move only one point at
  // a time, preventing 74% -> 51% cable-removal jumps.
  if (now - usbChangedAt >= 30000) {
    const uint32_t interval = usbPowered ? 60000 : 30000;
    if (now - lastStepAt >= interval) {
      if (rawPercent > displayedPercent) ++displayedPercent;
      else if (rawPercent < displayedPercent) --displayedPercent;
      lastStepAt = now;
    }
  }
  displayedPercent = std::max(0, std::min(100, displayedPercent));
  LOG_DBG("PWR", "USB=%d ADC=%umV VBAT=%umV raw=%d display=%d", usbPowered, adcMv, lastBattMv, rawPercent, displayedPercent);
  return static_cast<uint16_t>(displayedPercent);
#else
  return 100;
#endif
}

HalPowerManager::Lock::Lock() {
  xSemaphoreTake(powerManager.modeMutex, portMAX_DELAY);
  // Current limitation: only one lock at a time
  if (powerManager.currentLockMode != None) {
    LOG_ERR("PWR", "Lock already held, ignore");
    valid = false;
  } else {
    powerManager.currentLockMode = NormalSpeed;
    valid = true;
  }
  xSemaphoreGive(powerManager.modeMutex);
  if (valid) {
    // Immediately restore normal CPU frequency if currently in low-power mode
    powerManager.setPowerSaving(false);
  }
}

HalPowerManager::Lock::~Lock() {
  xSemaphoreTake(powerManager.modeMutex, portMAX_DELAY);
  if (valid) {
    powerManager.currentLockMode = None;
  }
  xSemaphoreGive(powerManager.modeMutex);
}
