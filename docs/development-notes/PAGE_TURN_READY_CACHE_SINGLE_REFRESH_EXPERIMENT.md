# Page Turn Ready-Cache Single Refresh Experiment

This build simplifies the Paper S3 reader page-turn path.

## Changes

- Visible cache-miss pages no longer use foreground grouped progressive rendering.
  - The page is rendered to the framebuffer once.
  - The framebuffer is displayed once through the normal page-turn refresh cycle.
- Reader page-turn inputs are ignored while the renderer/cache is busy.
- Previous/next page turns are gated by adjacent same-section framebuffer cache readiness.
  - The current visible page is stored to the frame cache after render.
  - Background cache work prepares the current page if missing, then the next page, then the previous page.
  - While that background frame cache job is active or an adjacent same-section target is not cached yet, previous/next inputs are consumed as no-op.
- Background frame-cache rendering is no longer aborted by page-turn input. Input is drained while the cache job finishes.

## Goal

Make the page-turn behavior deterministic and remove the race that allowed a second swipe during a slow visible render to modify `section->currentPage` before the first render finished storing cache/progress.

Once adjacent cache preparation is complete, both previous and next same-section page turns should normally be cache hits, so their visible speed should be similar.

## Notes

Chapter-boundary turns are still allowed through the normal section-load path to avoid permanently blocking on a page outside the current `Section`. Existing silent next-chapter indexing remains in place.
