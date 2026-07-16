# r26c Image ZIP / CBZ watchdog stability fix

This patch is intentionally conservative. Large manga ZIP/CBZ pages can spend a long time in ZIP extraction and JPEG/PNG decoding before the first visible page appears. On Paper S3 this was able to starve the FreeRTOS idle task and trigger `task_wdt` while `ActivityManager` was rendering.

Changes:

- CBZ reader now requests `skipLoopDelay()` so the main loop keeps full CPU frequency while CBZ is current. Auto sleep is not disabled.
- ZIP streaming extraction yields more frequently and resets the task watchdog before/around long read/write steps.
- JPEG/PNG file callbacks and decode callbacks yield more frequently and reset the task watchdog.
- CBZ extraction chunk size is reduced to 1024 bytes for better cooperative behavior.

No light sleep is introduced. The goal is to stabilize image ZIP/CBZ opening before further optimization.
