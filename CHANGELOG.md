# Changelog

## 1.6.0 — 2026-06-23 — Direct touch, reader typography, and firmware slimming

- Added direct-touch selection for the Home screen, Settings tabs, list-style screens, and multi-cover home layouts.
- Redesigned Paper S3 footer navigation as `Back / Select / Previous / Next`; Previous and Next now move by page instead of row.
- Added Reader Status Bar Margin Mode so the reader status bar can either stay at the bottom or follow the reading margins.
- Added a built-in bilingual EPUB user manual installed as `/book/CrossPoint_User_Manual.epub`.
- Updated the built-in Traditional Chinese reader fallback using the maintainer-supplied larger CJK raster source, with scalable reader metrics and improved vertical punctuation alignment.
- Slimmed firmware by removing embedded ReaderDyslexic families, reducing Noto Sans variants, and keeping only English hyphenation data.
- Kept Chinese-first documentation and web installer guidance aligned with the 1.6.0 browser flashing flow.

### Direct Touch Selection v4

- Added Reader Status Bar Margin Mode (`Status Bar Follows Margin` / `狀態列跟隨頁邊距`).
  - Off: status bar stays at the bottom; reading content uses `max(screen margin, status bar height)`.
  - On: status bar follows the bottom page margin; reading content uses `screen margin + status bar height`.
  - Left/right reader margins also apply to the status bar.
- Redesigned Paper S3 footer navigation as `Back / Select / Previous / Next`.
  - Previous/Next are page-level navigation actions in list-style screens.
  - Up/Down row movement is no longer used by the Paper S3 footer.
  - The Home screen hides the footer and is direct-touch driven.
- Fixed Traditional Chinese footer Back label.
  - `返回` no longer includes `<<` or `«`.
  - Chinese Large Text labels stay large; compact text is only used for long Latin labels.
- Added a built-in bilingual EPUB user manual.
  - Installed automatically as `/book/CrossPoint_User_Manual.epub` when browsing `/book`.

### Footer navigation and Settings tab layout

- Changed bottom virtual button wording from Up/Down style navigation to Back / Select / Previous / Next labels.
- Large Text theme now draws real footer labels instead of symbolic icons (`<<`, `o`, `^`, `v`).
- Large Text theme uses compact Latin labels for English footer buttons and Settings tabs, while keeping Chinese tab/footer labels large.
- Settings tab bars are now four equal-width cells in Classic, Lyra, and Large Text themes so visual tabs match direct-touch hit areas.
- Updated Traditional Chinese footer navigation labels to 前頁 / 後頁.


### Direct Touch Selection follow-up

- Added direct touch support for the Home Continue Reading card.
- Added direct touch support for the Settings category tab bar.
- Improved mixed Chinese/English UI label rendering by preferring the active UI font for printable ASCII and centering built-in Latin fallback glyphs within the UI line box.
- Hid the legacy Sleep Screen Cover Mode and Sleep Screen Cover Filter settings from the device/web settings list while retaining backward-compatible settings-file parsing.


### Larger CJK source and vertical spacing tuning

### Built-in CJK 31x39 source
- Replaced the embedded PaperPoint Sans TC source raster with the maintainer-supplied Noto Sans CJK TC Medium 23.5pt 31x39 bitmap derivative.
- Kept the historical 21x30 logical layout target and resampled the larger 31x39 source into 21x30 * reader-scale targets.
- Preserved the reader CJK scale range at 0.8x..2.5x with default 1.5x at reader font size 36.
- Resampled UI fallback glyphs back to the old logical size so Classic/Lyra UI is not globally enlarged by the larger source raster.

### Vertical spacing
- Changed vertical CJK layout to use a tighter visible-ink advance so character spacing 0px is visibly closer while avoiding overlap.
- Bumped EPUB section cache version to 38.


### Flash footprint reduction for larger CJK font experiments

### Reader font slimming
- Removed the embedded ReaderDyslexic font families from firmware. Existing settings that selected ReaderDyslexic now migrate back to the built-in NotoSans reader fallback.
- Reduced embedded NotoSans reader fonts to NotoSans 14 Regular/Bold, NotoSans 16 Regular/Bold, and NotoSans 8 Regular for small UI text.
- Removed embedded NotoSans Italic/BoldItalic, 12 px, and 18 px reader variants. Italic EPUB styling now falls back to Regular; BoldItalic falls back to Bold.

