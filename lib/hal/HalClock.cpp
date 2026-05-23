#include "HalClock.h"

#include <Logging.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <time.h>

#include "HalM5Mutex.h"

HalClock halClock;  // Singleton instance

void HalClock::begin() {
  // M5Unified initialises the on-board BM8563 RTC during M5.begin(); we just
  // probe it here to confirm the chip responded. Most Paper S3 units ship
  // with a working RTC, but on some pre-production units the BM8563 is
  // missing — gate the feature accordingly.
  HalM5Mutex::lock();
  // m5::rtc_date_t and m5::rtc_time_t are populated as a side effect of
  // reading the RTC. If the BM8563 isn't responding, M5.Rtc.getDateTime()
  // returns a zeroed struct and isEnabled() returns false.
  _available = M5.Rtc.isEnabled();
  if (_available) {
    // Prime the cache with an initial read.
    m5::rtc_time_t t;
    M5.Rtc.getTime(&t);
    _cachedHour = t.hours;
    _cachedMinute = t.minutes;
    _lastPollMs = millis();
    LOG_INF("CLK", "BM8563 RTC available, current UTC %02d:%02d", _cachedHour, _cachedMinute);
  } else {
    LOG_INF("CLK", "BM8563 RTC not available");
  }
  HalM5Mutex::unlock();
}

bool HalClock::getTime(uint8_t& hour, uint8_t& minute) const {
  if (!_available) return false;

  const unsigned long now = millis();
  if (_lastPollMs != 0 && (now - _lastPollMs) < CLOCK_POLL_MS) {
    hour = _cachedHour;
    minute = _cachedMinute;
    return true;
  }

  HalM5Mutex::lock();
  m5::rtc_time_t t;
  M5.Rtc.getTime(&t);
  HalM5Mutex::unlock();

  _cachedHour = t.hours;
  _cachedMinute = t.minutes;
  _lastPollMs = now;

  hour = _cachedHour;
  minute = _cachedMinute;
  return true;
}

bool HalClock::formatTime(char* buf, size_t bufSize, uint8_t utcOffsetBiased,
                          bool use24h) const {
  if (bufSize < 6) return false;
  uint8_t h, m;
  if (!getTime(h, m)) return false;

  // Apply UTC offset: biased value to signed half-hours.
  const int offsetHalfHours = static_cast<int>(utcOffsetBiased) - 24;
  int totalMinutes = static_cast<int>(h) * 60 + static_cast<int>(m) + offsetHalfHours * 30;
  totalMinutes = ((totalMinutes % 1440) + 1440) % 1440;  // wrap 24h

  const int hour24 = totalMinutes / 60;
  const int minute = totalMinutes % 60;

  if (use24h) {
    snprintf(buf, bufSize, "%02d:%02d", hour24, minute);
  } else {
    if (bufSize < 9) return false;  // need room for "HH:MM AM" + NUL
    const bool pm = hour24 >= 12;
    int h12 = hour24 % 12;
    if (h12 == 0) h12 = 12;
    snprintf(buf, bufSize, "%d:%02d %s", h12, minute, pm ? "PM" : "AM");
  }
  return true;
}

bool HalClock::writeTimeToRTC(uint8_t hour, uint8_t minute, uint8_t second) {
  if (!_available) return false;

  HalM5Mutex::lock();
  m5::rtc_time_t t;
  t.hours = hour;
  t.minutes = minute;
  t.seconds = second;
  M5.Rtc.setTime(&t);
  HalM5Mutex::unlock();

  _lastPollMs = 0;  // invalidate cache
  _cachedHour = hour;
  _cachedMinute = minute;
  return true;
}

bool HalClock::syncFromNTP() {
  if (!_available) return false;

  if (WiFi.status() != WL_CONNECTED) {
    LOG_ERR("CLK", "WiFi not connected, cannot sync NTP");
    return false;
  }

  LOG_INF("CLK", "Starting NTP sync...");
  configTzTime("UTC0", "pool.ntp.org", "time.nist.gov");

  // Wait up to 5 seconds for the first NTP response.
  constexpr int maxAttempts = 50;
  for (int i = 0; i < maxAttempts; i++) {
    const time_t now = time(nullptr);
    if (now > 1700000000) {  // sanity: after ~Nov 2023
      struct tm timeinfo;
      gmtime_r(&now, &timeinfo);
      const bool ok = writeTimeToRTC(timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
      if (ok) {
        LOG_INF("CLK", "RTC set to %02d:%02d:%02d UTC", timeinfo.tm_hour,
                timeinfo.tm_min, timeinfo.tm_sec);
      }
      return ok;
    }
    delay(100);
  }

  LOG_ERR("CLK", "NTP sync timed out");
  return false;
}
