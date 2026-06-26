# Reader spacing, status bar, and Paper S3 shortcut fix

## Changes

### 1. Compact CJK status bar rendering
- Restored runtime scaled CJK fallback for `UI_10_FONT_ID` and `SMALL_FONT_ID` to 70%.
- Increased standard status bar vertical reserve from 19 px to 24 px.
- This prevents the small Chinese status title from drawing too close to the bottom/progress area and reduces the lower ghost/shadow seen in the status bar.

### 2. Numeric line spacing and character spacing
- `lineSpacing` is now numeric percent instead of a 3-state enum.
  - Range: 80% to 140%, step 5%.
  - Legacy values 0/1/2 migrate to 90/100/115%.
- Added `characterSpacing`.
  - Range: 0 to 12 px, step 1 px.
  - Default: 2 px.
- Section cache now stores character spacing and has been bumped to version 35.

### 3. Vertical layout spacing
- Vertical layout now applies reader line spacing to column advance.
- Vertical character advance now uses line spacing plus character spacing.
- This separates vertical column spacing from character spacing instead of relying on one mixed value.

### 4. Removed non-applicable Paper S3 settings from Settings list
- Removed side button layout setting from the shared visible settings list.
- Removed short-press power button behavior setting from the shared visible settings list.
- Existing internal fields remain for compatibility/migration.

### 5. Reader touch shortcuts
- In reader mode on Paper S3:
  - Tap the middle-upper screen area to open `Settings > Reader` directly.
  - Tap the middle-lower screen area to open the reader page menu.
- Hardware/zone confirm still opens the reader page menu.

### 6. Reader menu order
- `Go Home` is now placed immediately below `Select Chapter` in the reader menu.

## Notes
- Because spacing affects page layout, existing section caches will be invalidated once after this update.
- The small CJK status fix is runtime scaling only; it does not add a second embedded font and should not increase flash size significantly.
