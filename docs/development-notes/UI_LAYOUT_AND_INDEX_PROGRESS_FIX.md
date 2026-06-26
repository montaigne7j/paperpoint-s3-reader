# UI Layout and Index Progress Fix

## This update includes
1. **Settings / list layout adjustment**
   - Increased classic list row height from 30 to 36 pixels.
   - Reserved a wider right-side value column for settings values.
   - Added truncation for long values to prevent clipping at the screen edge.
   - Centered list text vertically more consistently for the embedded CJK fallback.

2. **Settings top tab readability**
   - Increased tab spacing.
   - Added divider lines between category tabs so labels do not visually run together.

3. **Indexing popup improvement**
   - Enlarged popup box so the black frame is no longer shorter than the text.
   - Added a circular progress indicator under the popup text.
   - Added percentage text inside the circle.
   - EPUB indexing now reports incremental progress during parsing.

## Self-assessment
### Expected improvements
- Right-side values in Settings should be less likely to be clipped.
- Category labels such as 顯示 / 閱讀器 / 控制 / 系統 should be easier to distinguish.
- The indexing popup should now have a larger frame and a visible progress circle.

### Remaining risks
- The exact visual balance still depends on the chosen embedded CJK font metrics.
- English tightness may be improved indirectly by the new layout, but if it still looks too condensed, a separate Latin UI font tuning pass may still be needed.
- Full build verification could not be completed inside this environment because PlatformIO tried to download the platform package and network name resolution was unavailable.
