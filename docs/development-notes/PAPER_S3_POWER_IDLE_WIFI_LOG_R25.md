# Paper S3 r25 power idle, WiFi cleanup, and logging notes

> **r33 update:** the Paper S3 80 MHz idle transition described below is now disabled for stability. Paper S3 remains at its normal boot CPU frequency; deep sleep / power-off is still used for power saving. The WiFi cleanup and logging sections remain applicable.

This note documents the low-risk power changes introduced after the r24 fast-tap build.

## CPU idle policy

Paper S3 no longer bypasses `HalPowerManager::setPowerSaving()`.  The first-stage idle policy is intentionally conservative:

- normal active work: restore the boot CPU frequency, normally 240 MHz;
- idle after `HalPowerManager::IDLE_POWER_SAVING_MS` (3 seconds): switch to 80 MHz;
- Paper S3 main loop delay changes from 2 ms to 20 ms only while idle;
- no light sleep;
- no 10 MHz mode.

Any user activity, render lock, background work that prevents auto sleep, WiFi-active state, or skip-loop-delay activity restores full speed.

## Logging policy

The `LOG_INF` / `LOG_DBG` macros now check `logShouldFormat()` before evaluating arguments.  `logPrintf()` also returns before `va_start()` / `vsnprintf()` for INF/DBG messages when no USB serial monitor is connected.  ERR messages are still formatted and stored in the RTC log ring buffer so crash/error breadcrumbs remain available.

Release and RC builds use `LOG_LEVEL=0` so only ERR is compiled through the normal logging macros.  The default development build still uses debug logs for diagnostics.

## WiFi cleanup policy

`WifiSelectionActivity` now has a `keepWifiOnAfterExit` constructor flag.

- Network parent activities keep the default `true` because they need WiFi after network selection.
- Settings > WiFi Networks passes `false`, so leaving that screen deletes scan results, disconnects, and sets `WiFi.mode(WIFI_OFF)`.

This fixes the risk that a standalone WiFi scan or connection from Settings leaves the ESP32-S3 WiFi radio active during normal reading.

## Scope intentionally not included

- No GT911 interrupt wake or light sleep.
- No 40 MHz / 10 MHz idle mode.
- No WebServer WiFi sleep change; WebServer still intentionally uses `WiFi.setSleep(false)` for responsiveness while active.
