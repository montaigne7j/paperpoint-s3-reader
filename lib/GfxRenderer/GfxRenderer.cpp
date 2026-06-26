#include "GfxRenderer.h"

#include <FontDecompressor.h>
#include <FontManager.h>
#include <Logging.h>
#include <Utf8.h>
#include "../../src/fontIds.h"
#include "../../src/CrossPointSettings.h"

#include <algorithm>
#include <cassert>
#include <cmath>

#include "ExternalFontHelpers.h"
#include "FontCacheManager.h"

#include <cstring>

namespace {

// Keep UI font classification in sync with generated src/fontIds.h.
constexpr int UI_FONT_IDS[] = {
    UI_10_FONT_ID,
    UI_12_FONT_ID,
    SMALL_FONT_ID,
};

constexpr int UI_FONT_COUNT =
    sizeof(UI_FONT_IDS) / sizeof(UI_FONT_IDS[0]);

bool isUiFontIdForFallbackScaling(const int fontId) {
  for (int i = 0; i < UI_FONT_COUNT; ++i) {
    if (fontId == UI_FONT_IDS[i]) return true;
  }
  return false;
}

constexpr int READER_CJK_SCALE_MIN_PERCENT = 80;
constexpr int READER_CJK_SCALE_DEFAULT_PERCENT = 150;
constexpr int READER_CJK_SCALE_MAX_PERCENT = 250;

// The embedded CJK bitmap source is intentionally larger (31x39) than the
// logical reader grid used by existing layout tuning (21x30).  Reader font
// size still maps to the old logical target 21x30 * scale, then the 31x39
// source is resampled into that target.  This keeps the default size stable
// while improving quality by mostly downsampling / lightly scaling the larger
// source instead of enlarging a 21x30 bitmap.
constexpr int BUILTIN_CJK_SOURCE_CELL_WIDTH = 31;
constexpr int BUILTIN_CJK_SOURCE_CELL_HEIGHT = 39;
constexpr int BUILTIN_CJK_LOGICAL_CELL_WIDTH = 21;
constexpr int BUILTIN_CJK_LOGICAL_CELL_HEIGHT = 30;

int scaleMetricPercent(const int value, const int percent) {
  return (value * percent + 50) / 100;
}

int scaleMetricPercentCeil(const int value, const int percent) {
  return (value * percent + 99) / 100;
}

int readerBuiltinCjkScalePercent() {
  const int size = SETTINGS.fontSize;
  if (size <= CrossPointSettings::READER_FONT_SIZE_MIN) return READER_CJK_SCALE_MIN_PERCENT;
  if (size >= CrossPointSettings::READER_FONT_SIZE_MAX) return READER_CJK_SCALE_MAX_PERCENT;

  // Piecewise-linear mapping chosen so the current default reader size (36)
  // becomes the new default built-in CJK scale of 1.5x, while the full setting
  // range maps to 0.8x .. 2.5x.
  if (size <= CrossPointSettings::READER_FONT_SIZE_DEFAULT) {
    const int span = CrossPointSettings::READER_FONT_SIZE_DEFAULT - CrossPointSettings::READER_FONT_SIZE_MIN;
    const int pos = size - CrossPointSettings::READER_FONT_SIZE_MIN;
    return READER_CJK_SCALE_MIN_PERCENT +
           ((READER_CJK_SCALE_DEFAULT_PERCENT - READER_CJK_SCALE_MIN_PERCENT) * pos + span / 2) / span;
  }

  const int span = CrossPointSettings::READER_FONT_SIZE_MAX - CrossPointSettings::READER_FONT_SIZE_DEFAULT;
  const int pos = size - CrossPointSettings::READER_FONT_SIZE_DEFAULT;
  return READER_CJK_SCALE_DEFAULT_PERCENT +
         ((READER_CJK_SCALE_MAX_PERCENT - READER_CJK_SCALE_DEFAULT_PERCENT) * pos + span / 2) / span;
}

int fallbackScalePercentForFontId(const int fontId) {
  // UI fonts keep the original CJK fallback size; reader fonts get scalable
  // built-in CJK so font-size changes affect CJK glyphs, layout and cache.
  return isUiFontIdForFallbackScaling(fontId) ? 100 : readerBuiltinCjkScalePercent();
}

bool usesScaledReaderCjkFallback(const int fontId) {
  (void)fontId;
  // The source bitmap is 31x39, while the logical UI/reader target is still
  // based on the historical 21x30 grid.  Always use the percent resampling
  // path for fallback glyphs; UI font IDs use 100%, reader IDs use 0.8x..2.5x.
  return true;
}

uint32_t verticalPresentationForm(const uint32_t codepoint) {
  switch (codepoint) {
    case 0x300C: return 0xFE41;  // 「 -> ﹁
    case 0x300D: return 0xFE42;  // 」 -> ﹂
    case 0x300E: return 0xFE43;  // 『 -> ﹃
    case 0x300F: return 0xFE44;  // 』 -> ﹄

    case 0x0028:
    case 0xFF08: return 0xFE35;  // ( / （ -> ︵
    case 0x0029:
    case 0xFF09: return 0xFE36;  // ) / ） -> ︶

    case 0x007B:
    case 0xFF5B: return 0xFE37;  // { / ｛ -> ︷
    case 0x007D:
    case 0xFF5D: return 0xFE38;  // } / ｝ -> ︸

    case 0x3014: return 0xFE39;  // 〔 -> ︹
    case 0x3015: return 0xFE3A;  // 〕 -> ︺
    case 0x3010: return 0xFE3B;  // 【 -> ︻
    case 0x3011: return 0xFE3C;  // 】 -> ︼
    case 0x300A: return 0xFE3D;  // 《 -> ︽
    case 0x300B: return 0xFE3E;  // 》 -> ︾
    case 0x3008: return 0xFE3F;  // 〈 -> ︿
    case 0x3009: return 0xFE40;  // 〉 -> ﹀

    case 0x005B:
    case 0xFF3B: return 0xFE47;  // [ / ［ -> ﹇
    case 0x005D:
    case 0xFF3D: return 0xFE48;  // ] / ］ -> ﹈

    default: return 0;
  }
}

bool isCjkCodepoint(const uint32_t cp) {
  // CJK Unified Ideographs
  if (cp >= 0x4E00 && cp <= 0x9FFF) return true;

  // CJK Extension A
  if (cp >= 0x3400 && cp <= 0x4DBF) return true;

  // CJK Extensions B-F / Compatibility supplements
  if (cp >= 0x20000 && cp <= 0x2FA1F) return true;

  // CJK symbols and punctuation
  if (cp >= 0x3000 && cp <= 0x303F) return true;

  // Hiragana
  if (cp >= 0x3040 && cp <= 0x309F) return true;

  // Katakana
  if (cp >= 0x30A0 && cp <= 0x30FF) return true;

  // Bopomofo
  if (cp >= 0x3100 && cp <= 0x312F) return true;
  if (cp >= 0x31A0 && cp <= 0x31BF) return true;

  // CJK compatibility
  if (cp >= 0x3200 && cp <= 0x33FF) return true;

  // Full-width forms
  if (cp >= 0xFF00 && cp <= 0xFFEF) return true;

  return false;
}

bool isLatinTrackingCodepoint(const uint32_t cp) {
  return (cp >= '0' && cp <= '9') ||
         (cp >= 'A' && cp <= 'Z') ||
         (cp >= 'a' && cp <= 'z');
}

bool shouldPreferExternalUiGlyph(const uint32_t cp, const EpdGlyph* builtinGlyph) {
  // UI labels look unbalanced when CJK is drawn by the external 20/30 px
  // fixed-cell font but ASCII letters/digits come from the smaller built-in
  // Ubuntu face. Prefer the active UI font for printable ASCII too; if that
  // font lacks the glyph, drawText()/getTextWidth() will fall back to the
  // built-in path as before.
  return isCjkCodepoint(cp) ||
         (cp >= 0x20 && cp <= 0x7E) ||
         builtinGlyph == nullptr;
}

bool isAdvanceSensitiveCodepoint(const uint32_t cp) {
  // Horizontal EPUB layout is especially sensitive around compact glyphs that
  // often sit next to CJK with no explicit space: numbers, ASCII punctuation,
  // ellipsis/dashes, and common Unicode punctuation.  Some converted fonts can
  // report an advance that is smaller than the drawn bitmap's right edge.  If
  // the next glyph starts from that too-small advance, the ink overlaps.
  if (cp >= '0' && cp <= '9') return true;
  if ((cp >= 0x21 && cp <= 0x2F) ||
      (cp >= 0x3A && cp <= 0x40) ||
      (cp >= 0x5B && cp <= 0x60) ||
      (cp >= 0x7B && cp <= 0x7E)) {
    return true;
  }
  if (cp >= 0x2000 && cp <= 0x206F) return true;  // General punctuation, including U+2026
  if (cp >= 0x3000 && cp <= 0x303F) return true;  // CJK punctuation
  if (cp >= 0xFF00 && cp <= 0xFFEF) return true;  // Full-width punctuation/forms
  return false;
}

int latinPairTracking(const uint32_t previous, const uint32_t current) {
  // The compact Ubuntu UI faces are intentionally tight.  Two extra pixels
  // between adjacent Latin letters/digits keeps English readable beside the
  // wider fixed-cell CJK fallback without changing CJK punctuation spacing.
  return isLatinTrackingCodepoint(previous) && isLatinTrackingCodepoint(current) ? 2 : 0;
}

constexpr int COMPACT_CJK_SCALE_NUM = 7;
constexpr int COMPACT_CJK_SCALE_DEN = 10;

bool usesCompactScaledCjkFallback(const int fontId) {
  (void)fontId;
  // Historical compact UI fallback path is disabled.  UI glyph size is kept
  // stable; reader CJK scaling is controlled separately by SETTINGS.fontSize.
  return false;
}

int scaleCompactMetric(const int value) {
  return (value * COMPACT_CJK_SCALE_NUM + COMPACT_CJK_SCALE_DEN / 2) / COMPACT_CJK_SCALE_DEN;
}

int scaleCompactMetricCeil(const int value) {
  return (value * COMPACT_CJK_SCALE_NUM + COMPACT_CJK_SCALE_DEN - 1) / COMPACT_CJK_SCALE_DEN;
}

bool isZeroAdvanceControlCodepoint(const uint32_t cp) {
  return cp == 0x00AD ||  // soft hyphen
         cp == 0x034F ||
         cp == 0x061C ||
         (cp >= 0x200B && cp <= 0x200F) ||
         (cp >= 0x202A && cp <= 0x202E) ||
         (cp >= 0x2060 && cp <= 0x206F) ||
         cp == 0xFEFF;
}

bool shouldProtectVisibleAdvance(const uint32_t cp) {
  if (cp == 0 || cp < 0x20 || (cp >= 0x7F && cp <= 0x9F)) return false;
  if (utf8IsCombiningMark(cp) || isZeroAdvanceControlCodepoint(cp)) return false;
  return true;
}

int visibleGlyphAdvancePixels(const EpdGlyph* glyph) {
  if (glyph == nullptr) return 0;
  int advance = std::max(1, static_cast<int>(glyph->width));
  if (glyph->left > 0) {
    advance = std::max(advance, static_cast<int>(glyph->width) + glyph->left);
  }
  return advance;
}

int glyphAdvancePixels(const EpdGlyph* glyph, const uint32_t cp) {
  if (glyph == nullptr) return 0;
  int advance = fp4::toPixel(glyph->advanceX);

  if (shouldProtectVisibleAdvance(cp)) {
    const int visibleAdvance = visibleGlyphAdvancePixels(glyph);
    if (advance <= 0 || isAdvanceSensitiveCodepoint(cp)) {
      // Protect the next glyph from being drawn on top of visible ink.  The
      // extra pixel is intentionally limited to digits/punctuation/symbols so
      // normal Latin words do not become overly loose.
      advance = std::max(advance, visibleAdvance + (isAdvanceSensitiveCodepoint(cp) ? 1 : 0));
    } else {
      advance = std::max(advance, visibleAdvance);
    }
  }

  return advance;
}

int32_t glyphAdvanceFP(const EpdGlyph* glyph, const uint32_t cp) {
  if (glyph == nullptr) return 0;

  const int32_t originalAdvanceFP = glyph->advanceX;
  int advance = originalAdvanceFP > 0 ? fp4::toPixel(originalAdvanceFP) : 0;
  if (shouldProtectVisibleAdvance(cp)) {
    const int visibleAdvance = visibleGlyphAdvancePixels(glyph);
    if (advance <= 0 || isAdvanceSensitiveCodepoint(cp)) {
      advance = std::max(advance, visibleAdvance + (isAdvanceSensitiveCodepoint(cp) ? 1 : 0));
    } else {
      advance = std::max(advance, visibleAdvance);
    }
  }

  // Preserve the original fractional advance unless a visible-ink guard had to
  // enlarge it.  This keeps the old kerning/differential rounding behavior for
  // normal glyphs while fixing glyphs whose metrics were too narrow.
  if (originalAdvanceFP > 0 && fp4::toPixel(originalAdvanceFP) >= advance) {
    return originalAdvanceFP;
  }
  return fp4::fromPixel(advance);
}

int scaleBuiltinCjkAxis(const int value, const int percent, const int logicalCell, const int sourceCell) {
  return (value * logicalCell * percent + sourceCell * 50) / (sourceCell * 100);
}

int scaleBuiltinCjkAxisCeil(const int value, const int percent, const int logicalCell, const int sourceCell) {
  const int denominator = sourceCell * 100;
  return (value * logicalCell * percent + denominator - 1) / denominator;
}

int fallbackMetricXPixels(const int fontId, const int value) {
  const int percent = fallbackScalePercentForFontId(fontId);
  return std::max(1, scaleBuiltinCjkAxis(value, percent, BUILTIN_CJK_LOGICAL_CELL_WIDTH, BUILTIN_CJK_SOURCE_CELL_WIDTH));
}

int fallbackMetricYPixels(const int fontId, const int value) {
  const int percent = fallbackScalePercentForFontId(fontId);
  return std::max(1, scaleBuiltinCjkAxis(value, percent, BUILTIN_CJK_LOGICAL_CELL_HEIGHT, BUILTIN_CJK_SOURCE_CELL_HEIGHT));
}

int fallbackMetricXPixelsCeil(const int fontId, const int value) {
  const int percent = fallbackScalePercentForFontId(fontId);
  return std::max(1, scaleBuiltinCjkAxisCeil(value, percent, BUILTIN_CJK_LOGICAL_CELL_WIDTH, BUILTIN_CJK_SOURCE_CELL_WIDTH));
}

int fallbackMetricYPixelsCeil(const int fontId, const int value) {
  const int percent = fallbackScalePercentForFontId(fontId);
  return std::max(1, scaleBuiltinCjkAxisCeil(value, percent, BUILTIN_CJK_LOGICAL_CELL_HEIGHT, BUILTIN_CJK_SOURCE_CELL_HEIGHT));
}

int fallbackAdvancePixels(const int fontId, const EpdGlyph* glyph, const uint32_t cp) {
  return fallbackMetricXPixels(fontId, glyphAdvancePixels(glyph, cp));
}

int fallbackMetricPixels(const int fontId, const int value) {
  // Vertical metrics (ascender, advanceY, text height) use the logical 30px
  // reader-cell height, not the raw 39px source cell.
  return fallbackMetricYPixels(fontId, value);
}

bool shouldRotateVerticalGlyph(
    const uint32_t codepoint
) {
  switch (codepoint) {
    case 0x300C:  // 「
    case 0x300D:  // 」
    case 0x300E:  // 『
    case 0x300F:  // 』

    case 0x2026:  // …
    case 0x2014:  // —
    case 0x2013:  // –

    case 0x0028:  // (
    case 0x0029:  // )
    case 0xFF08:  // （
    case 0xFF09:  // ）

    case 0x3008:  // 〈
    case 0x3009:  // 〉
    case 0x300A:  // 《
    case 0x300B:  // 》

    case 0x3010:  // 【
    case 0x3011:  // 】
    case 0x3014:  // 〔
    case 0x3015:  // 〕
      return true;

    default:
      return false;
  }
}

}  // namespace

bool GfxRenderer::shouldUseExternalUiFont(
    const int fontId
) {
  return fontId == UI_10_FONT_ID ||
         fontId == UI_12_FONT_ID;
}

bool GfxRenderer::isUiFont(const int fontId) {
  return fontId == UI_10_FONT_ID ||
         fontId == UI_12_FONT_ID ||
         fontId == SMALL_FONT_ID;
}



