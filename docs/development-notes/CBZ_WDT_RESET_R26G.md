# r26g CBZ task watchdog reset fix

## Problem

Opening a ZIP / CBZ file could scan the archive successfully and then emit repeated ESP-IDF errors:

```text
E task_wdt: esp_task_wdt_reset(...): task not found
```

Device logs showed that the CBZ reader entered successfully, found the image list, and started extracting page 1, but cooperative watchdog reset calls were made from a task that was not registered with the task watchdog.

## Fix

The cooperative work paths no longer call `esp_task_wdt_reset()` directly. They only yield to FreeRTOS:

- `CbzReaderActivity`
- `ZipFile` streaming extraction
- `JpegToFramebufferConverter`
- `PngToFramebufferConverter`
- `FileBrowserActivity` directory scan / sort path

This lets IDLE tasks run and avoids the `task not found` error spam.

## Scope

This is a stability fix only. It preserves the r26 image ZIP / CBZ reader behavior and keeps previous r26 compile and FileBrowser stability fixes.
