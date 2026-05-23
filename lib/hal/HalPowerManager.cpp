#include "HalPowerManager.h"

#include <Logging.h>
#include <WiFi.h>
#include <esp_sleep.h>

#include <cassert>

#include "HalGPIO.h"

// M5PaperS3 power-off pulse pin (active-high pulse turns off PMIC)
static constexpr int PWROFF_PULSE_PIN = 44;

// M5PaperS3 battery voltage ADC pin (hardware voltage divider, ~2.04x ratio)
static constexpr int BAT_ADC_PIN = 3;
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
  analogSetAttenuation(ADC_11db);
#endif
}

void HalPowerManager::setPowerSaving(bool enabled) {
#if CROSSPOINT_PAPERS3
  // PaperS3 has a dedicated PMIC for power management and deep-sleeps via
  // GPIO44 pulse.  CPU throttling from 240→10 MHz only adds touch latency
  // (~50 ms) with negligible battery savings.  Keep full speed always.
  (void)enabled;
  return;
#else
  if (normalFreq <= 0) {
    return;  // invalid state
  }

  auto wifiMode = WiFi.getMode();
  if (wifiMode != WIFI_MODE_NULL) {
    // Wifi is active, force disabling power saving
    enabled = false;
  }

  // Note: We don't use mutex here to avoid too much overhead,
  // it's not very important if we read a slightly stale value for currentLockMode
  const LockMode mode = currentLockMode;

  if (mode == None && enabled && !isLowPower) {
    LOG_DBG("PWR", "Going to low-power mode");
    if (!setCpuFrequencyMhz(LOW_POWER_FREQ)) {
      LOG_DBG("PWR", "Failed to set CPU frequency = %d MHz", LOW_POWER_FREQ);
      return;
    }
    isLowPower = true;

  } else if ((!enabled || mode != None) && isLowPower) {
    LOG_DBG("PWR", "Restoring normal CPU frequency");
    if (!setCpuFrequencyMhz(normalFreq)) {
      LOG_DBG("PWR", "Failed to set CPU frequency = %d MHz", normalFreq);
      return;
    }
    isLowPower = false;
  }

  // Otherwise, no change needed
#endif
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
  static uint16_t cachedPercentX10 = 0;

  if (isLowPower) {
    return cachedPercentX10 / 10;
  }

  // Average multiple ADC samples to reduce noise (ESP32-S3 ADC jitters ~20-50mV).
  uint32_t sum = 0;
  for (int i = 0; i < BAT_ADC_SAMPLES; i++) {
    sum += analogReadMilliVolts(BAT_ADC_PIN);
  }
  const uint16_t adcMv = (uint16_t)(sum / BAT_ADC_SAMPLES);
  const uint16_t battMv = (uint16_t)((adcMv * 204) / 100);

  // Hysteresis: only update cached voltage if change exceeds threshold.
  // Prevents the raw percentage from flickering between adjacent values.
  if (lastBattMv == 0 || abs((int)battMv - (int)lastBattMv) >= BAT_HYSTERESIS_MV) {
    lastBattMv = battMv;
  }
  LOG_DBG("PWR", "Battery ADC=%umV  VBAT=%umV (cached=%umV)", adcMv, battMv, lastBattMv);

  // Li-Po linear approximation: 4200mV = 100%, 3300mV = 0%
  uint16_t rawPercent = 0;
  if (lastBattMv >= 4200) {
    rawPercent = 100;
  } else if (lastBattMv > 3300) {
    rawPercent = (uint16_t)((lastBattMv - 3300) * 100 / 900);
  }

  // Smooth the battery %.
  if (cachedPercentX10 == 0) {
    cachedPercentX10 = rawPercent * 10;
  } else {
    cachedPercentX10 = (cachedPercentX10 * 9 + rawPercent * 10) / 10;
  }
  return cachedPercentX10 / 10;
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
