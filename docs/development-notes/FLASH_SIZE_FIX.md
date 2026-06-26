# Flash Size Fix

## Failure
The linked program was 8,091,063 bytes while each OTA application slot is 7,340,032 bytes.
The overage was 751,031 bytes.

## Root cause
The compact 15x21 CJK font duplicated the complete 31,338-glyph bitmap and glyph-index tables.
Its compiled contribution was approximately 1.2 MiB.

## Fix
- Removed the separate `paperpoint_sans_tc_11_compact.h` font.
- Kept the 21x30 embedded Traditional Chinese source font.
- UI_10 and SMALL_FONT now render the same source glyphs at 100% using area-sampled scaling.
- Width, ascender, line-height and rotated-text calculations use the same 100% scale.
- Kept the two 7 MiB OTA application slots.

## Expected result
Removing the duplicate compact font should reduce the firmware by roughly 1.2 MiB, taking the previous 8.09 MB image to about 6.8-6.9 MB. Actual output must be confirmed by PlatformIO on the target build machine.
