# R31 CBZ preload pending page-turn fix

R30 delayed preload to reduce missed taps, but the intended behavior is different:

```text
preload is running
user taps next
preload finishes
reader immediately performs next-page action
```

R31 implements this.

## Implementation

During `preloadFrame()` the CBZ reader now polls input at three points:

1. after ZIP extraction,
2. after hidden image decode,
3. immediately before returning from preload.

If `PageForward` or the left comic tap zone is detected, it sets:

```cpp
pendingPageDelta = +1;
```

If `PageBack` or the right comic tap zone is detected, it sets:

```cpp
pendingPageDelta = -1;
```

After preload completes, the pending delta is applied immediately via `applyPageDelta(delta)`. When the pending target is the page that was just preloaded, the decoded frame cache remains valid and the next render should hit the cache.

## Expected log

```text
Queued pending next-page input during preload
Preloaded decoded frame cache for page XX
Applying pending page delta 1 after preload
Displayed page XX from decoded frame cache
```