bool GfxRenderer::isReaderFont(const int fontId) {
  for (int i = 0; i < UI_FONT_COUNT; ++i) {
    if (fontId == UI_FONT_IDS[i]) {
      return false;
    }
  }

  return true;
}

const EpdFontFamily* GfxRenderer::getBuiltinFallbackForFontId(const int fontId) const {
  (void)fontId;
  return builtinFallbackFont_;
}

bool GfxRenderer::shouldUseBuiltinFallback(
    const EpdFontFamily& primaryFont,
    const EpdFontFamily* fallbackFont,
    const uint32_t codepoint,
    const EpdFontFamily::Style style
) const {
  if (fallbackFont == nullptr) {
    return false;
  }

  if (fallbackFont->getGlyphExact(codepoint, style) == nullptr) {
    return false;
  }

  // Prefer the dedicated fixed-cell CJK design for CJK/full-width codepoints,
  // even if the Latin font would otherwise return U+FFFD. For other scripts,
  // use it only when the primary family truly lacks the requested glyph.
  return isCjkCodepoint(codepoint) ||
         primaryFont.getGlyphExact(codepoint, style) == nullptr;
}

int GfxRenderer::getTextWidthBuiltinFallback(
    const int fontId,
    const EpdFontFamily& primaryFont,
    const char* text,
    const EpdFontFamily::Style style
) const {
  if (text == nullptr || *text == '\0') {
    return 0;
  }

  const EpdFontFamily* fallbackFont = getBuiltinFallbackForFontId(fontId);
  int widthPixels = 0;
  int32_t pendingAdvanceFP = 0;
  uint32_t previousPrimaryCp = 0;

  auto flushPrimary = [&]() {
    if (previousPrimaryCp != 0) {
      widthPixels += fp4::toPixel(pendingAdvanceFP);
    }
    previousPrimaryCp = 0;
    pendingAdvanceFP = 0;
  };

  const char* cursor = text;
  uint32_t cp = 0;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&cursor)))) {
    if (utf8IsCombiningMark(cp)) {
      continue;
    }

    if (shouldUseBuiltinFallback(primaryFont, fallbackFont, cp, style)) {
      flushPrimary();
      const EpdGlyph* fallbackGlyph = fallbackFont->getGlyphExact(cp, style);
      if (fallbackGlyph != nullptr) {
        widthPixels += fallbackAdvancePixels(fontId, fallbackGlyph, cp);
      }
      continue;
    }

    cp = primaryFont.applyLigatures(cp, cursor, style);
    if (previousPrimaryCp != 0) {
      widthPixels += fp4::toPixel(
          pendingAdvanceFP + primaryFont.getKerning(previousPrimaryCp, cp, style));
      widthPixels += latinPairTracking(previousPrimaryCp, cp);
    }

    const EpdGlyph* glyph = primaryFont.getGlyph(cp, style);
    pendingAdvanceFP = glyphAdvanceFP(glyph, cp);
    previousPrimaryCp = cp;
  }

  flushPrimary();
  return widthPixels;
}

const uint8_t* GfxRenderer::getGlyphBitmap(const EpdFontData* fontData, const EpdGlyph* glyph) const {
  if (fontData->groups != nullptr) {
    auto* fd = fontCacheManager_ ? fontCacheManager_->getDecompressor() : nullptr;
    if (!fd) {
      LOG_ERR("GFX", "Compressed font but no FontDecompressor set");
      return nullptr;
    }
    uint32_t glyphIndex = static_cast<uint32_t>(glyph - fontData->glyph);
    // For page-buffer hits the pointer is stable for the page lifetime.
    // For hot-group hits it is valid only until the next getBitmap() call — callers
    // must consume it (draw the glyph) before requesting another bitmap.
    return fd->getBitmap(fontData, glyph, glyphIndex);
  }
  return &fontData->bitmap[glyph->dataOffset];
}

void GfxRenderer::begin() {
  frameBuffer = display.getFrameBuffer();
  if (!frameBuffer) {
    LOG_ERR("GFX", "!! No framebuffer");
    assert(false);
  }
}

void GfxRenderer::insertFont(const int fontId, EpdFontFamily font) { fontMap.insert({fontId, font}); }

// Translate logical (x,y) coordinates to physical panel coordinates based on current orientation
// This should always be inlined for better performance
static inline void rotateCoordinates(const GfxRenderer::Orientation orientation, const int x, const int y, int* phyX,
                                     int* phyY) {
  switch (orientation) {
    case GfxRenderer::Portrait: {
      // Logical portrait (540x960) → panel (960x540)
      // Rotation: 90 degrees clockwise
      *phyX = y;
      *phyY = HalDisplay::DISPLAY_HEIGHT - 1 - x;
      break;
    }
    case GfxRenderer::LandscapeClockwise: {
      // Logical landscape (960x540) rotated 180 degrees (swap top/bottom and left/right)
      *phyX = HalDisplay::DISPLAY_WIDTH - 1 - x;
      *phyY = HalDisplay::DISPLAY_HEIGHT - 1 - y;
      break;
    }
    case GfxRenderer::PortraitInverted: {
      // Logical portrait (540x960) → panel (960x540)
      // Rotation: 90 degrees counter-clockwise
      *phyX = HalDisplay::DISPLAY_WIDTH - 1 - y;
      *phyY = x;
      break;
    }
    case GfxRenderer::LandscapeCounterClockwise: {
      // Logical landscape (960x540) aligned with panel orientation
      *phyX = x;
      *phyY = y;
      break;
    }
  }
}

enum class TextRotation { None, Rotated90CW };

// Shared glyph rendering logic for normal and rotated text.
// Coordinate mapping and cursor advance direction are selected at compile time via the template parameter.
template <TextRotation rotation>
static void renderCharImpl(const GfxRenderer& renderer, GfxRenderer::RenderMode renderMode,
                           const EpdFontFamily& fontFamily, const uint32_t cp, int cursorX, int cursorY,
                           const bool pixelState, const EpdFontFamily::Style style) {
  const EpdGlyph* glyph = fontFamily.getGlyph(cp, style);
  if (!glyph) {
    LOG_ERR("GFX", "No glyph for codepoint %d", cp);
    return;
  }

  const EpdFontData* fontData = fontFamily.getData(style);
  const bool is2Bit = fontData->is2Bit;
  const uint8_t width = glyph->width;
  const uint8_t height = glyph->height;
  const int left = glyph->left;
  const int top = glyph->top;

  const uint8_t* bitmap = renderer.getGlyphBitmap(fontData, glyph);

  if (bitmap != nullptr) {
    // For Normal:  outer loop advances screenY, inner loop advances screenX
    // For Rotated: outer loop advances screenX, inner loop advances screenY (in reverse)
    int outerBase, innerBase;
    if constexpr (rotation == TextRotation::Rotated90CW) {
      outerBase = cursorX + fontData->ascender - top;  // screenX = outerBase + glyphY
      innerBase = cursorY - left;                      // screenY = innerBase - glyphX
    } else {
      outerBase = cursorY - top;   // screenY = outerBase + glyphY
      innerBase = cursorX + left;  // screenX = innerBase + glyphX
    }

    if (is2Bit) {
      int pixelPosition = 0;
      for (int glyphY = 0; glyphY < height; glyphY++) {
        const int outerCoord = outerBase + glyphY;
        for (int glyphX = 0; glyphX < width; glyphX++, pixelPosition++) {
          int screenX, screenY;
          if constexpr (rotation == TextRotation::Rotated90CW) {
            screenX = outerCoord;
            screenY = innerBase - glyphX;
          } else {
            screenX = innerBase + glyphX;
            screenY = outerCoord;
          }

          const uint8_t byte = bitmap[pixelPosition >> 2];
          const uint8_t bit_index = (3 - (pixelPosition & 3)) * 2;
          // the direct bit from the font is 0 -> white, 1 -> light gray, 2 -> dark gray, 3 -> black
          // we swap this to better match the way images and screen think about colors:
          // 0 -> black, 1 -> dark grey, 2 -> light grey, 3 -> white
          const uint8_t bmpVal = 3 - ((byte >> bit_index) & 0x3);

          if (renderMode == GfxRenderer::GRAYSCALE_DIRECT && bmpVal < 3) {
            // Write EPD gray value directly: bmpVal 0→3(black), 1→2(dk gray), 2→1(lt gray)
            renderer.drawPixelGray(screenX, screenY, 3 - bmpVal);
          } else if (renderMode == GfxRenderer::BW && bmpVal < 3) {
            // Black (also paints over the grays in BW mode)
            renderer.drawPixel(screenX, screenY, pixelState);
          } else if (renderMode == GfxRenderer::GRAYSCALE_MSB && (bmpVal == 1 || bmpVal == 2)) {
            renderer.drawPixel(screenX, screenY, false);
          } else if (renderMode == GfxRenderer::GRAYSCALE_LSB && bmpVal == 1) {
            renderer.drawPixel(screenX, screenY, false);
          }
        }
      }
    } else {
      int pixelPosition = 0;
      for (int glyphY = 0; glyphY < height; glyphY++) {
        const int outerCoord = outerBase + glyphY;
        for (int glyphX = 0; glyphX < width; glyphX++, pixelPosition++) {
          int screenX, screenY;
          if constexpr (rotation == TextRotation::Rotated90CW) {
            screenX = outerCoord;
            screenY = innerBase - glyphX;
          } else {
            screenX = innerBase + glyphX;
            screenY = outerCoord;
          }

          const uint8_t byte = bitmap[pixelPosition >> 3];
          const uint8_t bit_index = 7 - (pixelPosition & 7);

          if ((byte >> bit_index) & 1) {
            renderer.drawPixel(screenX, screenY, pixelState);
          }
        }
      }
    }
  }
}

static void renderCharImplScaled(const GfxRenderer& renderer, GfxRenderer::RenderMode renderMode,
                                 const EpdFontFamily& fontFamily, const uint32_t cp, const int cursorX,
                                 const int cursorY, const int scale, const bool pixelState,
                                 const EpdFontFamily::Style style) {
  const EpdGlyph* glyph = fontFamily.getGlyph(cp, style);
  if (!glyph) {
    LOG_ERR("GFX", "No glyph for codepoint %d", cp);
    return;
  }

  const EpdFontData* fontData = fontFamily.getData(style);
  if (fontData == nullptr) {
    return;
  }

  const uint8_t* bitmap = renderer.getGlyphBitmap(fontData, glyph);
  if (bitmap == nullptr || glyph->width == 0 || glyph->height == 0) {
    return;
  }

  const bool is2Bit = fontData->is2Bit;
  int pixelPosition = 0;
  for (int glyphY = 0; glyphY < glyph->height; ++glyphY) {
    const int screenY = cursorY - glyph->top * scale + glyphY * scale;
    for (int glyphX = 0; glyphX < glyph->width; ++glyphX, ++pixelPosition) {
      const int screenX = cursorX + glyph->left * scale + glyphX * scale;

      if (is2Bit) {
        const uint8_t byte = bitmap[pixelPosition >> 2];
        const uint8_t bitIndex = (3 - (pixelPosition & 3)) * 2;
        const uint8_t bmpVal = 3 - ((byte >> bitIndex) & 0x3);

        if (renderMode == GfxRenderer::GRAYSCALE_DIRECT && bmpVal < 3) {
          for (int dy = 0; dy < scale; ++dy) {
            for (int dx = 0; dx < scale; ++dx) {
              renderer.drawPixelGray(screenX + dx, screenY + dy, 3 - bmpVal);
            }
          }
        } else if (renderMode == GfxRenderer::BW && bmpVal < 3) {
          renderer.fillRect(screenX, screenY, scale, scale, pixelState);
        } else if (renderMode == GfxRenderer::GRAYSCALE_MSB && (bmpVal == 1 || bmpVal == 2)) {
          renderer.fillRect(screenX, screenY, scale, scale, false);
        } else if (renderMode == GfxRenderer::GRAYSCALE_LSB && bmpVal == 1) {
          renderer.fillRect(screenX, screenY, scale, scale, false);
        }
      } else {
        const uint8_t byte = bitmap[pixelPosition >> 3];
        const uint8_t bitIndex = 7 - (pixelPosition & 7);
        if ((byte >> bitIndex) & 1U) {
          renderer.fillRect(screenX, screenY, scale, scale, pixelState);
        }
      }
    }
  }
}


static void renderScaledGlyphBitmap(const GfxRenderer& renderer, GfxRenderer::RenderMode renderMode,
                                    const EpdFontData* fontData, const EpdGlyph* glyph,
                                    const int drawX, const int drawY,
                                    const int destWidth, const int destHeight,
                                    const bool pixelState) {
  if (fontData == nullptr || glyph == nullptr || destWidth <= 0 || destHeight <= 0) return;
  if (glyph->width == 0 || glyph->height == 0) return;

  const uint8_t* bitmap = renderer.getGlyphBitmap(fontData, glyph);
  if (bitmap == nullptr) return;

  // The embedded CJK source is now a larger 31x39 bitmap that is resampled to
  // the logical reader target size.  For 1-bit fonts, use a small supersampled
  // coverage estimate instead of nearest-neighbour so downscaling and mild
  // upscaling keep smoother stroke edges on grayscale-capable refreshes.
  constexpr int SAMPLE_GRID = 4;
  constexpr int SAMPLE_COUNT = SAMPLE_GRID * SAMPLE_GRID;

  for (int dy = 0; dy < destHeight; ++dy) {
    for (int dx = 0; dx < destWidth; ++dx) {
      if (fontData->is2Bit) {
        const int sy = std::min<int>(glyph->height - 1, (dy * glyph->height) / destHeight);
        const int sx = std::min<int>(glyph->width - 1, (dx * glyph->width) / destWidth);
        const int pixelPosition = sy * glyph->width + sx;
        const uint8_t byte = bitmap[pixelPosition >> 2];
        const uint8_t bitIndex = (3 - (pixelPosition & 3)) * 2;
        const uint8_t bmpVal = 3 - ((byte >> bitIndex) & 0x3);
        if (renderMode == GfxRenderer::GRAYSCALE_DIRECT && bmpVal < 3) {
          renderer.drawPixelGray(drawX + dx, drawY + dy, 3 - bmpVal);
        } else if (renderMode == GfxRenderer::BW && bmpVal < 3) {
          renderer.drawPixel(drawX + dx, drawY + dy, pixelState);
        } else if (renderMode == GfxRenderer::GRAYSCALE_MSB && (bmpVal == 1 || bmpVal == 2)) {
          renderer.drawPixel(drawX + dx, drawY + dy, false);
        } else if (renderMode == GfxRenderer::GRAYSCALE_LSB && bmpVal == 1) {
          renderer.drawPixel(drawX + dx, drawY + dy, false);
        }
        continue;
      }

      int covered = 0;
      for (int sySample = 0; sySample < SAMPLE_GRID; ++sySample) {
        const int sy = std::min<int>(
            glyph->height - 1,
            ((dy * SAMPLE_GRID + sySample) * glyph->height) / (destHeight * SAMPLE_GRID));
        for (int sxSample = 0; sxSample < SAMPLE_GRID; ++sxSample) {
          const int sx = std::min<int>(
              glyph->width - 1,
              ((dx * SAMPLE_GRID + sxSample) * glyph->width) / (destWidth * SAMPLE_GRID));
          const int pixelPosition = sy * glyph->width + sx;
          const uint8_t byte = bitmap[pixelPosition >> 3];
          const uint8_t bitIndex = 7 - (pixelPosition & 7);
          if ((byte >> bitIndex) & 1U) ++covered;
        }
      }

      if (covered == 0) continue;

      if (renderMode == GfxRenderer::GRAYSCALE_DIRECT) {
        const uint8_t gray = static_cast<uint8_t>(std::min(3, std::max(1, (covered * 3 + SAMPLE_COUNT / 2) / SAMPLE_COUNT)));
        renderer.drawPixelGray(drawX + dx, drawY + dy, gray);
      } else if (renderMode == GfxRenderer::BW) {
        if (covered * 2 >= SAMPLE_COUNT) renderer.drawPixel(drawX + dx, drawY + dy, pixelState);
      } else if (renderMode == GfxRenderer::GRAYSCALE_MSB) {
        if (covered * 2 >= SAMPLE_COUNT) renderer.drawPixel(drawX + dx, drawY + dy, false);
      } else if (renderMode == GfxRenderer::GRAYSCALE_LSB) {
        if (covered * 4 >= SAMPLE_COUNT * 3) renderer.drawPixel(drawX + dx, drawY + dy, false);
      }
    }
  }
}

