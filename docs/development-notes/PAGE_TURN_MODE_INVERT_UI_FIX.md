# Page Turn Mode, Invert Reader Content, and UI Footer Button Fix (v25)

## Changes

1. Added a Controls setting: **Page Turn Refresh Mode**.
   - Original Refresh Mode: uses the normal EPD painter path.
   - Band-scan Mode: uses the tuned Paper S3 page-turn band-scan path.

2. Band-scan direction is selected by page-turn direction and reading layout.
   - Vertical layout:
     - Next page: logical right-to-left.
     - Previous page: logical left-to-right.
   - Horizontal layout:
     - Next page: logical left-to-right.
     - Previous page: logical right-to-left.

   Note: the physical gate scan must still be sent in panel row order. Reverse mode changes active band order rather than sending rows backward, avoiding mirrored or corrupted rows.

3. Invert Reader Content now fills the entire logical reader screen before content rendering.
   - The black background reaches all edges.
   - The status bar is drawn under the same inversion state.
   - Reader images remain inverted through the renderer inversion path.

4. Footer page-navigation buttons are shown only when there is a page to navigate to.
   - ButtonNavigator page navigation no longer wraps at first/last page.
   - Main list screens now hide Previous/Next footer labels when unavailable.
