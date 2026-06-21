# EPUB chapter image cache reliability fix

## Problem

A chapter-opening PNG could render correctly after clearing the EPUB cache, but
later chapters could become `[Image: alt]` again.

The reader silently pre-indexes the next chapter while the current chapter's
penultimate page is displayed. A temporary SD-card, ZIP-stream, or image-header
read failure was previously converted to alt text and then persisted as a valid
section cache. Entering that chapter reused the degraded cache without running
the image parser again.

## Changes

- Section cache format version increased from 28 to 29, invalidating old degraded
  section caches automatically.
- PNG/JPEG extraction is retried up to three times.
- Image dimension parsing is retried up to three times.
- The parser records when display-mode image loading falls back to alt text.
- Silent next-chapter indexing rejects and deletes a section cache if any image
  fell back because loading failed.
- Foreground chapter loading remains tolerant: after the retries, genuinely
  unsupported or damaged images can still use alt text instead of making the
  whole chapter unreadable.

## Expected logs

A temporary failure during silent indexing may show:

```text
[EHP] Retrying image extraction (2/3): ...
[EHP] Image load failed in display mode; marking section cache as degraded: ...
[SCT] Rejecting pre-indexed section ... because an image fell back to alt text
[ERS] Failed silent indexing for chapter: ...
```

This is now safe. The bad section file is removed and the chapter is rebuilt
when it is actually opened.
