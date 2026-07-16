# r33 Stability, Sleep Image Picker, and Paragraph Indent

## 1. Paper S3 CPU stability policy

The former first-stage idle policy changed CPU frequency from the boot value (normally 240 MHz) to 80 MHz after three seconds. r33 disables this transition in two places:

- `src/main.cpp` never requests low-power frequency on Paper S3.
- `HalPowerManager::setPowerSaving()` forces `enabled=false` for Paper S3, preventing a future caller from restoring the transition accidentally.

The normal short main-loop delay remains. Auto-sleep and power-off still render the configured sleep image and enter the existing deep-sleep / PMIC-off path. Non-Paper targets retain their prior low-frequency policy.

## 2. Custom sleep image picker

`SleepImageSelectActivity` provides three root entries: Random, `/.sleep`, and `/cover`. Folder navigation is recursive. Supported list and preview formats are BMP, JPG/JPEG, and PNG.

Touch behavior follows the file/font picker convention:

1. First tap selects a row.
2. Second tap on the selected image, or Confirm, opens preview.
3. Preview Cancel returns without changing settings.
4. Preview Confirm stores `sleepCustomImagePath`, invalidates `last.txt`, re-scans candidates, and returns to the same folder.

An empty `sleepCustomImagePath` means Random. Random recursively scans `/.sleep`, `/cover`, and legacy `/sleep` to depth four. Directory handles are closed before recursive descent to avoid consuming many SD handles.

## 3. Two-character paragraph first-line indent

The setting `paragraphFirstLineIndent` is available in the Reader category.

### EPUB

Normal left-like paragraphs prepend two U+3000 ideographic spaces. Negative CSS text-indent values are preserved for hanging-indent/list structures. The behavior applies to horizontal and vertical layout. Section cache version is 40 and the setting is part of the cache header validation.

### TXT

Each source text line is treated as a paragraph. Only the first wrapped segment receives two ideographic-space widths; a segment continued on the next page is not indented again. The same width reduction is used while building the page index and rendering, so pagination remains consistent. TXT index cache version is 4 and includes the setting value.

## 4. Validation performed

- Regenerated the English and Traditional Chinese i18n tables: 344 keys.
- Parsed the new selector and the modified cache/layout units with a C++ syntax parser; no parser errors were found in those units. Existing macro-related parser limitations in untouched baseline regions remain unchanged.
- Verified EPUB and TXT cache signatures include the new setting.
- Verified Paper S3 has no reachable `setPowerSaving(true)` path and the power manager rejects such requests at runtime.

A complete PlatformIO firmware build still requires the external pioarduino platform and library packages.
