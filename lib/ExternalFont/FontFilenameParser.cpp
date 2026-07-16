#include "FontFilenameParser.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace {
bool stripExtensionNoCase(char* filename, const char* extension) {
  if (filename == nullptr || extension == nullptr) return false;
  const size_t filenameLength = std::strlen(filename);
  const size_t extensionLength = std::strlen(extension);
  if (extensionLength > filenameLength) return false;

  char* tail = filename + filenameLength - extensionLength;
  for (size_t i = 0; i < extensionLength; ++i) {
    if (std::tolower(static_cast<unsigned char>(tail[i])) !=
        std::tolower(static_cast<unsigned char>(extension[i]))) {
      return false;
    }
  }
  *tail = '\0';
  return true;
}

bool isAsciiSeparator(const char c) {
  return c == '_' || c == '-' || c == ' ' || c == '.';
}

void trimTrailingSeparators(char* text) {
  if (text == nullptr) return;
  size_t len = std::strlen(text);
  while (len > 0 && isAsciiSeparator(text[len - 1])) {
    text[--len] = '\0';
  }
}

const char* previousUtf8Char(const char* begin, const char* p) {
  if (p <= begin) return begin;
  --p;
  while (p > begin && (static_cast<unsigned char>(*p) & 0xC0) == 0x80) --p;
  return p;
}

bool isUtf8MultiplySign(const char* p) {
  return p != nullptr &&
         static_cast<unsigned char>(p[0]) == 0xC3 &&
         static_cast<unsigned char>(p[1]) == 0x97;
}

bool parseTrailingNumber(const char* begin, char*& endExclusive, int& value) {
  if (begin == nullptr || endExclusive == nullptr || endExclusive <= begin) return false;
  char* p = endExclusive;
  while (p > begin && std::isdigit(static_cast<unsigned char>(*(p - 1)))) --p;
  if (p == endExclusive) return false;

  int v = 0;
  for (char* q = p; q < endExclusive; ++q) {
    v = v * 10 + (*q - '0');
    if (v > 999) return false;
  }
  value = v;
  endExclusive = p;
  return true;
}

bool consumeDimensionSeparator(const char* begin, char*& endExclusive) {
  if (begin == nullptr || endExclusive == nullptr || endExclusive <= begin) return false;
  char* p = const_cast<char*>(previousUtf8Char(begin, endExclusive));
  if (p < begin) return false;
  if (*p == 'x' || *p == 'X') {
    endExclusive = p;
    return true;
  }
  if (isUtf8MultiplySign(p) && p + 2 == endExclusive) {
    endExclusive = p;
    return true;
  }
  return false;
}

bool consumeOptionalNameSeparator(const char* begin, char*& endExclusive) {
  if (begin == nullptr || endExclusive == nullptr || endExclusive <= begin) return false;
  bool consumed = false;
  while (endExclusive > begin && isAsciiSeparator(*(endExclusive - 1))) {
    --endExclusive;
    consumed = true;
  }
  return consumed;
}
}  // namespace

bool parseFontFilename(const char* filepath, ParsedFontFilename& out) {
  out = ParsedFontFilename{};
  if (filepath == nullptr || *filepath == '\0') return false;

  const char* filename = std::strrchr(filepath, '/');
  filename = filename ? filename + 1 : filepath;

  char nameCopy[128];
  std::strncpy(nameCopy, filename, sizeof(nameCopy) - 1);
  nameCopy[sizeof(nameCopy) - 1] = '\0';

  if (stripExtensionNoCase(nameCopy, ".bin")) {
    out.isRichFormat = false;
  } else if (stripExtensionNoCase(nameCopy, ".epdf")) {
    out.isRichFormat = true;
  } else {
    return false;
  }

  const char* begin = nameCopy;
  char* cursor = nameCopy + std::strlen(nameCopy);
  trimTrailingSeparators(nameCopy);
  cursor = nameCopy + std::strlen(nameCopy);

  int width = 0;
  int height = 0;
  if (!parseTrailingNumber(begin, cursor, height)) return false;
  if (!consumeDimensionSeparator(begin, cursor)) return false;
  if (!parseTrailingNumber(begin, cursor, width)) return false;
  if (width <= 0 || width > 255 || height <= 0 || height > 255) return false;

  out.width = static_cast<uint8_t>(width);
  out.height = static_cast<uint8_t>(height);

  // Preferred form remains Name_size_WxH, but for user-made fonts also accept
  // Name_WxH, Name 23×33, Name-23x30, and CJK names with spaces/symbols.  If no
  // explicit size precedes the dimension token, use height as a practical size.
  char* nameEnd = cursor;
  consumeOptionalNameSeparator(begin, nameEnd);

  int size = height;
  char* sizeCursor = nameEnd;
  if (parseTrailingNumber(begin, sizeCursor, size)) {
    char* beforeSize = sizeCursor;
    if (consumeOptionalNameSeparator(begin, beforeSize) && beforeSize > begin) {
      nameEnd = beforeSize;
    } else {
      // Digits are likely part of the font name, not a separated size token.
      size = height;
    }
  }
  if (size <= 0 || size > 255) size = height;
  out.size = static_cast<uint8_t>(size);

  if (nameEnd <= begin) {
    // Last-resort display name.  Keep the original stem rather than rejecting a
    // valid bitmap just because the user named it "23x33.bin".
    nameEnd = cursor;
  }
  *nameEnd = '\0';
  trimTrailingSeparators(nameCopy);
  if (nameCopy[0] == '\0') {
    std::snprintf(nameCopy, sizeof(nameCopy), "%dx%d", width, height);
  }
  std::strncpy(out.name, nameCopy, sizeof(out.name) - 1);
  out.name[sizeof(out.name) - 1] = '\0';
  return true;
}
