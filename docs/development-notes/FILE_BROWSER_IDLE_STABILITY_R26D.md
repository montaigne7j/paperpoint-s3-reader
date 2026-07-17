# r26d File Browser idle stability fix

Observed log showed the device entering FileBrowser, performing partial row updates, then entering Paper S3 80 MHz idle mode while still browsing folders. A later touch restored normal frequency, but the user reported repeated crashes during folder browsing.

This build keeps FileBrowser at 240 MHz while it is the current activity. It does not disable auto-sleep, and it does not change the 80 MHz idle behavior for normal reading/home screens that allow idle power saving.

Changes:

- Added `Activity::allowIdlePowerSaving()`.
- Added `ActivityManager::allowIdlePowerSaving()`.
- `FileBrowserActivity` returns `false` so main loop keeps full CPU frequency and the original 2 ms poll delay.
- `FileBrowserActivity::loadFiles()` now holds a `HalPowerManager::Lock` and periodically yields/resets task watchdog during directory scans and after sorting.

This is intentionally conservative: the File Browser is a short-lived interactive screen, so stability is prioritized over idle power saving there.
