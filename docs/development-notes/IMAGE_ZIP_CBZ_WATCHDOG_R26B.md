# r26b Image ZIP / CBZ watchdog fix

## Problem

Opening a large image ZIP / CBZ could enter `CbzReader`, successfully list images, then reboot with task watchdog output before the first page appeared.

Observed pattern:

- `Entering activity: CbzReader`
- `Loaded image zip: ... images=107`
- missing progress file is reported, which is normal on first open
- first page extraction / decode starts
- `task_wdt: Task watchdog got triggered`
- CPU0 is running `ActivityManager`

The archive was not rejected; the long first-page extraction/decode path did not yield often enough.

## Fix

- `ZipFile::readFileToStream()` now cooperatively delays during stored and deflated streaming output.
- JPEG framebuffer decode callbacks now cooperatively delay during 1:1, upscale, and downscale output loops.
- PNG framebuffer decode callback now cooperatively delays once per decoded line callback.
- `CbzReaderActivity` logs page extraction/decode milestones so the next device log can show whether a failure happens during ZIP extraction, image decode, status bar, or EPD display.

## Scope

This does not add light sleep, 10 MHz mode, or a new image rendering mode.  It only prevents long-running image work from starving the watchdog/idle task.
