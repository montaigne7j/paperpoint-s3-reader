# Page Turn Pending + Single Refresh Experiment V34

This build continues the V33 ready-cache/single-refresh experiment and changes
page-turn input handling from "consume while busy" to a deterministic one-slot
pending command.

## Goals

- Do not let multiple swipes stack up and skip pages.
- Do not abort the background frame-cache job just because the user swiped.
- Keep previous/next page turns equally fast once the adjacent page frame cache
  is ready.
- Force a full refresh when returning from UI to the reader, and when leaving an
  image page for a text page.

## Page-turn command model

The reader now has exactly one pending page-turn slot:

1. If render/cache is busy and a page-turn input arrives, the first page-turn is
   stored as pending.
2. Until that pending command is executed, any later page-turn input is consumed
   and ignored.
3. The pending command executes only after the adjacent frame caches are ready.
4. Once executed, the pending slot is cleared.

This turns the interaction into:

```text
swipe -> pending one command -> finish cache -> execute exactly one page turn
```

instead of:

```text
swipe -> cache still busy -> swipe again -> two pageTurn() calls -> skipped page
```

## Full-refresh cases

This build requests one full refresh in these cases:

- Returning from a pushed UI activity back to the reader.
- Visible page transition from an image-containing page to a text-only page.

The image-to-text check works for both frame-cache hits and cache misses because
frame-cache entries now record whether the stored page contains images.

## Notes

- The visible cache-miss path remains single-refresh, not grouped progressive.
- The background frame cache still prepares current/next/previous pages.
- Chapter-boundary turns still use the normal section-load path after the
  pending command executes.
