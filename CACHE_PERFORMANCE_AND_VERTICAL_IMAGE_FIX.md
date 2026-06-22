# Chapter cache performance and vertical image layout fix

## Implemented changes

### 1. Detailed Section cache mismatch logs

`Section::loadSectionFile()` now logs the exact parameter that caused a cache invalidation instead of only:

```text
Parameters do not match
```

Examples:

```text
Cache mismatch: viewport cached=503x720 current=503x690
Cache mismatch: readingLayout cached=0 current=1
Cache mismatch: imageRendering cached=0 current=1
```

This makes it easier to distinguish a normal rebuild from an avoidable rebuild.

The Section cache version is now `34` because vertical image pagination and image cache paths changed.

### 2. EPUB-wide shared image extraction cache

Previously each chapter extracted its own copy:

```text
img_398_0.png
img_399_0.png
img_400_0.png
```

Repeated resources such as `OEBPS/Images/C1.png` are now stored once per EPUB cache folder:

```text
img_shared_<hash>.png
```

When a later chapter references the same EPUB image resource, the parser reuses the already extracted image instead of decompressing it from the EPUB ZIP again.

Expected log after first use:

```text
[EHP] Reusing shared image cache: /.crosspoint/.../img_shared_xxxxxxxx.png
```

### 3. Size-qualified image pixel cache

The `.pxc` pixel cache now includes display size in the filename:

```text
img_shared_xxxxxxxx_503x503.pxc
```

This allows the same source image to be reused safely at different display sizes without overwriting another cached render.

### 4. Vertical layout images become standalone pages

In vertical reading layout, images are now emitted as standalone centered pages:

```text
text page(s)
image-only page
text page(s)
```

This avoids the previous behavior where the image occupied the top of a vertical page and shortened all text columns on that page.

Expected log:

```text
[EHP] Vertical layout: image emitted as a standalone page
```

### 5. Less frequent indexing popup updates

Small chapters under 50 KB now show only the static indexing popup and do not continuously refresh the progress circle.

For larger chapters:

- 50 KB to 200 KB: progress updates at roughly 10% increments and at least 1 second apart
- 200 KB and above: progress updates at roughly 5% increments and at least 1 second apart

This reduces e-paper refresh overhead during indexing.

### 6. Delayed silent next-chapter indexing

Silent next-chapter indexing is no longer started immediately after rendering the penultimate page.

It is now scheduled and runs only after about 3.5 seconds of idle time near the end of the chapter. This reduces the feeling that the current page render or page turn is blocked by pre-indexing work.

Expected log:

```text
[ERS] Scheduled silent indexing for chapter N after idle delay
[ERS] Silently indexing next chapter: N
```

## Notes

The existing silent pre-index image-fallback protection is preserved. If an image fails during silent indexing, the degraded section cache is rejected instead of being persisted.
