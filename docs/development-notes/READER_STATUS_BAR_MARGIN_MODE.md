# Reader Status Bar Margin Mode

This build adds a reader setting named `Status Bar Follows Margin` / `狀態列跟隨頁邊距`.

## Off / 關閉

The status bar stays at the bottom edge. The reading content automatically keeps at least enough bottom space to avoid the status bar.

```text
status reserve = actual status text/progress height + 12 px safe gap
content bottom margin = max(reader screen margin, status reserve)
```

This is the default mode and is best for ordinary reading themes.

## On / 開啟

The status bar is moved inward by the reader screen margin. The reading content ends above the status bar.

```text
status reserve = actual status text/progress height + 12 px safe gap
status bar bottom inset = reader screen margin
content bottom margin = reader screen margin + status reserve
```

This is intended for future framed/background reading themes where the status bar should sit inside the same visual frame as the text.

## Left and right margins

The reader screen margin is also applied to the left and right side of the status bar, so status text/progress aligns with the reading content.

## V1.8.4-r22 spacing correction

Earlier builds used `getStatusBarHeight()` as the reader-layout reserve. That value can be smaller than the actual CJK title glyph height in the normal themes, so the last EPUB/TXT line could sit too close to the status bar when the status bar was moved upward by the page margin.

The layout now computes the visible status-bar height from the active progress bar, side status text, and title font metrics, then adds a 12 px safety gap. EPUB visible rendering, EPUB background page-frame cache warming, and TXT layout share this reserve calculation.