bool GfxRenderer::renderPrimaryGlyphCentered(
    const int fontId,
    const int cellX,
    const int cellY,
    const int cellWidth,
    const int cellHeight,
    const uint32_t codepoint,
    const bool pixelState,
    const EpdFontFamily::Style style
) const {
  const auto fontIt = fontMap.find(fontId);
  if (fontIt == fontMap.end()) return false;

  const EpdFontFamily& primaryFont = fontIt->second;
  const EpdGlyph* glyph = primaryFont.getGlyphExact(codepoint, style);
  if (glyph == nullptr) return false;
  if (glyph->width == 0 || glyph->height == 0) return true;

  const int drawX = cellX + (cellWidth - static_cast<int>(glyph->width)) / 2;
  const int drawY = cellY + (cellHeight - static_cast<int>(glyph->height)) / 2;
  const int cursorX = drawX - glyph->left;
  const int baselineY = drawY + glyph->top;

  renderCharImpl<TextRotation::None>(
      *this, renderMode, primaryFont, codepoint,
      cursorX, baselineY, pixelState, style);
  return true;
}

bool GfxRenderer::renderBuiltinFallbackGlyphCentered(
    const int fontId,
    const int cellX,
    const int cellY,
    const int cellWidth,
    const int cellHeight,
    const uint32_t codepoint,
    const bool pixelState,
    const EpdFontFamily::Style style
) const {
  if (builtinFallbackFont_ == nullptr) {
    return false;
  }

  const EpdGlyph* glyph =
      builtinFallbackFont_->getGlyphExact(codepoint, style);
  if (glyph == nullptr) {
    return false;
  }

  // A retained blank glyph (notably U+3000 IDEOGRAPHIC SPACE) is valid.
  if (glyph->width == 0 || glyph->height == 0) {
    return true;
  }

  const EpdFontData* data = builtinFallbackFont_->getData(style);
  if (data == nullptr) return false;
  const int destWidth = fallbackMetricXPixelsCeil(fontId, glyph->width);
  const int destHeight = fallbackMetricYPixelsCeil(fontId, glyph->height);
  const int drawX = cellX + (cellWidth - destWidth) / 2;
  const int drawY = cellY + (cellHeight - destHeight) / 2;
  renderScaledGlyphBitmap(*this, renderMode, data, glyph, drawX, drawY, destWidth, destHeight, pixelState);
  return true;
}

bool GfxRenderer::renderBuiltinFallbackGlyphRotated90CW(
    const int fontId,
    const int cellX,
    const int cellY,
    const int cellSize,
    const uint32_t codepoint,
    const bool pixelState,
    const EpdFontFamily::Style style
) const {
  if (builtinFallbackFont_ == nullptr) {
    return false;
  }

  const EpdGlyph* glyph =
      builtinFallbackFont_->getGlyphExact(codepoint, style);
  if (glyph == nullptr) {
    return false;
  }

  if (glyph->width == 0 || glyph->height == 0) {
    return true;
  }

  const EpdFontData* data = builtinFallbackFont_->getData(style);
  if (data == nullptr) {
    return false;
  }

  // Fallback rotation path for glyphs without a vertical presentation form.
  // Render a scaled version of the original glyph rotated clockwise into the
  // same vertical cell.
  const int destWidth = fallbackMetricYPixelsCeil(fontId, glyph->height);
  const int destHeight = fallbackMetricXPixelsCeil(fontId, glyph->width);
  const int drawX = cellX + (cellSize - destWidth) / 2;
  const int drawY = cellY + (cellSize - destHeight) / 2;
  const uint8_t* bitmap = getGlyphBitmap(data, glyph);
  if (bitmap == nullptr) return false;
  for (int dy = 0; dy < destHeight; ++dy) {
    const int sx = std::min<int>(glyph->width - 1, ((destHeight - 1 - dy) * glyph->width) / destHeight);
    for (int dx = 0; dx < destWidth; ++dx) {
      const int sy = std::min<int>(glyph->height - 1, (dx * glyph->height) / destWidth);
      if (data->is2Bit) {
        const int pixelPosition = sy * glyph->width + sx;
        const uint8_t byte = bitmap[pixelPosition >> 2];
        const uint8_t bitIndex = (3 - (pixelPosition & 3)) * 2;
        const uint8_t bmpVal = 3 - ((byte >> bitIndex) & 0x3);
        if (renderMode == GRAYSCALE_DIRECT && bmpVal < 3) {
          drawPixelGray(drawX + dx, drawY + dy, 3 - bmpVal);
        } else if (renderMode == BW && bmpVal < 3) {
          drawPixel(drawX + dx, drawY + dy, pixelState);
        } else if (renderMode == GRAYSCALE_MSB && (bmpVal == 1 || bmpVal == 2)) {
          drawPixel(drawX + dx, drawY + dy, false);
        } else if (renderMode == GRAYSCALE_LSB && bmpVal == 1) {
          drawPixel(drawX + dx, drawY + dy, false);
        }
      } else {
        const int pixelPosition = sy * glyph->width + sx;
        const uint8_t byte = bitmap[pixelPosition >> 3];
        if ((byte >> (7 - (pixelPosition & 7))) & 1U) {
          drawPixel(drawX + dx, drawY + dy, pixelState);
        }
      }
    }
  }
  return true;
}


bool GfxRenderer::renderBuiltinFallbackGlyphScaled(
    const EpdFontFamily& fallbackFont,
    const uint32_t codepoint,
    const int cursorX,
    const int lineTopY,
    const bool pixelState,
    const EpdFontFamily::Style style
) const {
  const EpdGlyph* glyph = fallbackFont.getGlyphExact(codepoint, style);
  const EpdFontData* data = fallbackFont.getData(style);
  if (glyph == nullptr || data == nullptr) return false;
  if (glyph->width == 0 || glyph->height == 0) return true;

  const uint8_t* bitmap = getGlyphBitmap(data, glyph);
  if (bitmap == nullptr) return false;

  const int destWidth = std::max(1, scaleCompactMetricCeil(glyph->width));
  const int destHeight = std::max(1, scaleCompactMetricCeil(glyph->height));
  const int drawX = cursorX + scaleCompactMetric(glyph->left);
  const int drawY = lineTopY + scaleCompactMetric(data->ascender - glyph->top);

  // Area sampling (logical OR) keeps thin CJK strokes visible when the compact
  // UI path uses a scale below 100%.
  for (int dy = 0; dy < destHeight; ++dy) {
    const int sy0 = dy * COMPACT_CJK_SCALE_DEN / COMPACT_CJK_SCALE_NUM;
    const int sy1 = std::min<int>(glyph->height,
        ((dy + 1) * COMPACT_CJK_SCALE_DEN + COMPACT_CJK_SCALE_NUM - 1) / COMPACT_CJK_SCALE_NUM);
    for (int dx = 0; dx < destWidth; ++dx) {
      const int sx0 = dx * COMPACT_CJK_SCALE_DEN / COMPACT_CJK_SCALE_NUM;
      const int sx1 = std::min<int>(glyph->width,
          ((dx + 1) * COMPACT_CJK_SCALE_DEN + COMPACT_CJK_SCALE_NUM - 1) / COMPACT_CJK_SCALE_NUM);
      bool set = false;
      for (int sy = sy0; sy < sy1 && !set; ++sy) {
        for (int sx = sx0; sx < sx1; ++sx) {
          const int bitPos = sy * glyph->width + sx;
          if ((bitmap[bitPos >> 3] >> (7 - (bitPos & 7))) & 1U) {
            set = true;
            break;
          }
        }
      }
      if (set) drawPixel(drawX + dx, drawY + dy, pixelState);
    }
  }
  return true;
}

bool GfxRenderer::renderBuiltinFallbackGlyphScaledPercent(
    const EpdFontFamily& fallbackFont,
    const uint32_t codepoint,
    const int cursorX,
    const int lineTopY,
    const int percent,
    const bool pixelState,
    const EpdFontFamily::Style style
) const {
  const EpdGlyph* glyph = fallbackFont.getGlyphExact(codepoint, style);
  const EpdFontData* data = fallbackFont.getData(style);
  if (glyph == nullptr || data == nullptr) return false;
  if (glyph->width == 0 || glyph->height == 0) return true;

  const int destWidth = std::max(1, scaleBuiltinCjkAxisCeil(glyph->width, percent, BUILTIN_CJK_LOGICAL_CELL_WIDTH, BUILTIN_CJK_SOURCE_CELL_WIDTH));
  const int destHeight = std::max(1, scaleBuiltinCjkAxisCeil(glyph->height, percent, BUILTIN_CJK_LOGICAL_CELL_HEIGHT, BUILTIN_CJK_SOURCE_CELL_HEIGHT));
  const int drawX = cursorX + scaleBuiltinCjkAxis(glyph->left, percent, BUILTIN_CJK_LOGICAL_CELL_WIDTH, BUILTIN_CJK_SOURCE_CELL_WIDTH);
  const int drawY = lineTopY + scaleBuiltinCjkAxis(data->ascender - glyph->top, percent, BUILTIN_CJK_LOGICAL_CELL_HEIGHT, BUILTIN_CJK_SOURCE_CELL_HEIGHT);
  renderScaledGlyphBitmap(*this, renderMode, data, glyph, drawX, drawY, destWidth, destHeight, pixelState);
  return true;
}


bool GfxRenderer::renderBuiltinFallbackGlyphScaledRotated90CW(
    const EpdFontFamily& fallbackFont,
    const uint32_t codepoint,
    const int cursorX,
    const int cursorY,
    const bool pixelState,
    const EpdFontFamily::Style style
) const {
  const EpdGlyph* glyph = fallbackFont.getGlyphExact(codepoint, style);
  const EpdFontData* data = fallbackFont.getData(style);
  if (glyph == nullptr || data == nullptr) return false;
  if (glyph->width == 0 || glyph->height == 0) return true;

  const uint8_t* bitmap = getGlyphBitmap(data, glyph);
  if (bitmap == nullptr) return false;

  const int destWidth = std::max(1, scaleCompactMetricCeil(glyph->width));
  const int destHeight = std::max(1, scaleCompactMetricCeil(glyph->height));
  const int drawX = cursorX + scaleCompactMetric(data->ascender - glyph->top);
  const int drawY = cursorY - scaleCompactMetric(glyph->left);

  for (int dy = 0; dy < destHeight; ++dy) {
    const int sy0 = dy * COMPACT_CJK_SCALE_DEN / COMPACT_CJK_SCALE_NUM;
    const int sy1 = std::min<int>(glyph->height,
        ((dy + 1) * COMPACT_CJK_SCALE_DEN + COMPACT_CJK_SCALE_NUM - 1) / COMPACT_CJK_SCALE_NUM);
    for (int dx = 0; dx < destWidth; ++dx) {
      const int sx0 = dx * COMPACT_CJK_SCALE_DEN / COMPACT_CJK_SCALE_NUM;
      const int sx1 = std::min<int>(glyph->width,
          ((dx + 1) * COMPACT_CJK_SCALE_DEN + COMPACT_CJK_SCALE_NUM - 1) / COMPACT_CJK_SCALE_NUM);
      bool set = false;
      for (int sy = sy0; sy < sy1 && !set; ++sy) {
        for (int sx = sx0; sx < sx1; ++sx) {
          const int bitPos = sy * glyph->width + sx;
          if ((bitmap[bitPos >> 3] >> (7 - (bitPos & 7))) & 1U) {
            set = true;
            break;
          }
        }
      }
      if (set) drawPixel(drawX + dy, drawY - dx, pixelState);
    }
  }
  return true;
}

void GfxRenderer::renderExternalGlyph(
    const uint8_t* bitmap,
    ExternalFont* font,
    int* x,
    const int baselineY,
    const bool pixelState,
    const ExternalGlyphMetrics& metrics,
    const int advanceOverride,
    const int cellClipWidth
) const {
  if (!bitmap || !font || !x) {
    return;
  }

  const uint8_t width =
      metrics.width > 0 ? metrics.width : font->getCharWidth();

  const uint8_t height =
      metrics.height > 0 ? metrics.height : font->getCharHeight();

  const uint8_t bytesPerRow = (width + 7) / 8;

  const ExternalGlyphLayout layout =
      computeExternalGlyphLayout(
          *x,
          baselineY,
          *font,
          metrics,
          advanceOverride
      );

  const int cursorX = *x;
  const int screenWidth = getScreenWidth();
  const int screenHeight = getScreenHeight();

  int minGlyphX = std::max(0, -layout.drawX);
  int maxGlyphX =
      std::min<int>(width, screenWidth - layout.drawX);

  const int minGlyphY = std::max(0, -layout.drawY);
  const int maxGlyphY =
      std::min<int>(height, screenHeight - layout.drawY);

  if (cellClipWidth > 0) {
    minGlyphX =
        std::max(minGlyphX, cursorX - layout.drawX);

    maxGlyphX =
        std::min(
            maxGlyphX,
            cursorX + cellClipWidth - layout.drawX
        );
  }

  if (minGlyphX >= maxGlyphX || minGlyphY >= maxGlyphY) {
    *x += layout.advanceX;
    return;
  }

  for (int glyphY = minGlyphY;
       glyphY < maxGlyphY;
       ++glyphY) {

    const int screenY = layout.drawY + glyphY;

    for (int glyphX = minGlyphX;
         glyphX < maxGlyphX;
         ++glyphX) {

      const int byteIndex =
          glyphY * bytesPerRow + glyphX / 8;

      const int bitIndex = 7 - glyphX % 8;

      if ((bitmap[byteIndex] >> bitIndex) & 1) {
        drawPixel(
            layout.drawX + glyphX,
            screenY,
            pixelState
        );
      }
    }
  }

  *x += layout.advanceX;
}

bool GfxRenderer::renderExternalReaderGlyphCentered(
    const int fontId,
    const int cellX,
    const int cellY,
    const int cellWidth,
    const int cellHeight,
    const uint32_t codepoint,
    const bool pixelState
) const {
  if (!isReaderFont(fontId)) {
    return false;
  }

  FontManager& fontManager =
      FontManager::getInstance();

  if (!fontManager.isExternalFontEnabled()) {
    return false;
  }

  ExternalFont* font =
      fontManager.getActiveFont();

  if (font == nullptr ||
      !font->isLoaded()) {
    return false;
  }

  const uint8_t* bitmap =
      font->getGlyph(codepoint);

  if (bitmap == nullptr) {
    return false;
  }

  ExternalGlyphMetrics metrics =
      getDefaultMetrics(*font, codepoint);

  const int sourceWidth =
      metrics.width > 0
          ? metrics.width
          : font->getCharWidth();

  const int sourceHeight =
      metrics.height > 0
          ? metrics.height
          : font->getCharHeight();

  if (sourceWidth <= 0 ||
      sourceHeight <= 0) {
    return false;
  }

  const int bytesPerRow =
      (sourceWidth + 7) / 8;

  /*
   * 找出 glyph 實際黑色像素範圍。
   *
   * 不直接使用整個 bitmap cell，
   * 因為逗號、句號、英文瘦字的黑點
   * 可能只集中在 cell 的某個角落。
   */
  int minX = sourceWidth;
  int minY = sourceHeight;
  int maxX = -1;
  int maxY = -1;

  for (int sourceY = 0;
       sourceY < sourceHeight;
       ++sourceY) {
    for (int sourceX = 0;
         sourceX < sourceWidth;
         ++sourceX) {
      const int byteIndex =
          sourceY * bytesPerRow +
          sourceX / 8;

      const uint8_t mask =
          static_cast<uint8_t>(
              0x80U >> (sourceX & 7)
          );

      if ((bitmap[byteIndex] & mask) == 0) {
        continue;
      }

      if (sourceX < minX) {
        minX = sourceX;
      }

      if (sourceX > maxX) {
        maxX = sourceX;
      }

      if (sourceY < minY) {
        minY = sourceY;
      }

      if (sourceY > maxY) {
        maxY = sourceY;
      }
    }
  }

  /*
   * glyph 存在但沒有任何黑點。
   * 例如空白字元，視為已處理。
   */
  if (maxX < minX ||
      maxY < minY) {
    return true;
  }

  const int visibleWidth =
      maxX - minX + 1;

  const int visibleHeight =
      maxY - minY + 1;

  /*
   * 將實際可見筆畫置中於直排 cell。
   */
  const int destinationX =
      cellX +
      (cellWidth - visibleWidth) / 2;

  const int destinationY =
      cellY +
      (cellHeight - visibleHeight) / 2;

  for (int sourceY = minY;
       sourceY <= maxY;
       ++sourceY) {
    for (int sourceX = minX;
         sourceX <= maxX;
         ++sourceX) {
      const int byteIndex =
          sourceY * bytesPerRow +
          sourceX / 8;

      const uint8_t mask =
          static_cast<uint8_t>(
              0x80U >> (sourceX & 7)
          );

      if ((bitmap[byteIndex] & mask) == 0) {
        continue;
      }

      drawPixel(
          destinationX +
              sourceX -
              minX,
          destinationY +
              sourceY -
              minY,
          pixelState
      );
    }
  }

  return true;
}

