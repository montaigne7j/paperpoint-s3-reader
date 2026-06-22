#include "ExternalFontHelpers.h"

#include <algorithm>

// --- Glyph layout ---

ExternalGlyphLayout computeExternalGlyphLayout(int cursorX, int baselineY, const ExternalFont& font,
                                               const ExternalGlyphMetrics& metrics, int advanceOverride) {
  ExternalGlyphLayout layout{};
  layout.baselineY = baselineY;
  layout.drawX = cursorX + metrics.left;
  layout.drawY = layout.baselineY - metrics.top;
  layout.advanceX = std::max(1, advanceOverride >= 0 ? advanceOverride : static_cast<int>(metrics.advanceX));
  layout.trimLeadingEmptyColumns = false;
  return layout;
}

// --- Advance helpers ---

int clampExternalAdvance(const int baseWidth, const int spacing) { return std::max(1, baseWidth + spacing); }

bool shouldUseCjkSymbolCellMetrics(const uint32_t codepoint) {
  if (codepoint == 0x00B7) return true;                         // Middle dot used in CJK names
  if (codepoint >= 0x2000 && codepoint <= 0x206F) return true;  // General punctuation
  if (codepoint >= 0x2150 && codepoint <= 0x218F) return true;  // Number forms
  if (codepoint >= 0x2460 && codepoint <= 0x24FF) return true;  // Enclosed alphanumerics
  if (codepoint >= 0x3000 && codepoint <= 0x303F) return true;  // CJK symbols and punctuation
  if (codepoint == 0x30A0 || codepoint == 0x30FB || codepoint == 0x30FC) return true;
  if (codepoint >= 0x3200 && codepoint <= 0x33FF) return true;  // Enclosed/compatibility CJK
  if (codepoint >= 0xFF00 && codepoint <= 0xFFEF) return true;  // Fullwidth forms
  return false;
}

bool shouldUseGlyphBoundsForAdvance(const uint32_t codepoint) {
  if (codepoint >= '0' && codepoint <= '9') return true;
  if ((codepoint >= 0x21 && codepoint <= 0x2F) ||
      (codepoint >= 0x3A && codepoint <= 0x40) ||
      (codepoint >= 0x5B && codepoint <= 0x60) ||
      (codepoint >= 0x7B && codepoint <= 0x7E)) {
    return true;
  }
  if (codepoint >= 0x2000 && codepoint <= 0x206F) return true;
  if (codepoint >= 0x3000 && codepoint <= 0x303F) return true;
  if (codepoint >= 0xFF00 && codepoint <= 0xFFEF) return true;
  return false;
}

namespace {

void setFallbacks(ExternalGlyphFallbacks& fallbacks, const uint32_t first, const uint32_t second = 0,
                  const uint32_t third = 0, const uint32_t fourth = 0) {
  fallbacks.codepoints[0] = first;
  fallbacks.count = 1;
  if (second != 0) {
    fallbacks.codepoints[fallbacks.count++] = second;
  }
  if (third != 0) {
    fallbacks.codepoints[fallbacks.count++] = third;
  }
  if (fourth != 0) {
    fallbacks.codepoints[fallbacks.count++] = fourth;
  }
}

}  // namespace

ExternalGlyphFallbacks getExternalGlyphFallbacks(const uint32_t codepoint) {
  ExternalGlyphFallbacks fallbacks{};
  switch (codepoint) {
    case 0x201C:
      setFallbacks(fallbacks, 0x300C, 0x300E, 0x0022);
      break;
    case 0x201D:
      setFallbacks(fallbacks, 0x300D, 0x300F, 0x0022);
      break;
    case 0x2018:
      setFallbacks(fallbacks, 0x300C, 0x300E, 0x0027);
      break;
    case 0x2019:
      setFallbacks(fallbacks, 0x300D, 0x300F, 0x0027);
      break;
    case 0x300C:
    case 0x300E:
      setFallbacks(fallbacks, 0x201C, 0x0022);
      break;
    case 0x300D:
    case 0x300F:
      setFallbacks(fallbacks, 0x201D, 0x0022);
      break;
    default:
      break;
  }
  return fallbacks;
}

int getExternalGlyphAdvanceForRendering(const uint16_t glyphAdvance, const uint8_t cellWidth, const int spacing,
                                        const bool forceCellAdvance) {
  const int baseWidth = forceCellAdvance ? std::max<int>(glyphAdvance, cellWidth) : glyphAdvance;
  return clampExternalAdvance(baseWidth, spacing);
}

int getExternalGlyphAdvanceForRendering(const ExternalGlyphMetrics& metrics, const uint8_t cellWidth, const int spacing,
                                        const bool forceCellAdvance, const bool useGlyphBounds) {
  int baseWidth = metrics.advanceX;
  if (forceCellAdvance) {
    baseWidth = std::max<int>(baseWidth, cellWidth);
  }
  if (useGlyphBounds) {
    // Guard the next glyph from starting inside visible ink when a font reports
    // a narrow advance for digits/punctuation/ellipsis.  The +1 px mirrors the
    // built-in EpdFont protection and avoids just-touching black pixels on EPD.
    baseWidth = std::max<int>(baseWidth, metrics.left + metrics.width + 1);
  }
  return clampExternalAdvance(baseWidth, spacing);
}

int getExternalGlyphAdvanceForRendering(const ExternalFont& font, uint32_t codepoint, int spacing) {
  ExternalGlyphMetrics metrics{};
  metrics.width = font.getCharWidth();
  metrics.height = font.getCharHeight();
  metrics.advanceX = font.getCharWidth();

  if (font.getGlyphMetrics(codepoint, &metrics)) {
    return getExternalGlyphAdvanceForRendering(metrics, font.getCharWidth(), spacing,
                                               shouldUseCjkSymbolCellMetrics(codepoint),
                                               shouldUseGlyphBoundsForAdvance(codepoint));
  }

  return clampExternalAdvance(font.getCharWidth(), spacing);
}

void normalizeCjkSymbolMetrics(ExternalGlyphMetrics& metrics, const uint8_t cellWidth) {
  if (metrics.left < 0) {
    metrics.left = 0;
  } else if (metrics.left >= cellWidth) {
    metrics.left = 0;
  }

  if (metrics.advanceX < cellWidth) {
    metrics.advanceX = cellWidth;
  }
}

void normalizeCjkSymbolMetricsForRendering(ExternalGlyphMetrics& metrics, const uint8_t cellWidth,
                                           const bool richMetricsFormat) {
  if (!richMetricsFormat) {
    metrics.left = 0;
  }
  normalizeCjkSymbolMetrics(metrics, cellWidth);
}

// --- Font-level render metrics ---

int getExternalFontAscenderForRendering(const ExternalFont& font) {
  if (font.isRichMetricsFormat() && font.getAscender() > 0) {
    return font.getAscender();
  }
  return font.getCharHeight();
}

int getExternalFontLineHeightForRendering(const ExternalFont& font) {
  if (font.isRichMetricsFormat() && font.getLineHeight() > 0) {
    return font.getLineHeight();
  }
  return font.getCharHeight();
}
