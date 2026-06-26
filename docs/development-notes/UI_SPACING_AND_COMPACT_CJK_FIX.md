# UI spacing and compact CJK rendering

The firmware keeps one embedded 21x30 Traditional Chinese bitmap font.
For `UI_10_FONT_ID` and `SMALL_FONT_ID`, glyphs are rendered at 100% (full 21x30) at runtime using area sampling.
This avoids embedding a second complete 31,338-glyph font while retaining compact Settings and status-bar text.

Other spacing changes remain:
- Settings row height: 42 px
- Right-side value column: 170 px
- Latin pair tracking: +2 px
- Vertical reading glyph gap: +4 px
- Vertical column gap: at least +6 px