bool GfxRenderer::renderExternalReaderGlyphRotated90CW(
    const int fontId,
    const int cellX,
    const int cellY,
    const int cellSize,
    const uint32_t codepoint,
    const bool pixelState
) const {
  if (!isReaderFont(fontId)) {
    return false;
  }

  FontManager& fontManager =
      FontManager::getInstance();

  if (!fontManager.isExternalFontEnabled()) {
    return false;
  }

  ExternalFont* font =
      fontManager.getActiveFont();

  if (font == nullptr ||
      !font->isLoaded()) {
    return false;
  }

  const uint8_t* bitmap =
      font->getGlyph(codepoint);

  if (bitmap == nullptr) {
    return false;
  }

  const int sourceWidth =
      font->getCharWidth();

  const int sourceHeight =
      font->getCharHeight();

  if (sourceWidth <= 0 ||
      sourceHeight <= 0) {
    return false;
  }

  const int bytesPerRow =
      (sourceWidth + 7) / 8;

  // 旋轉後寬、高互換。
  const int rotatedWidth =
      sourceHeight;

  const int rotatedHeight =
      sourceWidth;

  // 在直排 cell 中置中。
  const int offsetX =
      (cellSize - rotatedWidth) / 2;

  const int offsetY =
      (cellSize - rotatedHeight) / 2;

  for (int sourceY = 0;
       sourceY < sourceHeight;
       ++sourceY) {
    for (int sourceX = 0;
         sourceX < sourceWidth;
         ++sourceX) {
      const int byteIndex =
          sourceY * bytesPerRow +
          sourceX / 8;

      const uint8_t mask =
          static_cast<uint8_t>(
              0x80U >> (sourceX & 7)
          );

      if ((bitmap[byteIndex] & mask) == 0) {
        continue;
      }

      // Clockwise rotation:
      //
      // source(x, y)
      // → destination(height - 1 - y, x)
      const int destinationX =
          sourceHeight - 1 - sourceY;

      const int destinationY =
          sourceX;

      drawPixel(
          cellX + offsetX + destinationX,
          cellY + offsetY + destinationY,
          pixelState
      );
    }
  }

  return true;
}

bool GfxRenderer::renderExternalReaderGlyph(
    const uint32_t cp,
    int* x,
    const int baselineY,
    const bool pixelState
) const {
  FontManager& fontManager = FontManager::getInstance();
  if (!fontManager.isExternalFontEnabled()) return false;

  ExternalFont* externalFont = fontManager.getActiveFont();
  if (externalFont == nullptr ||
      (!externalFont->handlesAllCodepoints() && !isCjkCodepoint(cp))) {
    return false;
  }

  const uint8_t* bitmap = externalFont->getGlyph(cp);
  if (bitmap == nullptr) return false;

  ExternalGlyphMetrics metrics = getDefaultMetrics(*externalFont, cp);
  const bool forceCell = isCjkCodepoint(cp);
  if (forceCell && shouldUseCjkSymbolCellMetrics(cp)) {
    normalizeCjkSymbolMetricsForRendering(metrics, externalFont->getCharWidth(),
                                          externalFont->isRichMetricsFormat());
  }

  const int advance = getExternalGlyphAdvanceForRendering(
      metrics, externalFont->getCharWidth(), 0, forceCell,
      shouldUseGlyphBoundsForAdvance(cp));

  renderExternalGlyph(bitmap, externalFont, x, baselineY, pixelState, metrics,
                      advance, forceCell ? externalFont->getCharWidth() : -1);
  return true;
}

bool GfxRenderer::renderExternalUiGlyph(
    const uint32_t cp,
    int* x,
    const int baselineY,
    const bool pixelState
) const {
  if (x == nullptr) {
    return false;
  }

  FontManager& fontManager =
      FontManager::getInstance();

  if (!fontManager.isUiFontEnabled()) {
    return false;
  }

  ExternalFont* uiFont =
      fontManager.getActiveUiFont();

  if (uiFont == nullptr ||
      !uiFont->isLoaded()) {
    return false;
  }

  const uint8_t* bitmap =
      uiFont->getGlyph(cp);

  if (bitmap == nullptr) {
    return false;
  }

  ExternalGlyphMetrics metrics =
      getDefaultMetrics(*uiFont, cp);

  int advance = 0;

  if (shouldUseCjkSymbolCellMetrics(cp)) {
    normalizeCjkSymbolMetricsForRendering(
        metrics,
        uiFont->getCharWidth(),
        uiFont->isRichMetricsFormat()
    );

    advance =
        getExternalGlyphAdvanceForRendering(
            metrics,
            uiFont->getCharWidth(),
            0,
            true,
            false
        );
  } else {
    advance =
        adjustNonRichAdvance(
            metrics,
            *uiFont
        );
  }

  if (advance <= 0) {
    advance = uiFont->getCharWidth();
  }

  renderExternalGlyph(
      bitmap,
      uiFont,
      x,
      baselineY,
      pixelState,
      metrics,
      advance,
      -1
  );

  return true;
}

// IMPORTANT: This function is in critical rendering path and is called for every pixel. Please keep it as simple and
// efficient as possible.
void GfxRenderer::drawPixel(const int x, const int y, const bool state) const {
  int phyX = 0;
  int phyY = 0;

  // Note: this call should be inlined for better performance
  rotateCoordinates(orientation, x, y, &phyX, &phyY);

  // Bounds checking against physical panel dimensions
  if (phyX < 0 || phyX >= HalDisplay::DISPLAY_WIDTH || phyY < 0 || phyY >= HalDisplay::DISPLAY_HEIGHT) {
    LOG_ERR("GFX", "!! Outside range (%d, %d) -> (%d, %d)", x, y, phyX, phyY);
    return;
  }

  // 8bpp: one byte per pixel, 0=white, 3=black
  const uint32_t index = phyY * HalDisplay::DISPLAY_WIDTH + phyX;
  const bool finalState = invertDrawing ? !state : state;
  frameBuffer[index] = finalState ? 3 : 0;
}

void GfxRenderer::drawPixelGray(const int x, const int y, const uint8_t epdValue) const {
  int phyX = 0;
  int phyY = 0;
  rotateCoordinates(orientation, x, y, &phyX, &phyY);
  if (phyX < 0 || phyX >= HalDisplay::DISPLAY_WIDTH || phyY < 0 || phyY >= HalDisplay::DISPLAY_HEIGHT) return;
  const uint32_t index = phyY * HalDisplay::DISPLAY_WIDTH + phyX;
  frameBuffer[index] = invertDrawing ? (3 - std::min<uint8_t>(epdValue, 3)) : epdValue;
}
int GfxRenderer::getTextWidthExternalReader(
    const int fontId,
    const char* text,
    const EpdFontFamily::Style style
) const {
  if (text == nullptr || *text == '\0') return 0;

  const auto fontIt = fontMap.find(fontId);
  if (fontIt == fontMap.end()) {
    LOG_ERR("GFX", "Font %d not found", fontId);
    return 0;
  }

  FontManager& fontManager = FontManager::getInstance();
  ExternalFont* externalFont = fontManager.getActiveFont();
  if (!fontManager.isExternalFontEnabled() || externalFont == nullptr) {
    return getTextWidthBuiltinFallback(fontId, fontIt->second, text, style);
  }

  const auto& builtinFont = fontIt->second;
  const EpdFontFamily* fallbackFont = getBuiltinFallbackForFontId(fontId);
  int width = 0;
  uint32_t previousBuiltinCp = 0;
  uint32_t cp = 0;

  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&text)))) {
    if (utf8IsCombiningMark(cp)) continue;

    const bool isCjk = isCjkCodepoint(cp);
    const bool useExternal = externalFont->handlesAllCodepoints() || isCjk;
    if (useExternal) {
      previousBuiltinCp = 0;

      // Legacy .bin is fixed-cell. TTF and EPDF use per-glyph metrics.
      if (!externalFont->isRichMetricsFormat()) {
        width += externalFont->getCharWidth();
        continue;
      }

      ExternalGlyphMetrics metrics{};
      metrics.width = externalFont->getCharWidth();
      metrics.height = externalFont->getCharHeight();
      metrics.advanceX = externalFont->getCharWidth();

      if (externalFont->getGlyphMetricsForLayout(cp, &metrics)) {
        if (isCjk && shouldUseCjkSymbolCellMetrics(cp)) {
          normalizeCjkSymbolMetricsForRendering(metrics, externalFont->getCharWidth(),
                                                externalFont->isRichMetricsFormat());
        }
        width += getExternalGlyphAdvanceForRendering(
            metrics, externalFont->getCharWidth(), 0, isCjk,
            shouldUseGlyphBoundsForAdvance(cp));
        continue;
      }

      // A selected external font may not actually contain every requested
      // codepoint. Prefer the embedded Traditional Chinese fallback before
      // displaying a tofu cell or the Latin family's replacement glyph.
      if (shouldUseBuiltinFallback(builtinFont, fallbackFont, cp, style)) {
        const EpdGlyph* fallbackGlyph =
            fallbackFont->getGlyphExact(cp, style);
        if (fallbackGlyph != nullptr) {
          width += fallbackAdvancePixels(fontId, fallbackGlyph, cp);
          continue;
        }
      }

      // Preserve legacy fixed-cell measurement when no embedded fallback has
      // the codepoint.
      if (!externalFont->handlesAllCodepoints() || isCjk) {
        width += externalFont->getCharWidth();
        continue;
      }
    }

    if (shouldUseBuiltinFallback(builtinFont, fallbackFont, cp, style)) {
      previousBuiltinCp = 0;
      const EpdGlyph* fallbackGlyph =
          fallbackFont->getGlyphExact(cp, style);
      if (fallbackGlyph != nullptr) {
        width += fallbackAdvancePixels(fontId, fallbackGlyph, cp);
      }
      continue;
    }

    cp = builtinFont.applyLigatures(cp, text, style);
    if (previousBuiltinCp != 0) {
      width += fp4::toPixel(builtinFont.getKerning(previousBuiltinCp, cp, style));
      width += latinPairTracking(previousBuiltinCp, cp);
    }
    const EpdGlyph* glyph = builtinFont.getGlyph(cp, style);
    if (glyph != nullptr) width += glyphAdvancePixels(glyph, cp);
    previousBuiltinCp = cp;
  }
  return width;
}

int GfxRenderer::getTextWidthExternalUi(
    const int fontId,
    const char* text,
    const EpdFontFamily::Style style
) const {
  if (text == nullptr || *text == '\0') {
    return 0;
  }

  const auto fontIt = fontMap.find(fontId);

  if (fontIt == fontMap.end()) {
    LOG_ERR("GFX", "UI font %d not found", fontId);
    return 0;
  }

  FontManager& fontManager =
      FontManager::getInstance();

  ExternalFont* uiFont =
      fontManager.getActiveUiFont();

  if (!fontManager.isUiFontEnabled() ||
      uiFont == nullptr ||
      !uiFont->isLoaded()) {
    return getTextWidthBuiltinFallback(fontId, fontIt->second, text, style);
  }

  const EpdFontFamily& builtinFont =
      fontIt->second;
  const EpdFontFamily* fallbackFont = getBuiltinFallbackForFontId(fontId);

  int widthPixels = 0;
  int32_t builtinWidthFP = 0;
  uint32_t previousBuiltinCp = 0;

  auto flushBuiltinWidth = [&]() {
    if (builtinWidthFP != 0) {
      widthPixels += fp4::toPixel(builtinWidthFP);
    }

    builtinWidthFP = 0;
    previousBuiltinCp = 0;
  };

  const char* ptr = text;
  uint32_t cp = 0;

  while ((cp = utf8NextCodepoint(
              reinterpret_cast<const uint8_t**>(&ptr)))) {

    if (utf8IsCombiningMark(cp)) {
      continue;
    }

    const EpdGlyph* builtinGlyph =
        builtinFont.getGlyphExact(cp, style);

    // 中文、全形符號、ASCII UI 字元，或內建 UI 字型沒有的字，
    // 優先使用外部 UI 字型，讓英數與中文大小和垂直位置一致。
    const bool tryExternal = shouldPreferExternalUiGlyph(cp, builtinGlyph);

    if (tryExternal) {
      // Legacy .bin UI fonts are fixed-cell fonts. Measuring a CJK glyph by
      // calling getGlyphMetricsForLayout() would seek to the glyph on SD and
      // scan every pixel just to rediscover the fixed cell width. The file
      // browser calls getTextWidth() many times while truncating every visible
      // filename, so that path turns one cursor move into hundreds of SD reads.
      //
      // The reader-font width path already has this fixed-cell shortcut. Keep
      // the UI path consistent and reserve per-glyph metric reads for EPDFont
      // rich-metrics fonts (or non-CJK fallback glyphs).
      if (!uiFont->isRichMetricsFormat() &&
          isCjkCodepoint(cp)) {
        flushBuiltinWidth();
        widthPixels += uiFont->getCharWidth();
        continue;
      }

      ExternalGlyphMetrics metrics{};

      metrics.width = uiFont->getCharWidth();
      metrics.height = uiFont->getCharHeight();
      metrics.advanceX = uiFont->getCharWidth();

      if (uiFont->getGlyphMetricsForLayout(
              cp,
              &metrics)) {

        flushBuiltinWidth();

        int advance = 0;

        if (shouldUseCjkSymbolCellMetrics(cp)) {
          normalizeCjkSymbolMetricsForRendering(
              metrics,
              uiFont->getCharWidth(),
              uiFont->isRichMetricsFormat()
          );

          advance =
              getExternalGlyphAdvanceForRendering(
                  metrics,
                  uiFont->getCharWidth(),
                  0,
                  true,
                  false
              );
        } else {
          advance =
              adjustNonRichAdvance(
                  metrics,
                  *uiFont
              );
        }

        widthPixels += std::max(1, advance);
        continue;
      }
    }

    if (shouldUseBuiltinFallback(builtinFont, fallbackFont, cp, style)) {
      flushBuiltinWidth();
      const EpdGlyph* fallbackGlyph =
          fallbackFont->getGlyphExact(cp, style);
      if (fallbackGlyph != nullptr) {
        widthPixels += fallbackAdvancePixels(fontId, fallbackGlyph, cp);
      }
      continue;
    }

    cp = builtinFont.applyLigatures(
        cp,
        ptr,
        style
    );

    if (previousBuiltinCp != 0) {
      builtinWidthFP += builtinFont.getKerning(
          previousBuiltinCp,
          cp,
          style
      );
      widthPixels += latinPairTracking(previousBuiltinCp, cp);
    }

    builtinGlyph =
        builtinFont.getGlyph(cp, style);

    // 外部與內建都沒有時，使用 ? 的寬度。
    if (builtinGlyph == nullptr) {
      builtinGlyph =
          builtinFont.getGlyph('?', style);
    }

    if (builtinGlyph != nullptr) {
      builtinWidthFP += glyphAdvanceFP(builtinGlyph, cp);
    }

    previousBuiltinCp = cp;
  }

  flushBuiltinWidth();

  return widthPixels;
}

int GfxRenderer::getTextWidth(
    const int fontId,
    const char* text,
    const EpdFontFamily::Style style
) const {
  FontManager& fontManager =
      FontManager::getInstance();

   // UI 外部字型
  if (shouldUseExternalUiFont(fontId) &&
      fontManager.isUiFontEnabled() &&
      fontManager.getActiveUiFont() != nullptr) {

    return getTextWidthExternalUi(
        fontId,
        text,
        style
    );
  }

  if (isReaderFont(fontId) &&
      fontManager.isExternalFontEnabled() &&
      fontManager.getActiveFont() != nullptr) {

    return getTextWidthExternalReader(
        fontId,
        text,
        style
    );
  }

  const auto fontIt = fontMap.find(fontId);

  if (fontIt == fontMap.end()) {
    LOG_ERR("GFX", "Font %d not found", fontId);
    return 0;
  }

  return getTextWidthBuiltinFallback(fontId, fontIt->second, text, style);
}

int GfxRenderer::getTextWidthScaled(const int fontId, const char* text, const int scale,
                                    const EpdFontFamily::Style style) const {
  const int safeScale = std::max(1, scale);
  return getTextWidth(fontId, text, style) * safeScale;
}

