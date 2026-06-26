# Traditional Chinese UI language update

This version keeps only two system UI languages:

- Traditional Chinese (`ZH_TW`)
- English (`EN`)

Traditional Chinese is the default language for a fresh installation and after migrating from the previous language-settings format.

The language can be changed at:

`設定 → 系統 → 系統語言`

The language settings file version was increased from 1 to 2. On the first boot after updating, an older `/.crosspoint/language.bin` is reset once to Traditional Chinese and rewritten in the new format.

Translation sources are in:

- `lib/I18n/translations/english.yaml`
- `lib/I18n/translations/traditional_chinese.yaml`

After editing either translation file, regenerate the C++ tables with:

```powershell
python scripts/gen_i18n.py lib/I18n/translations lib/I18n/
```
## Built-in Traditional Chinese glyph fallback

The firmware now embeds **PaperPoint Sans TC Medium**, a 31×39 fixed-cell bitmap derivative resampled to the historical logical 21×30 UI/reader grid generated from the maintainer-supplied Noto Sans CJK TC Medium raster. It is automatically used for CJK/full-width codepoints when no external UI font is active, so Traditional Chinese UI text no longer depends on an SD-card font.

An explicitly selected external UI font still takes priority. Missing glyphs can fall back to the embedded font. See `BUILTIN_CJK_FONT.md` and `LICENSES/OFL-1.1-NotoSansCJK.txt`.
