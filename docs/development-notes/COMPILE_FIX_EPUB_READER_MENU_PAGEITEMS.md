# Compile Fix: EpubReaderMenuActivity pageItems

## Problem

`EpubReaderMenuActivity.cpp` failed to compile in the LARGE_TEXT render path:

```text
error: 'pageItems' was not declared in this scope; did you mean 'menuPageItems'?
```

## Cause

The v25 footer button-hint update reused `pageItems` in the LARGE_TEXT render branch, but that variable only existed in the loop/touch-navigation branch, not in the render branch.

## Fix

Define `pageItems` locally in the LARGE_TEXT render branch from the visible list area:

```cpp
const int pageItems = std::max(1, listHeight / std::max(1, metrics.listRowHeight));
const int menuPageItems = std::max(1, pageItems - summaryRows);
```

This keeps the footer previous/next hint visibility behavior from v25 while restoring compilation.