void GfxRenderer::drawCenteredText(const int fontId, const int y, const char* text, const bool black,
                                   const EpdFontFamily::Style style) const {
  const int x = (getScreenWidth() - getTextWidth(fontId, text, style)) / 2;
  drawText(fontId, x, y, text, black, style);
}

void GfxRenderer::drawCenteredTextScaled(const int fontId, const int y, const char* text, const int scale,
                                         const bool black, const EpdFontFamily::Style style) const {
  const int safeScale = std::max(1, scale);
  const int x = (getScreenWidth() - getTextWidthScaled(fontId, text, safeScale, style)) / 2;
  drawTextScaled(fontId, x, y, text, safeScale, black, style);
}

void GfxRenderer::drawText(const int fontId, const int x, const int y, const char* text, const bool black,
                           const EpdFontFamily::Style style) const {
  const int yPos = y + getFontAscenderSize(fontId);
  int lastBaseX = x;
  int lastBaseLeft = 0;
  int lastBaseWidth = 0;
  int lastBaseTop = 0;
  int32_t prevAdvanceFP = 0;  // 12.4 fixed-point: prev glyph's advance + next kern for snap

  // cannot draw a NULL / empty string
  if (text == nullptr || *text == '\0') {
    return;
  }

  if (fontCacheManager_ && fontCacheManager_->isScanning()) {
    fontCacheManager_->recordText(text, fontId, style);
    return;
  }

  const auto fontIt = fontMap.find(fontId);
  if (fontIt == fontMap.end()) {
    LOG_ERR("GFX", "Font %d not found", fontId);
    return;
  }
  const auto& font = fontIt->second;
  const EpdFontFamily* fallbackFont = getBuiltinFallbackForFontId(fontId);

  ExternalFont* activeUiFont = nullptr;

  if (shouldUseExternalUiFont(fontId)) {
    FontManager& fontManager =
        FontManager::getInstance();

    if (fontManager.isUiFontEnabled()) {
      activeUiFont =
          fontManager.getActiveUiFont();

      if (activeUiFont != nullptr &&
          !activeUiFont->isLoaded()) {
        activeUiFont = nullptr;
      }
    }
  }

  /*
   * drawText() receives y as the top of the text line. When a UI line mixes
   * external CJK cells and built-in Latin fallback glyphs, align the smaller
   * Latin line box inside the external UI line box instead of anchoring it at
   * the very top. This prevents English letters and digits from looking too
   * small and visually shifted upward next to Chinese text.
   */
  int builtinBaselineY = yPos;

  if (activeUiFont != nullptr || fallbackFont != nullptr) {
    const EpdFontData* builtinData = font.getData(style);
    if (builtinData != nullptr) {
      int verticalOffset = 0;
      if (activeUiFont != nullptr) {
        const int externalLineHeight = getExternalFontLineHeightForRendering(*activeUiFont);
        verticalOffset = std::max(0, (externalLineHeight - static_cast<int>(builtinData->advanceY)) / 2);
      }
      builtinBaselineY = y + builtinData->ascender + verticalOffset;
    }
  }

  uint32_t cp;
  uint32_t prevCp = 0;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&text)))) {
    if (utf8IsCombiningMark(cp)) {
      const EpdGlyph* combiningGlyph = font.getGlyph(cp, style);
      if (!combiningGlyph) continue;
      const int raiseBy = combiningMark::raiseAboveBase(combiningGlyph->top, combiningGlyph->height, lastBaseTop);
      const int combiningX = combiningMark::centerOver(lastBaseX, lastBaseLeft, lastBaseWidth, combiningGlyph->left,
                                                       combiningGlyph->width);
      renderCharImpl<TextRotation::None>(*this, renderMode, font, cp, combiningX, builtinBaselineY - raiseBy, black, style);
      continue;
    }

        const bool useExternalReaderFont =
        isReaderFont(fontId) &&
        FontManager::getInstance().isExternalFontEnabled();

    ExternalFont* activeReaderFont = useExternalReaderFont
                                         ? FontManager::getInstance().getActiveFont()
                                         : nullptr;
    if (activeReaderFont != nullptr &&
        (activeReaderFont->handlesAllCodepoints() || isCjkCodepoint(cp))) {

      // 先把前一個內建字型 glyph 尚未套用的 advance 補上
      if (prevCp != 0) {
        lastBaseX += fp4::toPixel(prevAdvanceFP);
      }

      prevCp = 0;
      prevAdvanceFP = 0;

      if (renderExternalReaderGlyph(
              cp,
              &lastBaseX,
              yPos,
              black)) {

        // 外部 glyph 已經自行前進 lastBaseX
        lastBaseLeft = 0;
        lastBaseWidth = 0;
        lastBaseTop = 0;

        continue;
      }
    }

        if (activeUiFont != nullptr) {
      const EpdGlyph* builtinGlyph =
          font.getGlyphExact(cp, style);

      const bool tryExternalUi = shouldPreferExternalUiGlyph(cp, builtinGlyph);

      if (tryExternalUi) {
        // 先結算前一個內建 glyph 尚未套用的 advance。
        if (prevCp != 0) {
          lastBaseX +=
              fp4::toPixel(prevAdvanceFP);
        }

        prevCp = 0;
        prevAdvanceFP = 0;

        const int externalBaseline =
            y +
            getExternalFontAscenderForRendering(
                *activeUiFont
            );

        if (renderExternalUiGlyph(
                cp,
                &lastBaseX,
                externalBaseline,
                black)) {

          lastBaseLeft = 0;
          lastBaseWidth = 0;
          lastBaseTop = 0;

          continue;
        }

        // 外部 UI 字型缺字時，繼續嘗試內嵌繁中字型。
      }
    }

    if (shouldUseBuiltinFallback(font, fallbackFont, cp, style)) {
      if (prevCp != 0) {
        lastBaseX += fp4::toPixel(prevAdvanceFP);
      }
      prevCp = 0;
      prevAdvanceFP = 0;

      const EpdGlyph* fallbackGlyph =
          fallbackFont->getGlyphExact(cp, style);
      const EpdFontData* fallbackData =
          fallbackFont->getData(style);

      if (fallbackGlyph != nullptr && fallbackData != nullptr) {
        if (usesScaledReaderCjkFallback(fontId)) {
          const int percent = fallbackScalePercentForFontId(fontId);
          renderBuiltinFallbackGlyphScaledPercent(
              *fallbackFont, cp, lastBaseX, y, percent, black, style);
          lastBaseLeft = scaleBuiltinCjkAxis(fallbackGlyph->left, percent, BUILTIN_CJK_LOGICAL_CELL_WIDTH, BUILTIN_CJK_SOURCE_CELL_WIDTH);
          lastBaseWidth = scaleBuiltinCjkAxisCeil(fallbackGlyph->width, percent, BUILTIN_CJK_LOGICAL_CELL_WIDTH, BUILTIN_CJK_SOURCE_CELL_WIDTH);
          lastBaseTop = scaleBuiltinCjkAxis(fallbackGlyph->top, percent, BUILTIN_CJK_LOGICAL_CELL_HEIGHT, BUILTIN_CJK_SOURCE_CELL_HEIGHT);
        } else if (usesCompactScaledCjkFallback(fontId)) {
          renderBuiltinFallbackGlyphScaled(
              *fallbackFont, cp, lastBaseX, y, black, style);
          lastBaseLeft = scaleCompactMetric(fallbackGlyph->left);
          lastBaseWidth = scaleCompactMetricCeil(fallbackGlyph->width);
          lastBaseTop = scaleCompactMetric(fallbackGlyph->top);
        } else {
          renderCharImpl<TextRotation::None>(
              *this, renderMode, *fallbackFont, cp,
              lastBaseX, y + fallbackData->ascender, black, style);
          lastBaseLeft = fallbackGlyph->left;
          lastBaseWidth = fallbackGlyph->width;
          lastBaseTop = fallbackGlyph->top;
        }
        lastBaseX += fallbackAdvancePixels(fontId, fallbackGlyph, cp);
        continue;
      }
    }

    cp = font.applyLigatures(cp, text, style);

    // Differential rounding: snap (previous advance + current kern) as one unit so
    // identical character pairs always produce the same pixel step regardless of
    // where they fall on the line.
    if (prevCp != 0) {
      const auto kernFP = font.getKerning(prevCp, cp, style);  // 4.4 fixed-point kern
      lastBaseX += fp4::toPixel(prevAdvanceFP + kernFP);       // snap 12.4 fixed-point to nearest pixel
      lastBaseX += latinPairTracking(prevCp, cp);
    }

    const EpdGlyph* glyph = font.getGlyph(cp, style);

    lastBaseLeft = glyph ? glyph->left : 0;
    lastBaseWidth = glyph ? glyph->width : 0;
    lastBaseTop = glyph ? glyph->top : 0;
    prevAdvanceFP = glyphAdvanceFP(glyph, cp);  // 12.4 fixed-point

    renderCharImpl<TextRotation::None>(*this, renderMode, font, cp, lastBaseX, builtinBaselineY, black, style);
    prevCp = cp;
  }
}

void GfxRenderer::drawTextScaled(const int fontId, const int x, const int y, const char* text, const int scale,
                                 const bool black, const EpdFontFamily::Style style) const {
  const int safeScale = std::max(1, scale);
  if (safeScale == 1) {
    drawText(fontId, x, y, text, black, style);
    return;
  }

  if (text == nullptr || *text == '\0') {
    return;
  }

  if (fontCacheManager_ && fontCacheManager_->isScanning()) {
    fontCacheManager_->recordText(text, fontId, style);
    return;
  }

  const auto fontIt = fontMap.find(fontId);
  if (fontIt == fontMap.end()) {
    LOG_ERR("GFX", "Font %d not found", fontId);
    return;
  }

  const EpdFontFamily& font = fontIt->second;
  const EpdFontFamily* fallbackFont = getBuiltinFallbackForFontId(fontId);
  const EpdFontData* primaryData = font.getData(style);
  const int primaryBaselineY =
      y + (primaryData != nullptr ? static_cast<int>(primaryData->ascender) * safeScale
                                  : getFontAscenderSize(fontId) * safeScale);

  int cursorX = x;
  int32_t prevAdvanceFP = 0;
  uint32_t prevCp = 0;
  const char* cursor = text;
  uint32_t cp = 0;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&cursor)))) {
    if (utf8IsCombiningMark(cp)) {
      continue;
    }

    if (shouldUseBuiltinFallback(font, fallbackFont, cp, style)) {
      if (prevCp != 0) {
        cursorX += fp4::toPixel(prevAdvanceFP) * safeScale;
      }
      prevCp = 0;
      prevAdvanceFP = 0;

      const EpdGlyph* fallbackGlyph = fallbackFont->getGlyphExact(cp, style);
      const EpdFontData* fallbackData = fallbackFont->getData(style);
      if (fallbackGlyph != nullptr && fallbackData != nullptr) {
        if (usesScaledReaderCjkFallback(fontId)) {
          const int percent = fallbackScalePercentForFontId(fontId) * safeScale;
          renderBuiltinFallbackGlyphScaledPercent(*fallbackFont, cp, cursorX, y, percent, black, style);
          cursorX += std::max(1, scaleBuiltinCjkAxis(glyphAdvancePixels(fallbackGlyph, cp), percent, BUILTIN_CJK_LOGICAL_CELL_WIDTH, BUILTIN_CJK_SOURCE_CELL_WIDTH));
        } else {
          const int fallbackBaselineY = y + static_cast<int>(fallbackData->ascender) * safeScale;
          renderCharImplScaled(*this, renderMode, *fallbackFont, cp, cursorX, fallbackBaselineY, safeScale, black, style);
          cursorX += fallbackAdvancePixels(fontId, fallbackGlyph, cp) * safeScale;
        }
        continue;
      }
    }

    cp = font.applyLigatures(cp, cursor, style);
    if (prevCp != 0) {
      cursorX += fp4::toPixel(prevAdvanceFP + font.getKerning(prevCp, cp, style)) * safeScale;
      cursorX += latinPairTracking(prevCp, cp) * safeScale;
    }

    const EpdGlyph* glyph = font.getGlyph(cp, style);
    prevAdvanceFP = glyphAdvanceFP(glyph, cp);
    renderCharImplScaled(*this, renderMode, font, cp, cursorX, primaryBaselineY, safeScale, black, style);
    prevCp = cp;
  }
}

void GfxRenderer::drawVerticalText(
    const int fontId,
    const int rightX,
    const int topY,
    const char* text,
    const bool black,
    const EpdFontFamily::Style style
) const {
  if (text == nullptr || *text == '\0') {
    return;
  }

  int columnX = rightX;
  int cursorY = topY;

  const int glyphAdvance = std::max(1, getLineHeight(fontId));
  const int columnAdvance = std::max(1, getLineHeight(fontId));

  const int bottomMargin = 20;
  const int bottomLimit = getScreenHeight() - bottomMargin;

  const uint8_t* cursor = reinterpret_cast<const uint8_t*>(text);

  auto renderCenteredAnyFont = [&](const uint32_t renderCp) -> bool {
    bool ok = renderExternalReaderGlyphCentered(
        fontId, columnX, cursorY, glyphAdvance, glyphAdvance, renderCp, black);
    if (ok) return true;

    ok = renderPrimaryGlyphCentered(
        fontId, columnX, cursorY, glyphAdvance, glyphAdvance, renderCp, black, style);
    if (ok) return true;

    return renderBuiltinFallbackGlyphCentered(
        fontId, columnX, cursorY, glyphAdvance, glyphAdvance, renderCp, black, style);
  };

  while (*cursor != 0) {
    const uint8_t* codepointStart = cursor;
    const uint32_t cp = utf8NextCodepoint(&cursor);
    if (cp == 0) break;
    if (cp == '\r') continue;

    if (cp == '\n') {
      cursorY = topY;
      columnX -= columnAdvance;
      continue;
    }

    if (cursorY + glyphAdvance > bottomLimit) {
      cursorY = topY;
      columnX -= columnAdvance;
    }
    if (columnX < 0) break;

    const size_t utf8Length = static_cast<size_t>(cursor - codepointStart);
    if (utf8Length == 0 || utf8Length > 4) continue;

    char glyphText[5] = {0};
    std::memcpy(glyphText, codepointStart, utf8Length);

    bool rendered = false;

    // Prefer proper vertical presentation forms for East Asian punctuation.
    // This avoids the old one-size-fits-all 90° rotation that made corner
    // brackets/parentheses appear wrong or 180° off in vertical layout.
    const uint32_t verticalCp = verticalPresentationForm(cp);
    if (verticalCp != 0) {
      rendered = renderCenteredAnyFont(verticalCp);
    }

    // If the selected font does not include a vertical presentation glyph,
    // fall back to the old rotation path for known punctuation.  CJK ideographs
    // are never intentionally rotated here.
    if (!rendered && shouldRotateVerticalGlyph(cp)) {
      rendered = renderExternalReaderGlyphRotated90CW(
          fontId, columnX, cursorY, glyphAdvance, cp, black);
      if (!rendered) {
        rendered = renderBuiltinFallbackGlyphRotated90CW(
            fontId, columnX, cursorY, glyphAdvance, cp, black, style);
      }
    }

    if (!rendered) {
      rendered = renderCenteredAnyFont(cp);
    }

    // Last-resort fallback.  In normal cases built-in Latin primary glyphs,
    // external glyphs and CJK fallback glyphs all use centered vertical cells,
    // so ASCII digits/letters no longer start at the left edge of the cell.
    if (!rendered) {
      drawText(fontId, columnX, cursorY, glyphText, black, style);
    }

    cursorY += glyphAdvance;
  }
}

void GfxRenderer::drawLine(int x1, int y1, int x2, int y2, const bool state) const {
  if (fontCacheManager_ && fontCacheManager_->isScanning()) return;
  if (x1 == x2) {
    if (y2 < y1) {
      std::swap(y1, y2);
    }
    for (int y = y1; y <= y2; y++) {
      drawPixel(x1, y, state);
    }
  } else if (y1 == y2) {
    if (x2 < x1) {
      std::swap(x1, x2);
    }
    for (int x = x1; x <= x2; x++) {
      drawPixel(x, y1, state);
    }
  } else {
    // Bresenham's line algorithm — integer arithmetic only
    int dx = x2 - x1;
    int dy = y2 - y1;
    int sx = (dx > 0) ? 1 : -1;
    int sy = (dy > 0) ? 1 : -1;
    dx = sx * dx;  // abs
    dy = sy * dy;  // abs

    int err = dx - dy;
    while (true) {
      drawPixel(x1, y1, state);
      if (x1 == x2 && y1 == y2) break;
      int e2 = 2 * err;
      if (e2 > -dy) {
        err -= dy;
        x1 += sx;
      }
      if (e2 < dx) {
        err += dx;
        y1 += sy;
      }
    }
  }
}