### Hyphenation slimming
- Kept only English Liang hyphenation data in firmware. Other language tries are excluded because Chinese reading does not need them and English remains useful for mixed Latin text.


### Vertical punctuation and scalable CJK reader font

### Vertical punctuation and alignment fix
- Use vertical presentation forms for CJK brackets/quotes before falling back to rotation.
- Center built-in primary font glyphs in vertical cells so ASCII letters and numbers align with CJK glyphs.

### Built-in CJK scalable reader font
- Scale the embedded CJK fallback in reader layout from 0.8x to 2.5x, with default 1.5x at reader font size 36.
- Apply the scaled CJK metrics to horizontal and vertical layout calculations.
- Bump section cache version to 37.


## 1.5.0 — 2026-06-22 — Reader value adjust compile fix

- Fixed `ReaderValueAdjustActivity` dynamic title translation by replacing `tr(titleId)` with `I18N.get(titleId)`.


## 2026-06-21 — Reader spacing/status-bar fix v2

- Reverted global compact scaling of `UI_10_FONT_ID` / `SMALL_FONT_ID` CJK fallback glyphs.
- Fixed reader status-bar CJK ghosting by bottom-aligning status title text instead of shrinking global UI fonts.
- Separated vertical column spacing from vertical character spacing.
- Made reader font size, line spacing, and character spacing +/- pickers apply immediately.
- Bumped EPUB section cache version to 36.


## 2026-06-21 — Reader spacing and Paper S3 shortcut tuning

- Restored compact runtime CJK scaling for UI_10/SMALL fonts and expanded status bar vertical reserve to reduce Chinese status-bar bottom ghosting.
- Converted reader line spacing to numeric percent and added separate numeric reader character spacing.
- Applied vertical layout spacing to both vertical character advance and column advance.
- Removed non-applicable side-button layout and short-press power-button behavior settings from the settings list.
- Added reader touch shortcuts: middle-upper opens Settings > Reader, middle-lower opens reader page menu.
- Moved Go Home directly below Select Chapter in the reader menu.
- Bumped EPUB section cache version to 35.

## 1.5.0 — 2026-06-22 — Chapter cache performance and vertical image layout fix

- Added detailed Section cache mismatch logs so cache invalidation shows the exact changed parameter.
- Added EPUB-wide shared image extraction cache for repeated resources such as chapter ornaments.
- Added size-qualified `.pxc` image render cache names so one source image can be cached at multiple display sizes safely.
- Changed vertical reading layout images to standalone centered image pages.
- Reduced indexing popup refresh frequency; small chapters now use a static popup only.
- Delayed silent next-chapter indexing until the reader has been idle near the end of a chapter.

## 1.4.0 — 2026-06-21 — Large UI and Traditional Chinese release

- Added the **Large Text** UI theme for settings, file browser, recent books, reader menu, and reader status bar.
- In the Large Text theme, the reader clock and chapter/book title share the same left status-bar slot; enabling one hides the other.
- Changed Large Text bottom hints to compact symbols (`<<`, `o`, `^`, `v`) to keep the oversized UI readable.
- Added the built-in **PaperPoint Sans TC Medium** Traditional Chinese fallback font and made it the built-in UI CJK fallback.
- Updated the Chinese README and web installer page with version 1.4.0 notes, browser flashing steps, supported sleep image formats, and current feature limitations.

## 2026-06-21 — EPUB image cache reliability fix

- Prevented silent next-chapter indexing from persisting transient image failures as permanent `[Image: alt]` section caches.
- Added three-attempt retry handling for EPUB PNG/JPEG extraction and image-dimension reads.
- Increased the section cache format from version 28 to 29 so previously degraded caches rebuild automatically.
- Kept foreground parsing tolerant for genuinely unsupported or damaged images.

## 2026-06-21 — Built-in CJK compile fix

- Restored the missing `#ifndef OMIT_FONTS` guard removed during the legacy font cleanup.
- Kept Noto Sans 14 available outside the optional-font guard so reduced builds retain a default reader font.
- Moved Noto Sans 14 registration outside the matching `OMIT_FONTS` block.
- Revalidated all conditional-compilation directives in `src/main.cpp`.

## 2026-06-20 — Licence compliance remediation

