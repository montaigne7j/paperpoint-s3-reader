# Foreground Grouped Progressive Render v32

Base: v31 foreground grouped progressive render.

## Fixes in v32

1. **Ignore/drain gestures during foreground cache-miss render**

   v31 intentionally polled touch input between progressive groups so the touch
   driver stayed fresh.  However, a swipe detected during this visible render
   could remain pending and be handled by the normal reader loop after the page
   completed.  That advanced `section->currentPage` without an immediate visible
   update, so the next swipe looked like it jumped two pages.

   v32 drains input at progressive render start and clears input after each
   observed in-render gesture.  It also clears input again after cache store.
   Result: swipes during a foreground render are ignored instead of queued as a
   delayed page turn.

2. **Vertical progressive reveal direction corrected**

   Reader navigation remains:

   - Vertical reading: swipe right = next page
   - Vertical reading: swipe left = previous page

   The visual reveal direction is now reversed from v31:

   - Previous page / swipe left: reveal from the right side first
   - Next page / swipe right: reveal from the left side first

3. **Grouped row-range refresh remains**

   v31 already refreshed only the physical row range covered by the current
   group of PageElements.  v32 keeps that behavior.  Only the final consistency
   refresh still uses `rows 0..539` so background, margins, and status bar are
   fully synchronized.

## New / changed logs

```text
[PRG] input observed and will be ignored during grouped progressive ...
[PRG] input drained after grouped progressive render
[PRG] input state cleared after grouped progressive render
```

The normal group logs remain:

```text
[PRG] foreground grouped progressive start: elements=... groupSize=2 order=...
[PRG] group N elements=... rows=A..B draw=... displayCall=...
[PRG] internal heap start: Free=... Min=... MaxAlloc=...
[PRG] internal heap before-cache-store: Free=... Min=... MaxAlloc=...
[PRG] internal heap done: Free=... Min=... MaxAlloc=...
```