void GfxRenderer::drawLine(int x1, int y1, int x2, int y2, const int lineWidth, const bool state) const {
  for (int i = 0; i < lineWidth; i++) {
    drawLine(x1, y1 + i, x2, y2 + i, state);
  }
}

void GfxRenderer::drawRect(const int x, const int y, const int width, const int height, const bool state) const {
  drawLine(x, y, x + width - 1, y, state);
  drawLine(x + width - 1, y, x + width - 1, y + height - 1, state);
  drawLine(x + width - 1, y + height - 1, x, y + height - 1, state);
  drawLine(x, y, x, y + height - 1, state);
}

// Border is inside the rectangle
void GfxRenderer::drawRect(const int x, const int y, const int width, const int height, const int lineWidth,
                           const bool state) const {
  for (int i = 0; i < lineWidth; i++) {
    drawLine(x + i, y + i, x + width - i, y + i, state);
    drawLine(x + width - i, y + i, x + width - i, y + height - i, state);
    drawLine(x + width - i, y + height - i, x + i, y + height - i, state);
    drawLine(x + i, y + height - i, x + i, y + i, state);
  }
}

void GfxRenderer::drawArc(const int maxRadius, const int cx, const int cy, const int xDir, const int yDir,
                          const int lineWidth, const bool state) const {
  const int stroke = std::min(lineWidth, maxRadius);
  const int innerRadius = std::max(maxRadius - stroke, 0);
  const int outerRadius = maxRadius;

  if (outerRadius <= 0) {
    return;
  }

  const int outerRadiusSq = outerRadius * outerRadius;
  const int innerRadiusSq = innerRadius * innerRadius;

  int xOuter = outerRadius;
  int xInner = innerRadius;

  for (int dy = 0; dy <= outerRadius; ++dy) {
    while (xOuter > 0 && (xOuter * xOuter + dy * dy) > outerRadiusSq) {
      --xOuter;
    }
    // Keep the smallest x that still lies outside/at the inner radius,
    // i.e. (x^2 + y^2) >= innerRadiusSq.
    while (xInner > 0 && ((xInner - 1) * (xInner - 1) + dy * dy) >= innerRadiusSq) {
      --xInner;
    }

    if (xOuter < xInner) {
      continue;
    }

    const int x0 = cx + xDir * xInner;
    const int x1 = cx + xDir * xOuter;
    const int left = std::min(x0, x1);
    const int width = std::abs(x1 - x0) + 1;
    const int py = cy + yDir * dy;

    if (width > 0) {
      fillRect(left, py, width, 1, state);
    }
  }
};

// Border is inside the rectangle, rounded corners
void GfxRenderer::drawRoundedRect(const int x, const int y, const int width, const int height, const int lineWidth,
                                  const int cornerRadius, bool state) const {
  drawRoundedRect(x, y, width, height, lineWidth, cornerRadius, true, true, true, true, state);
}

// Border is inside the rectangle, rounded corners
void GfxRenderer::drawRoundedRect(const int x, const int y, const int width, const int height, const int lineWidth,
                                  const int cornerRadius, bool roundTopLeft, bool roundTopRight, bool roundBottomLeft,
                                  bool roundBottomRight, bool state) const {
  if (lineWidth <= 0 || width <= 0 || height <= 0) {
    return;
  }

  const int maxRadius = std::min({cornerRadius, width / 2, height / 2});
  if (maxRadius <= 0) {
    drawRect(x, y, width, height, lineWidth, state);
    return;
  }

  const int stroke = std::min(lineWidth, maxRadius);
  const int right = x + width - 1;
  const int bottom = y + height - 1;

  const int horizontalWidth = width - 2 * maxRadius;
  if (horizontalWidth > 0) {
    if (roundTopLeft || roundTopRight) {
      fillRect(x + maxRadius, y, horizontalWidth, stroke, state);
    }
    if (roundBottomLeft || roundBottomRight) {
      fillRect(x + maxRadius, bottom - stroke + 1, horizontalWidth, stroke, state);
    }
  }

  const int verticalHeight = height - 2 * maxRadius;
  if (verticalHeight > 0) {
    if (roundTopLeft || roundBottomLeft) {
      fillRect(x, y + maxRadius, stroke, verticalHeight, state);
    }
    if (roundTopRight || roundBottomRight) {
      fillRect(right - stroke + 1, y + maxRadius, stroke, verticalHeight, state);
    }
  }

  if (roundTopLeft) {
    drawArc(maxRadius, x + maxRadius, y + maxRadius, -1, -1, lineWidth, state);
  }
  if (roundTopRight) {
    drawArc(maxRadius, right - maxRadius, y + maxRadius, 1, -1, lineWidth, state);
  }
  if (roundBottomRight) {
    drawArc(maxRadius, right - maxRadius, bottom - maxRadius, 1, 1, lineWidth, state);
  }
  if (roundBottomLeft) {
    drawArc(maxRadius, x + maxRadius, bottom - maxRadius, -1, 1, lineWidth, state);
  }
}

void GfxRenderer::fillRect(const int x, const int y, const int width, const int height, const bool state) const {
  for (int fillY = y; fillY < y + height; fillY++) {
    drawLine(x, fillY, x + width - 1, fillY, state);
  }
}

// NOTE: Those are in critical path, and need to be templated to avoid runtime checks for every pixel.
// Any branching must be done outside the loops to avoid performance degradation.
template <>
void GfxRenderer::drawPixelDither<Color::Clear>(const int x, const int y) const {
  // Do nothing
}

template <>
void GfxRenderer::drawPixelDither<Color::Black>(const int x, const int y) const {
  drawPixel(x, y, true);
}

template <>
void GfxRenderer::drawPixelDither<Color::White>(const int x, const int y) const {
  drawPixel(x, y, false);
}

template <>
void GfxRenderer::drawPixelDither<Color::LightGray>(const int x, const int y) const {
  drawPixel(x, y, x % 2 == 0 && y % 2 == 0);
}

template <>
void GfxRenderer::drawPixelDither<Color::DarkGray>(const int x, const int y) const {
  drawPixel(x, y, (x + y) % 2 == 0);  // TODO: maybe find a better pattern?
}

void GfxRenderer::fillRectDither(const int x, const int y, const int width, const int height, Color color) const {
  if (color == Color::Clear) {
  } else if (color == Color::Black) {
    fillRect(x, y, width, height, true);
  } else if (color == Color::White) {
    fillRect(x, y, width, height, false);
  } else if (color == Color::LightGray) {
    for (int fillY = y; fillY < y + height; fillY++) {
      for (int fillX = x; fillX < x + width; fillX++) {
        drawPixelDither<Color::LightGray>(fillX, fillY);
      }
    }
  } else if (color == Color::DarkGray) {
    for (int fillY = y; fillY < y + height; fillY++) {
      for (int fillX = x; fillX < x + width; fillX++) {
        drawPixelDither<Color::DarkGray>(fillX, fillY);
      }
    }
  }
}

template <Color color>
void GfxRenderer::fillArc(const int maxRadius, const int cx, const int cy, const int xDir, const int yDir) const {
  if (maxRadius <= 0) return;

  if constexpr (color == Color::Clear) {
    return;
  }

  const int radiusSq = maxRadius * maxRadius;

  // Avoid sqrt by scanning from outer radius inward while y grows.
  int x = maxRadius;
  for (int dy = 0; dy <= maxRadius; ++dy) {
    while (x > 0 && (x * x + dy * dy) > radiusSq) {
      --x;
    }
    if (x < 0) break;

    const int py = cy + yDir * dy;
    if (py < 0 || py >= getScreenHeight()) continue;

    int x0 = cx;
    int x1 = cx + xDir * x;
    if (x0 > x1) std::swap(x0, x1);
    const int width = x1 - x0 + 1;

    if (width <= 0) continue;

    if constexpr (color == Color::Black) {
      fillRect(x0, py, width, 1, true);
    } else if constexpr (color == Color::White) {
      fillRect(x0, py, width, 1, false);
    } else {
      // LightGray / DarkGray: use existing dithered fill path.
      fillRectDither(x0, py, width, 1, color);
    }
  }
}

void GfxRenderer::fillRoundedRect(const int x, const int y, const int width, const int height, const int cornerRadius,
                                  const Color color) const {
  fillRoundedRect(x, y, width, height, cornerRadius, true, true, true, true, color);
}

void GfxRenderer::fillRoundedRect(const int x, const int y, const int width, const int height, const int cornerRadius,
                                  bool roundTopLeft, bool roundTopRight, bool roundBottomLeft, bool roundBottomRight,
                                  const Color color) const {
  if (width <= 0 || height <= 0) {
    return;
  }

  // Assume if we're not rounding all corners then we are only rounding one side
  const int roundedSides = (!roundTopLeft || !roundTopRight || !roundBottomLeft || !roundBottomRight) ? 1 : 2;
  const int maxRadius = std::min({cornerRadius, width / roundedSides, height / roundedSides});
  if (maxRadius <= 0) {
    fillRectDither(x, y, width, height, color);
    return;
  }

  const int horizontalWidth = width - 2 * maxRadius;
  if (horizontalWidth > 0) {
    fillRectDither(x + maxRadius + 1, y, horizontalWidth - 2, height, color);
  }

  const int leftFillTop = y + (roundTopLeft ? (maxRadius + 1) : 0);
  const int leftFillBottom = y + height - 1 - (roundBottomLeft ? (maxRadius + 1) : 0);
  if (leftFillBottom >= leftFillTop) {
    fillRectDither(x, leftFillTop, maxRadius + 1, leftFillBottom - leftFillTop + 1, color);
  }

  const int rightFillTop = y + (roundTopRight ? (maxRadius + 1) : 0);
  const int rightFillBottom = y + height - 1 - (roundBottomRight ? (maxRadius + 1) : 0);
  if (rightFillBottom >= rightFillTop) {
    fillRectDither(x + width - maxRadius - 1, rightFillTop, maxRadius + 1, rightFillBottom - rightFillTop + 1, color);
  }

  auto fillArcTemplated = [this](int maxRadius, int cx, int cy, int xDir, int yDir, Color color) {
    switch (color) {
      case Color::Clear:
        break;
      case Color::Black:
        fillArc<Color::Black>(maxRadius, cx, cy, xDir, yDir);
        break;
      case Color::White:
        fillArc<Color::White>(maxRadius, cx, cy, xDir, yDir);
        break;
      case Color::LightGray:
        fillArc<Color::LightGray>(maxRadius, cx, cy, xDir, yDir);
        break;
      case Color::DarkGray:
        fillArc<Color::DarkGray>(maxRadius, cx, cy, xDir, yDir);
        break;
    }
  };

  if (roundTopLeft) {
    fillArcTemplated(maxRadius, x + maxRadius, y + maxRadius, -1, -1, color);
  }

  if (roundTopRight) {
    fillArcTemplated(maxRadius, x + width - maxRadius - 1, y + maxRadius, 1, -1, color);
  }

  if (roundBottomRight) {
    fillArcTemplated(maxRadius, x + width - maxRadius - 1, y + height - maxRadius - 1, 1, 1, color);
  }

  if (roundBottomLeft) {
    fillArcTemplated(maxRadius, x + maxRadius, y + height - maxRadius - 1, -1, 1, color);
  }
}

void GfxRenderer::drawImage(const uint8_t bitmap[], const int x, const int y, const int width, const int height) const {
  /*
   * Source images are 1-bit packed in logical image order.  Older builds
   * translated only the origin and then copied the packed bits directly to the
   * physical framebuffer.  That made square 1-bit assets such as Logo120 look
   * rotated on portrait-oriented Paper S3 screens, most visibly on the default
   * sleep screen.
   *
   * drawImage() is used only for small built-in UI assets, so a per-pixel path
   * is acceptable and keeps the bitmap orientation consistent with normal
   * renderer drawing in every screen orientation.
   */
  if (bitmap == nullptr || width <= 0 || height <= 0) return;

  const int imageWidthBytes = (width + 7) / 8;
  for (int row = 0; row < height; ++row) {
    const int logicalY = y + row;
    for (int col = 0; col < width; ++col) {
      const uint8_t srcByte = bitmap[row * imageWidthBytes + col / 8];
      const bool sourceWhite = (srcByte & (0x80 >> (col % 8))) != 0;
      drawPixel(x + col, logicalY, !sourceWhite);
    }
  }
}

void GfxRenderer::drawIcon(const uint8_t bitmap[], const int x, const int y, const int width, const int height) const {
  /*
   * Icons are stored as normal 1-bit logical bitmaps.  The old Paper S3 path
   * copied the packed bytes directly to the physical framebuffer with swapped
   * coordinates, which made newly added asymmetric icons such as Power appear
   * rotated by 90 degrees.  Draw icons through the logical pixel path instead
   * so they follow the same orientation as text and other UI elements.
   *
   * White source pixels are transparent; only black pixels are drawn.
   */
  if (bitmap == nullptr || width <= 0 || height <= 0) return;

  const int imageWidthBytes = (width + 7) / 8;
  for (int row = 0; row < height; ++row) {
    for (int col = 0; col < width; ++col) {
      const uint8_t srcByte = bitmap[row * imageWidthBytes + col / 8];
      const bool sourceWhite = (srcByte & (0x80 >> (col % 8))) != 0;
      if (!sourceWhite) {
        drawPixel(x + col, y + row, true);
      }
    }
  }
}

void GfxRenderer::drawBitmap(const Bitmap& bitmap, const int x, const int y, const int maxWidth, const int maxHeight,
                             const float cropX, const float cropY) const {
  if (fontCacheManager_ && fontCacheManager_->isScanning()) return;
  // For 1-bit bitmaps, use optimized 1-bit rendering path (no crop support for 1-bit)
  if (bitmap.is1Bit() && cropX == 0.0f && cropY == 0.0f) {
    drawBitmap1Bit(bitmap, x, y, maxWidth, maxHeight);
    return;
  }

  float scale = 1.0f;
  bool isScaled = false;
  int cropPixX = std::floor(bitmap.getWidth() * cropX / 2.0f);
  int cropPixY = std::floor(bitmap.getHeight() * cropY / 2.0f);
  LOG_DBG("GFX", "Cropping %dx%d by %dx%d pix, is %s", bitmap.getWidth(), bitmap.getHeight(), cropPixX, cropPixY,
          bitmap.isTopDown() ? "top-down" : "bottom-up");

  if (maxWidth > 0 && (1.0f - cropX) * bitmap.getWidth() > maxWidth) {
    scale = static_cast<float>(maxWidth) / static_cast<float>((1.0f - cropX) * bitmap.getWidth());
    isScaled = true;
  }
  if (maxHeight > 0 && (1.0f - cropY) * bitmap.getHeight() > maxHeight) {
    scale = std::min(scale, static_cast<float>(maxHeight) / static_cast<float>((1.0f - cropY) * bitmap.getHeight()));
    isScaled = true;
  }
  LOG_DBG("GFX", "Scaling by %f - %s", scale, isScaled ? "scaled" : "not scaled");

  // Calculate output row size (2 bits per pixel, packed into bytes)
  // IMPORTANT: Use int, not uint8_t, to avoid overflow for images > 1020 pixels wide
  const int outputRowSize = (bitmap.getWidth() + 3) / 4;
  auto* outputRow = static_cast<uint8_t*>(malloc(outputRowSize));
  auto* rowBytes = static_cast<uint8_t*>(malloc(bitmap.getRowBytes()));

  if (!outputRow || !rowBytes) {
    LOG_ERR("GFX", "!! Failed to allocate BMP row buffers");
    free(outputRow);
    free(rowBytes);
    return;
  }

  for (int bmpY = 0; bmpY < (bitmap.getHeight() - cropPixY); bmpY++) {
    // The BMP's (0, 0) is the bottom-left corner (if the height is positive, top-left if negative).
    // Screen's (0, 0) is the top-left corner.
    int screenY = -cropPixY + (bitmap.isTopDown() ? bmpY : bitmap.getHeight() - 1 - bmpY);
    if (isScaled) {
      screenY = std::floor(screenY * scale);
    }
    screenY += y;  // the offset should not be scaled
    if (screenY >= getScreenHeight()) {
      break;
    }

    if (bitmap.readNextRow(outputRow, rowBytes) != BmpReaderError::Ok) {
      LOG_ERR("GFX", "Failed to read row %d from bitmap", bmpY);
      free(outputRow);
      free(rowBytes);
      return;
    }

    if (screenY < 0) {
      continue;
    }

    if (bmpY < cropPixY) {
      // Skip the row if it's outside the crop area
      continue;
    }

    for (int bmpX = cropPixX; bmpX < bitmap.getWidth() - cropPixX; bmpX++) {
      int screenX = bmpX - cropPixX;
      if (isScaled) {
        screenX = std::floor(screenX * scale);
      }
      screenX += x;  // the offset should not be scaled
      if (screenX >= getScreenWidth()) {
        break;
      }
      if (screenX < 0) {
        continue;
      }

      const uint8_t val = outputRow[bmpX / 4] >> (6 - ((bmpX * 2) % 8)) & 0x3;

      if (renderMode == GRAYSCALE_DIRECT && val < 3) {
        // val: 0=black, 1=dark grey, 2=light grey; EPD: 0=white, 3=black
        drawPixelGray(screenX, screenY, 3 - val);
      } else if (renderMode == BW && val < 3) {
        drawPixel(screenX, screenY);
      } else if (renderMode == GRAYSCALE_MSB && (val == 1 || val == 2)) {
        drawPixel(screenX, screenY, false);
      } else if (renderMode == GRAYSCALE_LSB && val == 1) {
        drawPixel(screenX, screenY, false);
      }
    }
  }

  free(outputRow);
  free(rowBytes);
}

