# Low-memory background-work guard — r7 experiment

This experiment reduces Paper S3 reader instability seen when internal heap becomes highly fragmented during long EPUB reading sessions.

## Guard threshold

Background reader work is paused when either condition is true:

```text
Internal Free < 12000 bytes
Internal MaxAlloc < 4096 bytes
```

The guard checks `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT` because the crash logs showed very low internal heap and largest-free-block values.

## Affected work

- Background same-section page frame-cache warming.
- Silent next-chapter indexing near chapter end.

## Page-turn behavior

When memory is low, user page turns bypass cache-gated waiting and fall back to the visible render path. This avoids a pending-turn deadlock where the reader waits for a cache job that is intentionally paused.

## Diagnostic log

```text
[DBG] [ERS] Background work paused: feature=frame-cache free=... maxAlloc=... thresholds free>=12000 maxAlloc>=4096
```

The log is rate-limited to avoid flooding Serial during low-memory states.
