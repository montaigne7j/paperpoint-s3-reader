# Page Skip Fast Tap R24

This update improves fast repeated page-turn input on Paper S3.

## Reader fast tap / page-skip behavior

- Single page turns keep the existing immediate path.
- Repeated taps while the reader is busy are accumulated within the current chapter.
- When the target page is known but the content is not rendered yet, the status bar uses a tentative page marker such as `4.../23`.
- The tentative marker is rendered only in the status area by using physical-row refresh, then the reader waits briefly for additional taps.
- After the debounce window, the reader renders only the final target page, avoiding wasted renders for intermediate pages.
- If a fast tap would cross a chapter boundary and the next/previous chapter page count is not known, accumulation stops at the boundary. The reader builds/loads that chapter and lands on the first page for forward turns or the last page for backward turns.
- While a chapter boundary is loading or indexing, additional page-turn taps are absorbed instead of being accumulated across unknown chapters.

## Cache and indexing notes

- Frame-cache warming is paused while a tentative status is active so a cache entry never stores a `4.../23` status bar.
- Silent indexing for the next chapter now starts earlier, from roughly the last five pages, and the idle delay is reduced to make the next chapter more likely to be ready before the boundary.

## Chapter selection footer

The chapter selection footer is changed to:

1. Exit
2. Parent
3. Previous page
4. Next page

Button 2 no longer selects a chapter. Parent / previous / next labels are hidden when that action is not available. Direct touch on a selected row can still activate the row.