void GfxRenderer::drawBitmap1Bit(const Bitmap& bitmap, const int x, const int y, const int maxWidth,
                                 const int maxHeight) const {
  float scale = 1.0f;
  bool isScaled = false;
  if (maxWidth > 0 && bitmap.getWidth() > maxWidth) {
    scale = static_cast<float>(maxWidth) / static_cast<float>(bitmap.getWidth());
    isScaled = true;
  }
  if (maxHeight > 0 && bitmap.getHeight() > maxHeight) {
    scale = std::min(scale, static_cast<float>(maxHeight) / static_cast<float>(bitmap.getHeight()));
    isScaled = true;
  }

  // For 1-bit BMP, output is still 2-bit packed (for consistency with readNextRow)
  const int outputRowSize = (bitmap.getWidth() + 3) / 4;
  auto* outputRow = static_cast<uint8_t*>(malloc(outputRowSize));
  auto* rowBytes = static_cast<uint8_t*>(malloc(bitmap.getRowBytes()));

  if (!outputRow || !rowBytes) {
    LOG_ERR("GFX", "!! Failed to allocate 1-bit BMP row buffers");
    free(outputRow);
    free(rowBytes);
    return;
  }

  for (int bmpY = 0; bmpY < bitmap.getHeight(); bmpY++) {
    // Read rows sequentially using readNextRow
    if (bitmap.readNextRow(outputRow, rowBytes) != BmpReaderError::Ok) {
      LOG_ERR("GFX", "Failed to read row %d from 1-bit bitmap", bmpY);
      free(outputRow);
      free(rowBytes);
      return;
    }

    // Calculate screen Y based on whether BMP is top-down or bottom-up
    const int bmpYOffset = bitmap.isTopDown() ? bmpY : bitmap.getHeight() - 1 - bmpY;
    int screenY = y + (isScaled ? static_cast<int>(std::floor(bmpYOffset * scale)) : bmpYOffset);
    if (screenY >= getScreenHeight()) {
      continue;  // Continue reading to keep row counter in sync
    }
    if (screenY < 0) {
      continue;
    }

    for (int bmpX = 0; bmpX < bitmap.getWidth(); bmpX++) {
      int screenX = x + (isScaled ? static_cast<int>(std::floor(bmpX * scale)) : bmpX);
      if (screenX >= getScreenWidth()) {
        break;
      }
      if (screenX < 0) {
        continue;
      }

      // Get 2-bit value (result of readNextRow quantization)
      const uint8_t val = outputRow[bmpX / 4] >> (6 - ((bmpX * 2) % 8)) & 0x3;

      // For 1-bit source: 0 or 1 -> map to black (0,1,2) or white (3)
      // val < 3 means black pixel (draw it)
      if (val < 3) {
        if (renderMode == GRAYSCALE_DIRECT) {
          drawPixelGray(screenX, screenY, 3 - val);
        } else {
          drawPixel(screenX, screenY, true);
        }
      }
      // White pixels (val == 3) are not drawn (leave background)
    }
  }

  free(outputRow);
  free(rowBytes);
}

void GfxRenderer::fillPolygon(const int* xPoints, const int* yPoints, int numPoints, bool state) const {
  if (numPoints < 3) return;

  // Find bounding box
  int minY = yPoints[0], maxY = yPoints[0];
  for (int i = 1; i < numPoints; i++) {
    if (yPoints[i] < minY) minY = yPoints[i];
    if (yPoints[i] > maxY) maxY = yPoints[i];
  }

  // Clip to screen
  if (minY < 0) minY = 0;
  if (maxY >= getScreenHeight()) maxY = getScreenHeight() - 1;

  // Allocate node buffer for scanline algorithm
  auto* nodeX = static_cast<int*>(malloc(numPoints * sizeof(int)));
  if (!nodeX) {
    LOG_ERR("GFX", "!! Failed to allocate polygon node buffer");
    return;
  }

  // Scanline fill algorithm
  for (int scanY = minY; scanY <= maxY; scanY++) {
    int nodes = 0;

    // Find all intersection points with edges
    int j = numPoints - 1;
    for (int i = 0; i < numPoints; i++) {
      if ((yPoints[i] < scanY && yPoints[j] >= scanY) || (yPoints[j] < scanY && yPoints[i] >= scanY)) {
        // Calculate X intersection using fixed-point to avoid float
        int dy = yPoints[j] - yPoints[i];
        if (dy != 0) {
          nodeX[nodes++] = xPoints[i] + (scanY - yPoints[i]) * (xPoints[j] - xPoints[i]) / dy;
        }
      }
      j = i;
    }

    // Sort nodes by X (simple bubble sort, numPoints is small)
    for (int i = 0; i < nodes - 1; i++) {
      for (int k = i + 1; k < nodes; k++) {
        if (nodeX[i] > nodeX[k]) {
          int temp = nodeX[i];
          nodeX[i] = nodeX[k];
          nodeX[k] = temp;
        }
      }
    }

    // Fill between pairs of nodes
    for (int i = 0; i < nodes - 1; i += 2) {
      int startX = nodeX[i];
      int endX = nodeX[i + 1];

      // Clip to screen
      if (startX < 0) startX = 0;
      if (endX >= getScreenWidth()) endX = getScreenWidth() - 1;

      // Draw horizontal line
      for (int x = startX; x <= endX; x++) {
        drawPixel(x, scanY, state);
      }
    }
  }

  free(nodeX);
}

// For performance measurement (using static to allow "const" methods)
static unsigned long start_ms = 0;

void GfxRenderer::beginFrame() const { start_ms = millis(); }

void GfxRenderer::clearScreen(const uint8_t color) const {
  beginFrame();
  display.clearScreen(color);
}

void GfxRenderer::invertScreen() const {
  for (uint32_t i = 0; i < HalDisplay::BUFFER_SIZE; i++) {
    frameBuffer[i] = 3 - frameBuffer[i];
  }
}

bool GfxRenderer::displayGc16Bitmap(
    const Bitmap& bitmap,
    const bool clearFirst,
    const HalDisplay::Gc16DitherMode ditherMode,
    const bool rotate180
) const {
  return display.showGc16Bitmap(
      bitmap,
      clearFirst,
      ditherMode,
      rotate180
  );
}

void GfxRenderer::displayBuffer(const HalDisplay::RefreshMode refreshMode) const {
  auto elapsed = millis() - start_ms;
  // If a full refresh was requested (e.g. activity transition), override the mode
  auto mode = refreshMode;
  const bool forcedFull = forceNextFullRefresh;
  if (forceNextFullRefresh) {
    mode = HalDisplay::FULL_REFRESH;
    forceNextFullRefresh = false;
  }
  LOG_DBG(
      "GFX",
      "Time = %lu ms from clearScreen to displayBuffer mode=%d forcedFull=%d",
      elapsed,
      static_cast<int>(mode),
      forcedFull ? 1 : 0
  );
  display.displayBuffer(mode, fadingFix);
}

void GfxRenderer::displayPhysicalRows(int rowStart, int rowEnd) const {
  if (rowStart > rowEnd) return;
  rowStart = std::max(0, std::min<int>(HalDisplay::DISPLAY_HEIGHT - 1, rowStart));
  rowEnd = std::max(0, std::min<int>(HalDisplay::DISPLAY_HEIGHT - 1, rowEnd));
  if (rowStart > rowEnd) return;
  LOG_DBG("GFX", "Progressive rows %d..%d at %lu ms from clearScreen", rowStart, rowEnd, millis() - start_ms);
  display.displayBufferRows(rowStart, rowEnd, fadingFix);
}

void GfxRenderer::waitDisplayIdle() const {
  display.waitUntilIdle();
}

void GfxRenderer::logicalRectToPhysicalRows(
    const int x,
    const int y,
    const int width,
    const int height,
    int* rowStart,
    int* rowEnd
) const {
  if (!rowStart || !rowEnd) return;
  const int x0 = x;
  const int y0 = y;
  const int x1 = x + std::max(1, width) - 1;
  const int y1 = y + std::max(1, height) - 1;

  int px = 0;
  int py = 0;
  int minRow = HalDisplay::DISPLAY_HEIGHT - 1;
  int maxRow = 0;
  const int corners[4][2] = {{x0, y0}, {x1, y0}, {x0, y1}, {x1, y1}};
  for (const auto& c : corners) {
    rotateCoordinates(orientation, c[0], c[1], &px, &py);
    minRow = std::min(minRow, py);
    maxRow = std::max(maxRow, py);
  }

  *rowStart = std::max(0, std::min<int>(HalDisplay::DISPLAY_HEIGHT - 1, minRow));
  *rowEnd = std::max(0, std::min<int>(HalDisplay::DISPLAY_HEIGHT - 1, maxRow));
}

std::string GfxRenderer::truncatedText(const int fontId, const char* text, const int maxWidth,
                                       const EpdFontFamily::Style style) const {
  if (!text || maxWidth <= 0) return "";

  const std::string item = text;
  // U+2026 HORIZONTAL ELLIPSIS (UTF-8: 0xE2 0x80 0xA6)
  const char* ellipsis = "\xe2\x80\xa6";

  if (getTextWidth(fontId, item.c_str(), style) <= maxWidth) {
    return item;
  }

  // Record UTF-8 codepoint boundaries once, then binary-search the longest
  // prefix that fits. The old implementation removed one codepoint and
  // remeasured the entire string on every iteration (O(n^2)). With external
  // CJK fonts each measurement may touch the SD card, which made long Chinese
  // filenames especially slow.
  std::vector<size_t> boundaries;
  boundaries.reserve(item.size() / 2 + 1);
  boundaries.push_back(0);

  const auto* begin =
      reinterpret_cast<const unsigned char*>(item.c_str());
  const unsigned char* cursor = begin;

  while (*cursor != '\0') {
    utf8NextCodepoint(&cursor);
    boundaries.push_back(
        static_cast<size_t>(cursor - begin));
  }

  size_t low = 0;
  size_t high = boundaries.size() - 1;

  while (low < high) {
    const size_t mid = low + (high - low + 1) / 2;
    std::string candidate = item.substr(0, boundaries[mid]);
    candidate += ellipsis;

    // Preserve the previous behaviour: a candidate exactly equal to maxWidth
    // is shortened once more, so the returned text is strictly inside the
    // available width.
    if (getTextWidth(fontId, candidate.c_str(), style) < maxWidth) {
      low = mid;
    } else {
      high = mid - 1;
    }
  }

  if (low == 0) {
    return ellipsis;
  }

  return item.substr(0, boundaries[low]) + ellipsis;
}

std::string GfxRenderer::truncatedTextScaled(const int fontId, const char* text, const int maxWidth, const int scale,
                                             const EpdFontFamily::Style style) const {
  const int safeScale = std::max(1, scale);
  const int unscaledWidth = maxWidth > 0 ? std::max(1, maxWidth / safeScale) : 0;
  return truncatedText(fontId, text, unscaledWidth, style);
}

std::vector<std::string> GfxRenderer::wrappedText(const int fontId, const char* text, const int maxWidth,
                                                  const int maxLines, const EpdFontFamily::Style style) const {
  std::vector<std::string> lines;

  if (!text || maxWidth <= 0 || maxLines <= 0) return lines;

  std::string remaining = text;
  std::string currentLine;

  while (!remaining.empty()) {
    if (static_cast<int>(lines.size()) == maxLines - 1) {
      // Last available line: combine any word already started on this line with
      // the rest of the text, then let truncatedText fit it with an ellipsis.
      std::string lastContent = currentLine.empty() ? remaining : currentLine + " " + remaining;
      lines.push_back(truncatedText(fontId, lastContent.c_str(), maxWidth, style));
      return lines;
    }

    // Find next word
    size_t spacePos = remaining.find(' ');
    std::string word;

    if (spacePos == std::string::npos) {
      word = remaining;
      remaining.clear();
    } else {
      word = remaining.substr(0, spacePos);
      remaining.erase(0, spacePos + 1);
    }

    std::string testLine = currentLine.empty() ? word : currentLine + " " + word;

    if (getTextWidth(fontId, testLine.c_str(), style) <= maxWidth) {
      currentLine = testLine;
    } else {
      if (!currentLine.empty()) {
        lines.push_back(currentLine);
        // If the carried-over word itself exceeds maxWidth, truncate it and
        // push it as a complete line immediately — storing it in currentLine
        // would allow a subsequent short word to be appended after the ellipsis.
        if (getTextWidth(fontId, word.c_str(), style) > maxWidth) {
          lines.push_back(truncatedText(fontId, word.c_str(), maxWidth, style));
          currentLine.clear();
          if (static_cast<int>(lines.size()) >= maxLines) return lines;
        } else {
          currentLine = word;
        }
      } else {
        // Single word wider than maxWidth: truncate and stop to avoid complicated
        // splitting rules (different between languages). Results in an aesthetically
        // pleasing end.
        lines.push_back(truncatedText(fontId, word.c_str(), maxWidth, style));
        return lines;
      }
    }
  }

  if (!currentLine.empty() && static_cast<int>(lines.size()) < maxLines) {
    lines.push_back(currentLine);
  }

  return lines;
}

// Note: Internal driver treats screen in command orientation; this library exposes a logical orientation
int GfxRenderer::getScreenWidth() const {
  switch (orientation) {
    case Portrait:
    case PortraitInverted:
      // 540px wide in portrait logical coordinates
      return HalDisplay::DISPLAY_HEIGHT;
    case LandscapeClockwise:
    case LandscapeCounterClockwise:
      // 960px wide in landscape logical coordinates
      return HalDisplay::DISPLAY_WIDTH;
  }
  return HalDisplay::DISPLAY_HEIGHT;
}

int GfxRenderer::getScreenHeight() const {
  switch (orientation) {
    case Portrait:
    case PortraitInverted:
      // 960px tall in portrait logical coordinates
      return HalDisplay::DISPLAY_WIDTH;
    case LandscapeClockwise:
    case LandscapeCounterClockwise:
      // 540px tall in landscape logical coordinates
      return HalDisplay::DISPLAY_HEIGHT;
  }
  return HalDisplay::DISPLAY_WIDTH;
}

int GfxRenderer::getSpaceWidth(const int fontId, const EpdFontFamily::Style style) const {
  const auto fontIt = fontMap.find(fontId);
  if (fontIt == fontMap.end()) {
    LOG_ERR("GFX", "Font %d not found", fontId);
    return 0;
  }

  const EpdGlyph* spaceGlyph = fontIt->second.getGlyph(' ', style);
  return spaceGlyph ? fp4::toPixel(spaceGlyph->advanceX) : 0;  // snap 12.4 fixed-point to nearest pixel
}

int GfxRenderer::getSpaceAdvance(const int fontId, const uint32_t leftCp, const uint32_t rightCp,
                                 const EpdFontFamily::Style style) const {
  const auto fontIt = fontMap.find(fontId);
  if (fontIt == fontMap.end()) return 0;
  const auto& font = fontIt->second;
  const EpdGlyph* spaceGlyph = font.getGlyph(' ', style);
  const int32_t spaceAdvanceFP = spaceGlyph ? static_cast<int32_t>(spaceGlyph->advanceX) : 0;
  // Combine space advance + flanking kern into one fixed-point sum before snapping.
  // Snapping the combined value avoids the +/-1 px error from snapping each component separately.
  const int32_t kernFP = static_cast<int32_t>(font.getKerning(leftCp, ' ', style)) +
                         static_cast<int32_t>(font.getKerning(' ', rightCp, style));
  return fp4::toPixel(spaceAdvanceFP + kernFP);
}

