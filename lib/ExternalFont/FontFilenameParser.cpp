#include "FontFilenameParser.h"

#include <cstdio>
#include <cstring>

bool parseFontFilename(const char* filepath, ParsedFontFilename& out) {
  if (filepath == nullptr || *filepath == '\0') {
    return false;
  }

  // Extract filename from path.
  const char* filename = strrchr(filepath, '/');
  filename = filename ? filename + 1 : filepath;

  // Working copy so we can null-terminate fields in place.
  char nameCopy[64];
  strncpy(nameCopy, filename, sizeof(nameCopy) - 1);
  nameCopy[sizeof(nameCopy) - 1] = '\0';

  // Detect supported extension.
  char* ext = strstr(nameCopy, ".bin");
  if (ext) {
    out.isRichFormat = false;
  } else {
    ext = strstr(nameCopy, ".epdf");
    if (!ext) {
      return false;
    }
    out.isRichFormat = true;
  }
  *ext = '\0';

  // Parse trailing _WxH.
  char* lastUnderscore = strrchr(nameCopy, '_');
  if (!lastUnderscore) {
    return false;
  }
  int w = 0;
  int h = 0;
  if (sscanf(lastUnderscore + 1, "%dx%d", &w, &h) != 2) {
    return false;
  }
  out.width = static_cast<uint8_t>(w);
  out.height = static_cast<uint8_t>(h);
  *lastUnderscore = '\0';

  // Parse trailing _size.
  lastUnderscore = strrchr(nameCopy, '_');
  if (!lastUnderscore) {
    return false;
  }
  int size = 0;
  if (sscanf(lastUnderscore + 1, "%d", &size) != 1) {
    return false;
  }
  out.size = static_cast<uint8_t>(size);
  *lastUnderscore = '\0';

  // Remaining prefix is the font name.
  strncpy(out.name, nameCopy, sizeof(out.name) - 1);
  out.name[sizeof(out.name) - 1] = '\0';
  return true;
}
