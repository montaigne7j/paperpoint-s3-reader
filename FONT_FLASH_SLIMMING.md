# Font flash slimming

This build reduces embedded Latin reader font and hyphenation data to recover app partition space for larger Traditional Chinese bitmap font experiments.

## Changes

### ReaderDyslexic removed from firmware

Removed the embedded ReaderDyslexic 8 / 10 / 12 / 14 Regular / Bold / Italic / BoldItalic font families. Existing settings that selected ReaderDyslexic are migrated back to the built-in NotoSans reader fallback.

### NotoSans reduced

Kept only the embedded Latin fonts still needed by the PaperPoint Chinese-first reader:

- NotoSans 14 Regular / Bold
- NotoSans 16 Regular / Bold
- NotoSans 8 Regular for small UI text

Removed embedded NotoSans 12, NotoSans 18, Italic, and BoldItalic variants. EPUB italic styling now falls back to Regular, and BoldItalic falls back to Bold through `EpdFontFamily::getFont()`.

### Hyphenation reduced

Kept only English hyphenation data. Chinese text does not use Liang hyphenation, and English remains helpful for mixed Latin text.

## Validation

Measure local firmware size after rebuilding:

```powershell
pio run -t clean
pio run -e default

$slot = 0x700000
$bin = (Get-Item ".pio\build\default\firmware.bin").Length
$free = $slot - $bin

"firmware.bin = {0:N0} bytes" -f $bin
"OTA slot     = {0:N0} bytes" -f $slot
"free space   = {0:N0} bytes" -f $free
```

Recommended tests after flashing:

1. Built-in font Chinese horizontal and vertical reading.
2. Mixed English / numbers in vertical reading.
3. Bold text in EPUB / UI still appears bold.
4. Italic EPUB text remains readable, even though it now falls back to regular.
5. Hyphenation toggle still works for English and has no visible negative effect on Chinese text.
