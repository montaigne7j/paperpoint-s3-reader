# Reader spacing / status-bar fix v2

## 1. Status-bar CJK ghosting

The previous fix scaled `UI_10_FONT_ID` and `SMALL_FONT_ID` CJK fallback glyphs down globally.  That solved the reader status-bar clipping, but it also changed every Classic/Lyra UI screen that uses those font IDs.

This version reverts the global compact-scaling path.  The embedded Traditional Chinese fallback is again drawn at its original 21x30 bitmap size for normal UI rendering.

The reader status bar is fixed locally instead: its title text is bottom-aligned with the Latin status text and the progress bar.  If a Chinese glyph is taller than the reserved status-bar area, it may protrude upward, but it should not extend downward into the progress bar and leave a ghost line.

## 2. Vertical line spacing vs character spacing

Vertical layout now separates two concepts:

- Reader line spacing controls the distance between vertical columns.
- Reader character spacing controls the distance between characters inside one vertical column.

This fixes the previous behavior where changing line spacing also changed the top-to-bottom distance between characters.

`Reader character spacing = 0 px` now uses a tight vertical advance instead of inheriting the wider horizontal line box.

## 3. Numeric +/- setting screens

Reader font size, reader line spacing, and reader character spacing now use focused +/- adjustment screens.

Changes apply immediately when pressing +/-; Back or Done only returns to the previous screen.  There is no longer a separate Select/confirm step required to apply the value.

## Cache note

The EPUB section cache version is bumped to 36 so chapters are reflowed with the corrected spacing model.