- Removed the proprietary built-in reading font and all generated/source references.
- Migrated the default reader font to Noto Sans with backward-compatible settings conversion.
- Renamed generated OpenDyslexic and Ubuntu derivatives to comply with font naming terms.
- Added EPD_Painter, GC16, hyphenation, font, LGPL release, asset, SBOM and third-party notices.
- Pinned PlatformIO library versions and added automated compliance/SBOM/relink-kit checks.
- Added packaging of the exact resolved ArduinoWebSockets and Arduino-ESP32 source trees for each binary release.

## 2026-06-21 — Earlier Paper S3 baseline work

### UI / UX
- **Vertical text centering** in all list rows (file browser, settings, recent books) — text, icons, subtitles, and values are now properly centered within their row height using dynamic font metrics instead of hardcoded pixel offsets
- **Tap-friendly row heights** for in-book menus on PaperS3: chapter selection, footnotes, and reader menu rows increased from 30–36px to 75px with vertically centered text
- **Lyra 3-Covers theme PaperS3 fix** — added PaperS3-specific metrics (120px menu/list rows, 16px spacing) so touch hit-testing matches the rendered layout; previously all taps mapped to wrong items due to metrics mismatch (64px vs 120px)
- **Multi-cover touch selection** — tapping a specific book cover in the 3-cover home layout now opens that book instead of always opening the first one (uses touch X coordinate)
- **Boot splash footer** aligned with in-app status bar position and pushed 2px higher to prevent descender ghosting (letters like j, g)
- **Reader status bar footer** pushed 2px higher to avoid descender mirroring at the screen edge
- **First-open cover skip** — books now skip the cover page on first open; uses a proper `isFirstOpen` flag (progress.bin existence) instead of the unreliable `spineIndex == 0` check, with fallback to spine 1 when no text reference is found

### Rendering
- **Background-prepared custom sleep images** on Paper S3: `.bmp`, `.jpg`/`.jpeg`, and `.png` files are decoded and converted to validated GC16 caches while the device is idle, so shutdown never waits for image processing
- **Arbitrary-size sleep images**: opaque images preserve aspect ratio and use fixed center-crop fill for the 540×960 screen; transparent PNG overlays preserve aspect ratio, fit entirely without cropping, and blend with the current reading page or a white background
- **Fail-safe sleep fallback**: shutdown uses the newly prepared cache, then the previous valid cache, then the built-in sleep screen; incomplete `.tmp` files are never displayed
- **Force full e-ink refresh** on every activity transition (PaperS3) to eliminate ghosting artifacts
- **VIEWABLE_MARGIN_BOTTOM** reduced from 22 to 16 to reclaim empty space under the footer

### Performance / Stability
- **JPEGDEC stack overflow fix** — the ~16KB JPEGDEC object is now heap-allocated in PSRAM via placement new instead of overflowing the 8KB render task stack
- **Fast JPEG thumbnail path** — on PaperS3, cover thumbnails are decoded directly from PSRAM using JPEGDEC at 1/8 scale with Floyd-Steinberg dithering, bypassing the slow picojpeg + temp file path
- **JPEGDEC patches** for progressive JPEG support (skip AC Huffman tables) and MCU_SKIP guard to prevent crashes on grayscale chroma skip
- **Build optimization** — `-O2` with `-Os` removed from build_unflags so the speed optimization actually takes effect

### CI / Release
- **GitHub Actions**: fixed `upload-artifact@v6` → `@v4` in CI workflow (v6 doesn't exist)
- **GitHub Release creation**: release workflow now creates a GitHub Release with firmware binaries attached via `softprops/action-gh-release@v2`

### Credits
- Display driver: [EPD_Painter](https://github.com/nickoala/EPD_Painter) by nickoala
### Built-in Traditional Chinese font

- Embedded a compact 21×30 **PaperPoint Sans TC Medium** fallback generated from the maintainer-supplied Noto Sans CJK TC Medium raster.
- Added exact-glyph lookup so proportional Latin families no longer hide missing CJK glyphs behind U+FFFD.
- Added mixed Latin/CJK measurement and rendering for horizontal and vertical text.
- Replaced the 5.9 MB direct-index raster at runtime with 31,338 sparse cropped glyphs (about 1.86 MiB including metadata), with no runtime decompression.
- Stopped auto-forcing the legacy SD-card UI font; saved external choices still override the embedded fallback.
- Added OFL licence, provenance, generator, SBOM entry and compliance checks.
- Restored the referenced `partitions.csv` with two 7 MiB OTA application slots so the larger firmware can still use OTA.
