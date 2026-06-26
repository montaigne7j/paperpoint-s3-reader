# Reader Status Bar Margin Mode

This build adds a reader setting named `Status Bar Follows Margin` / `狀態列跟隨頁邊距`.

## Off / 關閉

The status bar stays at the bottom edge. The reading content automatically keeps at least enough bottom space to avoid the status bar.

```text
content bottom margin = max(reader screen margin, status bar height)
```

This is the default mode and is best for ordinary reading themes.

## On / 開啟

The status bar is moved inward by the reader screen margin. The reading content ends above the status bar.

```text
status bar bottom inset = reader screen margin
content bottom margin = reader screen margin + status bar height
```

This is intended for future framed/background reading themes where the status bar should sit inside the same visual frame as the text.

## Left and right margins

The reader screen margin is also applied to the left and right side of the status bar, so status text/progress aligns with the reading content.
