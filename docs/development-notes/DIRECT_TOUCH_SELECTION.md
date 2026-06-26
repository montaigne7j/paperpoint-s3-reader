# Direct Touch Selection

This build adds **Direct Touch Selection** for Paper S3 non-reading/list UI screens while keeping the existing bottom virtual buttons as the primary fallback controls.

## Behavior

For list-style screens:

1. Tap an unselected row to move the selection to that row.
2. Tap the currently selected row again to activate it.
3. Bottom virtual buttons remain unchanged.
4. File Browser keeps using the existing partial row redraw path when the selection moves within the same page.

## Input model

`HalGPIO` now exposes content-area taps separately when footer mode is active:

- Footer button area still maps to Back / Confirm / Up / Down.
- Top-left power hotspot still maps to Power.
- Content-area single-finger taps do not become virtual buttons.
- Content-area taps are exposed through `MappedInputManager::wasContentTapReleased()` and the associated content tap coordinates.

A small movement threshold is used so obvious drags do not trigger row activation.

## Screens updated

Direct row selection was added to these list-style screens:

- Home Continue Reading card
- Home menu rows
- File Browser
- Recent Books
- Reader Menu
- Select Chapter / TOC
- Settings category tabs
- Settings setting rows
- Reader Font Select
- Status Bar Settings
- Network Mode Selection
- Wi-Fi network list
- Language selection
- Calibre / OPDS settings
- KOReader settings

## Notes

The reading page itself is intentionally unchanged. It still uses the existing reader touch zones and reader menu behavior.

## Follow-up fixes

- The Home Continue Reading card now participates in Direct Touch Selection: tap once to select it, tap it again to open the current book.
- The Settings category tab bar now accepts direct taps on Display / Reader / Controls / System.
- Mixed UI labels prefer the active UI font for printable ASCII when available, so English letters and digits align better with Chinese text.
- The old Sleep Screen Cover Mode and Sleep Screen Cover Filter controls are hidden from settings because they no longer match the active Paper S3 sleep-screen workflow.
## Footer navigation label update

The Paper S3 footer still uses four equal touch zones, but the visible labels now reflect the intended navigation model:

1. Back
2. Select
3. Previous
4. Next

Large Text theme no longer replaces the footer labels with abstract symbols. English footer labels use a compact Latin scale so `Previous` fits cleanly; Chinese labels keep the larger UI scale.

## Settings tab hit area update

Settings category tabs are rendered as equal-width cells across the full screen. The touch hit area and visual tab cell are now the same region, which prevents enlarged tap targets from overlapping adjacent tabs. In Large Text theme, English tab labels use compact Latin scale when needed, while Chinese tab labels remain large.

## v4 behavior notes

- Paper S3 Home hides the footer buttons and is operated by direct touch.
- Footer buttons on list-style screens are now Back / Select / Previous / Next.
- Previous / Next jump by visible page, preserving the selected row position when possible.
- Individual row movement is handled by direct touch selection instead of Up / Down buttons.
- Traditional Chinese footer labels use plain text, for example `返回`, `選擇`, `前頁`, `後頁`; large Chinese labels remain large.
- Long English labels in Large Text mode use compact Latin text so they fit the four footer zones.