int GfxRenderer::getKerning(const int fontId, const uint32_t leftCp, const uint32_t rightCp,
                            const EpdFontFamily::Style style) const {
  const auto fontIt = fontMap.find(fontId);
  if (fontIt == fontMap.end()) return 0;
  const int kernFP = fontIt->second.getKerning(leftCp, rightCp, style);  // 4.4 fixed-point
  return fp4::toPixel(kernFP);                                           // snap 4.4 fixed-point to nearest pixel
}

int GfxRenderer::getTextAdvanceX(
    const int fontId,
    const char* text,
    EpdFontFamily::Style style
) const {
  if (text == nullptr || *text == '\0') {
    return 0;
  }

  FontManager& fontManager = FontManager::getInstance();

  // EPUB 排版使用 getTextAdvanceX()，因此這裡也必須使用
  // 與 drawText() 相同的外部中文字型 metrics。
  if (isReaderFont(fontId) &&
      fontManager.isExternalFontEnabled() &&
      fontManager.getActiveFont() != nullptr) {
    return getTextWidthExternalReader(
        fontId,
        text,
        style
    );
  }

  // 以下保留原本程式
  const auto fontIt = fontMap.find(fontId);

  if (fontIt == fontMap.end()) {
    LOG_ERR("GFX", "Font %d not found", fontId);
    return 0;
  }

  return getTextWidthBuiltinFallback(fontId, fontIt->second, text, style);
}

int GfxRenderer::getFontAscenderSize(const int fontId) const {
  const EpdFontFamily* fallbackFont = getBuiltinFallbackForFontId(fontId);

   // UI 字型優先使用外部 Noto 字型的 ascender。
  FontManager& fontManager =
      FontManager::getInstance();

  if (shouldUseExternalUiFont(fontId) &&
    fontManager.isUiFontEnabled()) {

    ExternalFont* uiFont =
        fontManager.getActiveUiFont();

    if (uiFont != nullptr &&
        uiFont->isLoaded()) {
      const int externalAscender =
          getExternalFontAscenderForRendering(*uiFont);
      const EpdFontData* fallbackData =
          fallbackFont != nullptr
              ? fallbackFont->getData(EpdFontFamily::REGULAR)
              : nullptr;
      return fallbackData != nullptr
                 ? std::max(externalAscender, fallbackMetricPixels(fontId, static_cast<int>(fallbackData->ascender)))
                 : externalAscender;
    }
  }

  if (isReaderFont(fontId) && fontManager.isExternalFontEnabled()) {
    ExternalFont* readerFont = fontManager.getActiveFont();
    if (readerFont != nullptr && readerFont->isLoaded() && readerFont->isTtfFormat()) {
      const int externalAscender =
          getExternalFontAscenderForRendering(*readerFont);
      const EpdFontData* fallbackData =
          fallbackFont != nullptr
              ? fallbackFont->getData(EpdFontFamily::REGULAR)
              : nullptr;
      return fallbackData != nullptr
                 ? std::max(externalAscender, fallbackMetricPixels(fontId, static_cast<int>(fallbackData->ascender)))
                 : externalAscender;
    }
  }

  const auto fontIt = fontMap.find(fontId);
  if (fontIt == fontMap.end()) {
    LOG_ERR("GFX", "Font %d not found", fontId);
    return 0;
  }

  const int primaryAscender =
      fontIt->second.getData(EpdFontFamily::REGULAR)->ascender;
  const EpdFontData* fallbackData =
      fallbackFont != nullptr
          ? fallbackFont->getData(EpdFontFamily::REGULAR)
          : nullptr;
  return fallbackData != nullptr
             ? std::max(primaryAscender, fallbackMetricPixels(fontId, static_cast<int>(fallbackData->ascender)))
             : primaryAscender;
}

int GfxRenderer::getLineHeight(const int fontId) const {
  const EpdFontFamily* fallbackFont = getBuiltinFallbackForFontId(fontId);

   // UI 選單行距使用外部 UI 字型高度。
  FontManager& fontManager =
      FontManager::getInstance();

  if (shouldUseExternalUiFont(fontId) &&
    fontManager.isUiFontEnabled()) {

    ExternalFont* uiFont =
        fontManager.getActiveUiFont();

    if (uiFont != nullptr &&
        uiFont->isLoaded()) {
      const int externalHeight =
          getExternalFontLineHeightForRendering(*uiFont);
      const EpdFontData* fallbackData =
          fallbackFont != nullptr
              ? fallbackFont->getData(EpdFontFamily::REGULAR)
              : nullptr;
      return fallbackData != nullptr
                 ? std::max(externalHeight, fallbackMetricPixels(fontId, static_cast<int>(fallbackData->advanceY)))
                 : externalHeight;
    }
  }

  if (isReaderFont(fontId) && fontManager.isExternalFontEnabled()) {
    ExternalFont* readerFont = fontManager.getActiveFont();
    if (readerFont != nullptr && readerFont->isLoaded() && readerFont->isTtfFormat()) {
      const int externalHeight =
          getExternalFontLineHeightForRendering(*readerFont);
      const EpdFontData* fallbackData =
          fallbackFont != nullptr
              ? fallbackFont->getData(EpdFontFamily::REGULAR)
              : nullptr;
      return fallbackData != nullptr
                 ? std::max(externalHeight, fallbackMetricPixels(fontId, static_cast<int>(fallbackData->advanceY)))
                 : externalHeight;
    }
  }

  const auto fontIt = fontMap.find(fontId);
  if (fontIt == fontMap.end()) {
    LOG_ERR("GFX", "Font %d not found", fontId);
    return 0;
  }

  const int primaryHeight =
      fontIt->second.getData(EpdFontFamily::REGULAR)->advanceY;
  const EpdFontData* fallbackData =
      fallbackFont != nullptr
          ? fallbackFont->getData(EpdFontFamily::REGULAR)
          : nullptr;
  return fallbackData != nullptr
             ? std::max(primaryHeight, fallbackMetricPixels(fontId, static_cast<int>(fallbackData->advanceY)))
             : primaryHeight;
}

int GfxRenderer::getVerticalGlyphAdvance(const int fontId) const {
  const int lineHeight = std::max(1, getLineHeight(fontId));
  if (getBuiltinFallbackForFontId(fontId) == nullptr) return lineHeight;

  // Vertical CJK text should use visible-ink spacing rather than the full
  // logical cell height.  This makes character spacing 0px genuinely tight in
  // vertical mode while still leaving a small safety gap to avoid overlap.
  const int percent = fallbackScalePercentForFontId(fontId);
  const int logicalTargetHeight = std::max(1, scaleMetricPercent(BUILTIN_CJK_LOGICAL_CELL_HEIGHT, percent));
  const int tightAdvance = std::max(1, (logicalTargetHeight * 17 + 10) / 20);  // ~85%
  return std::min(lineHeight, tightAdvance);
}

int GfxRenderer::getLineHeightScaled(const int fontId, const int scale) const {
  return getLineHeight(fontId) * std::max(1, scale);
}

int GfxRenderer::getTextHeight(const int fontId) const {
  const EpdFontFamily* fallbackFont = getBuiltinFallbackForFontId(fontId);
  FontManager& fontManager =
      FontManager::getInstance();

  if (shouldUseExternalUiFont(fontId) &&
    fontManager.isUiFontEnabled()) {

    ExternalFont* uiFont =
        fontManager.getActiveUiFont();

    if (uiFont != nullptr &&
        uiFont->isLoaded()) {
      const int externalHeight = uiFont->getCharHeight();
      const EpdFontData* fallbackData =
          fallbackFont != nullptr
              ? fallbackFont->getData(EpdFontFamily::REGULAR)
              : nullptr;
      return fallbackData != nullptr
                 ? std::max(externalHeight, fallbackMetricPixels(fontId, static_cast<int>(fallbackData->ascender)))
                 : externalHeight;
    }
  }

  if (isReaderFont(fontId) && fontManager.isExternalFontEnabled()) {
    ExternalFont* readerFont = fontManager.getActiveFont();
    if (readerFont != nullptr && readerFont->isLoaded() && readerFont->isTtfFormat()) {
      const int externalHeight = readerFont->getCharHeight();
      const EpdFontData* fallbackData =
          fallbackFont != nullptr
              ? fallbackFont->getData(EpdFontFamily::REGULAR)
              : nullptr;
      return fallbackData != nullptr
                 ? std::max(externalHeight, fallbackMetricPixels(fontId, static_cast<int>(fallbackData->ascender)))
                 : externalHeight;
    }
  }

  const auto fontIt = fontMap.find(fontId);
  if (fontIt == fontMap.end()) {
    LOG_ERR("GFX", "Font %d not found", fontId);
    return 0;
  }
  const int primaryHeight =
      fontIt->second.getData(EpdFontFamily::REGULAR)->ascender;
  const EpdFontData* fallbackData =
      fallbackFont != nullptr
          ? fallbackFont->getData(EpdFontFamily::REGULAR)
          : nullptr;
  return fallbackData != nullptr
             ? std::max(primaryHeight, fallbackMetricPixels(fontId, static_cast<int>(fallbackData->ascender)))
             : primaryHeight;
}

void GfxRenderer::drawTextRotated90CW(const int fontId, const int x, const int y, const char* text, const bool black,
                                      const EpdFontFamily::Style style) const {
  // Cannot draw a NULL / empty string
  if (text == nullptr || *text == '\0') {
    return;
  }

  const auto fontIt = fontMap.find(fontId);
  if (fontIt == fontMap.end()) {
    LOG_ERR("GFX", "Font %d not found", fontId);
    return;
  }

  const auto& font = fontIt->second;
  const EpdFontFamily* fallbackFont = getBuiltinFallbackForFontId(fontId);

  int lastBaseY = y;
  int lastBaseLeft = 0;
  int lastBaseWidth = 0;
  int lastBaseTop = 0;
  int32_t prevAdvanceFP = 0;  // 12.4 fixed-point: prev glyph's advance + next kern for snap

  uint32_t cp;
  uint32_t prevCp = 0;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&text)))) {
    if (utf8IsCombiningMark(cp)) {
      const EpdGlyph* combiningGlyph = font.getGlyph(cp, style);
      if (!combiningGlyph) continue;
      const int raiseBy = combiningMark::raiseAboveBase(combiningGlyph->top, combiningGlyph->height, lastBaseTop);
      const int combiningX = x - raiseBy;
      const int combiningY = combiningMark::centerOverRotated90CW(lastBaseY, lastBaseLeft, lastBaseWidth,
                                                                  combiningGlyph->left, combiningGlyph->width);
      renderCharImpl<TextRotation::Rotated90CW>(*this, renderMode, font, cp, combiningX, combiningY, black, style);
      continue;
    }

    if (shouldUseBuiltinFallback(font, fallbackFont, cp, style)) {
      if (prevCp != 0) {
        lastBaseY -= fp4::toPixel(prevAdvanceFP);
      }
      prevCp = 0;
      prevAdvanceFP = 0;

      const EpdGlyph* fallbackGlyph =
          fallbackFont->getGlyphExact(cp, style);
      if (fallbackGlyph != nullptr) {
        if (usesCompactScaledCjkFallback(fontId)) {
          renderBuiltinFallbackGlyphScaledRotated90CW(
              *fallbackFont, cp, x, lastBaseY, black, style);
          lastBaseLeft = scaleCompactMetric(fallbackGlyph->left);
          lastBaseWidth = scaleCompactMetricCeil(fallbackGlyph->width);
          lastBaseTop = scaleCompactMetric(fallbackGlyph->top);
        } else {
          renderCharImpl<TextRotation::Rotated90CW>(
              *this, renderMode, *fallbackFont, cp,
              x, lastBaseY, black, style);
          lastBaseLeft = fallbackGlyph->left;
          lastBaseWidth = fallbackGlyph->width;
          lastBaseTop = fallbackGlyph->top;
        }
        lastBaseY -= fallbackAdvancePixels(fontId, fallbackGlyph, cp);
        continue;
      }
    }

    cp = font.applyLigatures(cp, text, style);

    // Differential rounding: snap (previous advance + current kern) as one unit,
    // subtracting for the rotated coordinate direction.
    if (prevCp != 0) {
      const auto kernFP = font.getKerning(prevCp, cp, style);  // 4.4 fixed-point kern
      lastBaseY -= fp4::toPixel(prevAdvanceFP + kernFP);       // snap 12.4 fixed-point to nearest pixel
    }

    const EpdGlyph* glyph = font.getGlyph(cp, style);

    lastBaseLeft = glyph ? glyph->left : 0;
    lastBaseWidth = glyph ? glyph->width : 0;
    lastBaseTop = glyph ? glyph->top : 0;
    prevAdvanceFP = glyphAdvanceFP(glyph, cp);  // 12.4 fixed-point

    renderCharImpl<TextRotation::Rotated90CW>(*this, renderMode, font, cp, x, lastBaseY, black, style);
    prevCp = cp;
  }
}

uint8_t* GfxRenderer::getFrameBuffer() const { return frameBuffer; }

size_t GfxRenderer::getBufferSize() { return HalDisplay::BUFFER_SIZE; }

// unused
// void GfxRenderer::grayscaleRevert() const { display.grayscaleRevert(); }

void GfxRenderer::copyGrayscaleLsbBuffers() const { display.copyGrayscaleLsbBuffers(frameBuffer); }

void GfxRenderer::copyGrayscaleMsbBuffers() const { display.copyGrayscaleMsbBuffers(frameBuffer); }

void GfxRenderer::displayGrayBuffer() const { display.displayGrayBuffer(fadingFix); }

bool GfxRenderer::storeBwBuffer() {
  if (bwBufferStored) {
    LOG_ERR("GFX", "!! BW buffer already stored - freeing old one");
    free(bwBufferStored);
    bwBufferStored = nullptr;
  }

  bwBufferStored = static_cast<uint8_t*>(heap_caps_malloc(HalDisplay::BUFFER_SIZE, MALLOC_CAP_SPIRAM));
  if (!bwBufferStored) {
    LOG_ERR("GFX", "!! Failed to allocate BW buffer backup (%u bytes)", HalDisplay::BUFFER_SIZE);
    return false;
  }

  memcpy(bwBufferStored, frameBuffer, HalDisplay::BUFFER_SIZE);
  LOG_DBG("GFX", "Stored BW buffer (%u bytes)", HalDisplay::BUFFER_SIZE);
  return true;
}

void GfxRenderer::restoreBwBuffer() {
  if (!bwBufferStored) return;

  memcpy(frameBuffer, bwBufferStored, HalDisplay::BUFFER_SIZE);
  display.cleanupGrayscaleBuffers(frameBuffer);

  free(bwBufferStored);
  bwBufferStored = nullptr;
  LOG_DBG("GFX", "Restored and freed BW buffer");
}

/**
 * Cleanup grayscale buffers using the current frame buffer.
 * Use this when BW buffer was re-rendered instead of stored/restored.
 */
void GfxRenderer::cleanupGrayscaleWithFrameBuffer() const {
  if (frameBuffer) {
    display.cleanupGrayscaleBuffers(frameBuffer);
  }
}

void GfxRenderer::getOrientedViewableTRBL(int* outTop, int* outRight, int* outBottom, int* outLeft) const {
  switch (orientation) {
    case Portrait:
      *outTop = VIEWABLE_MARGIN_TOP;
      *outRight = VIEWABLE_MARGIN_RIGHT;
      *outBottom = VIEWABLE_MARGIN_BOTTOM;
      *outLeft = VIEWABLE_MARGIN_LEFT;
      break;
    case LandscapeClockwise:
      *outTop = VIEWABLE_MARGIN_LEFT;
      *outRight = VIEWABLE_MARGIN_TOP;
      *outBottom = VIEWABLE_MARGIN_RIGHT;
      *outLeft = VIEWABLE_MARGIN_BOTTOM;
      break;
    case PortraitInverted:
      *outTop = VIEWABLE_MARGIN_BOTTOM;
      *outRight = VIEWABLE_MARGIN_LEFT;
      *outBottom = VIEWABLE_MARGIN_TOP;
      *outLeft = VIEWABLE_MARGIN_RIGHT;
      break;
    case LandscapeCounterClockwise:
      *outTop = VIEWABLE_MARGIN_RIGHT;
      *outRight = VIEWABLE_MARGIN_BOTTOM;
      *outBottom = VIEWABLE_MARGIN_LEFT;
      *outLeft = VIEWABLE_MARGIN_TOP;
      break;
  }
}
