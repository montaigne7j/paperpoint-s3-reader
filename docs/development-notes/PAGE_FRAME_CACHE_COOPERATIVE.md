# Page Frame Cache Cooperative Experiment (v21)

This experiment keeps the v20 framebuffer cache behavior, but changes cache warm from a single blocking page render into cooperative chunks.

## Goal

Reduce missed or delayed taps while keeping the four-slot page framebuffer cache.

## Behavior

- The visible page turn path is still first priority.
- If the requested page is already cached, it is restored and displayed immediately.
- If the requested page is not cached, the visible page is rendered synchronously as before.
- Background cache warm only starts after the idle delay.
- A background cache warm page is split into cooperative chunks.
- Between chunks, the reader loop can process touch/button input.
- If input appears while a cache warm job is in progress, the partial warm job is aborted.
- Cache entries are still selected by metadata: `spineIndex + pageNumber`.
- No framebuffer slot data is shifted when turning pages.
- Existing cached pages are not rendered again.

## Cache warm priority

When idle, the warm candidates remain:

1. Next page
2. Current page
3. Page after next
4. Previous page

Only missing pages are warmed.

## New cooperative parameters

Defined in `src/activities/reader/EpubReaderActivity.cpp`:

```cpp
constexpr uint8_t pageFrameCacheCooperativeChunks = 20;
constexpr unsigned long pageFrameCacheChunkBudgetMs = 45;
```

A cache page is rendered in roughly 20 chunks. Each chunk also has a soft time budget. After each chunk the main loop returns, so input can be handled before the next chunk.

## Expected result

Cache hits should remain fast, while a background warm render should no longer block the UI for 500-900 ms in one continuous operation.

If a user taps during a warm job, the job is aborted and the page turn proceeds normally:

- cache hit: show cached page
- cache miss: render the target page immediately
