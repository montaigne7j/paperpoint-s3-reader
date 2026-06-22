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

  char* lastUnderscore = std::strrchr(nameCopy, '_');
  if (lastUnderscore == nullptr) return false;
  int width = 0;
  int height = 0;
  char trailing = '\0';
  if (std::sscanf(lastUnderscore + 1, "%dx%d%c", &width, &height, &trailing) != 2 ||
      width <= 0 || width > 255 || height <= 0 || height > 255) {
    return false;
  }
  out.width = static_cast<uint8_t>(width);
  out.height = static_cast<uint8_t>(height);
  *lastUnderscore = '\0';

  lastUnderscore = std::strrchr(nameCopy, '_');
  if (lastUnderscore == nullptr) return false;
  int size = 0;
  trailing = '\0';
  if (std::sscanf(lastUnderscore + 1, "%d%c", &size, &trailing) != 1 || size <= 0 || size > 255) {
    return false;
  }
  out.size = static_cast<uint8_t>(size);
  *lastUnderscore = '\0';

  if (nameCopy[0] == '\0') return false;
  std::strncpy(out.name, nameCopy, sizeof(out.name) - 1);
  out.name[sizeof(out.name) - 1] = '\0';
  return true;
}
