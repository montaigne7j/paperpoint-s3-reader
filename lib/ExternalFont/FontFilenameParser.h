#pragma once

#include <cstdint>

// Result of parsing a font filename in the form
//   FontName_size_WxH.bin    (legacy Xteink fixed-grid format)
//   FontName_size_WxH.epdf   (rich-metrics EPDFont — produced by the
//                             x4-epdfont-converter)
struct ParsedFontFilename {
  char name[32] = {0};
  uint8_t size = 0;
  uint8_t width = 0;
  uint8_t height = 0;
  bool isRichFormat = false;  // true for .epdf, false for .bin
};

// Parses the filename component of `filepath` (anything before the trailing
// '/' is ignored). Returns false on malformed input.
bool parseFontFilename(const char* filepath, ParsedFontFilename& out);
