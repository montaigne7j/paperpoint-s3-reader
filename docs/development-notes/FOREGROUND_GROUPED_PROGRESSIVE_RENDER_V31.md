# Foreground Grouped Progressive Render v31

This experimental build updates the v30 foreground progressive page-render path.

## Changes

- Foreground progressive render is used only on visible cache-miss page turns.
  Framebuffer cache hits still use the normal fast path.
- Progressive refresh is now grouped: every 2 `PageElement`s are rendered before a row-range refresh.
  This reduces the number of row-range display calls compared with v30's one-refresh-per-element approach.
- Row-range refresh still computes the physical row range affected by the rendered elements.
  It does not intentionally drive physical rows 0..539 for every element group.
- The final status/background consistency refresh is retained because v30 showed that element refresh only covers text/body ranges.
- Before copying the completed page into the framebuffer cache, the display is explicitly waited idle.
- Internal heap diagnostics were added at progressive start, before cache store, and done:
  - `Free`
  - `Min`
  - `MaxAlloc`
- Vertical-reading swipe mapping was changed per request:
  - swipe right = next page
  - swipe left = previous page

## Log tags to check

```text
[PRG] foreground grouped progressive start: elements=... groupSize=2 ... cacheMissOnly=1
[PRG] internal heap start: Free=... Min=... MaxAlloc=...
[PRG] element ... rows=... draw=...
[PRG] group ... elements=2 rows=... displayCall=...
[PRG] internal heap before-cache-store: Free=... Min=... MaxAlloc=...
[ERS] render phase: displayIdleBeforeCache=... cacheStore=...
[PRG] internal heap done: Free=... Min=... MaxAlloc=...
[PRG] foreground grouped progressive done: elements=... groups=... displayCalls=...
```

## Notes

In v30 the first visible section appeared quickly, but total time grew because every element made a display call, then a final full-range refresh/status update and cache store added more time. v31 reduces display calls by grouping two elements at a time while keeping the final refresh for consistency.
