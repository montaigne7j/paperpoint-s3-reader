#include "FontManager.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "../../src/CrossPointSettings.h"
#include "FontFilenameParser.h"

// Out-of-class definitions for static constexpr members (required for ODR-use
// in C++14)
constexpr int FontManager::MAX_FONTS;
constexpr const char* FontManager::FONTS_DIR;
constexpr const char* FontManager::SETTINGS_FILE;
constexpr uint8_t FontManager::SETTINGS_VERSION;

namespace {
bool endsWithNoCase(const char* text, const char* suffix) {
  if (text == nullptr || suffix == nullptr) return false;
  const size_t textLength = std::strlen(text);
  const size_t suffixLength = std::strlen(suffix);
  if (suffixLength > textLength) return false;
  const char* start = text + textLength - suffixLength;
  for (size_t i = 0; i < suffixLength; ++i) {
    if (std::tolower(static_cast<unsigned char>(start[i])) !=
        std::tolower(static_cast<unsigned char>(suffix[i]))) {
      return false;
    }
  }
  return true;
}

void deriveTtfDisplayName(const char* filename, char* out, const size_t outSize) {
  if (out == nullptr || outSize == 0) return;
  out[0] = '\0';
  if (filename == nullptr) return;
  std::strncpy(out, filename, outSize - 1);
  out[outSize - 1] = '\0';
  char* dot = std::strrchr(out, '.');
  if (dot != nullptr) *dot = '\0';
  for (char* p = out; *p != '\0'; ++p) {
    if (*p == '_' || *p == '-') *p = ' ';
  }
}

const char* fontTypeLabel(const FontFileType type) {
  switch (type) {
    case FontFileType::TrueType:
      return "TTF";
    case FontFileType::EpdFont:
      return "EPDF";
    case FontFileType::BitmapBin:
    default:
      return "BIN";
  }
}
}  // namespace

FontManager& FontManager::getInstance() {
  static FontManager instance;
  return instance;
}

uint8_t FontManager::getTtfPixelSize() {
  return std::min<uint8_t>(CrossPointSettings::READER_FONT_SIZE_MAX,
                           std::max<uint8_t>(CrossPointSettings::READER_FONT_SIZE_MIN,
                                             SETTINGS.fontSize));
}

void FontManager::scanFonts() {
  // Directory iteration order is not stable across SD cards or after files are
  // copied. Preserve the active choices by filename, then remap their indices
  // after the refreshed list is sorted.
  char selectedReaderFilename[sizeof(_fonts[0].filename)] = {};
  char selectedUiFilename[sizeof(_fonts[0].filename)] = {};
  const bool hadReaderSelection = _selectedIndex >= 0 && _selectedIndex < _fontCount;
  const bool hadUiSelection = _selectedUiIndex >= 0 && _selectedUiIndex < _fontCount;
  if (hadReaderSelection) {
    std::strncpy(selectedReaderFilename, _fonts[_selectedIndex].filename,
                 sizeof(selectedReaderFilename) - 1);
  }
  if (hadUiSelection) {
    std::strncpy(selectedUiFilename, _fonts[_selectedUiIndex].filename,
                 sizeof(selectedUiFilename) - 1);
  }

  HalFile dir = Storage.open(FONTS_DIR, O_RDONLY);
  if (!dir) {
    LOG_ERR("FONT_MGR", "Cannot open fonts directory: %s", FONTS_DIR);
    return;
  }

  if (!dir.isDirectory()) {
    LOG_ERR("FONT_MGR", "%s is not a directory", FONTS_DIR);
    dir.close();
    return;
  }

  _fontCount = 0;
  HalFile entry;
  while (_fontCount < MAX_FONTS && (entry = dir.openNextFile())) {
    if (entry.isDirectory()) {
      entry.close();
      continue;
    }

    char filename[128];
    entry.getName(filename, sizeof(filename));
    entry.close();

    FontInfo& info = _fonts[_fontCount];
    info = FontInfo{};
    std::strncpy(info.filename, filename, sizeof(info.filename) - 1);
    info.filename[sizeof(info.filename) - 1] = '\0';

    if (endsWithNoCase(filename, ".ttf")) {
      const uint8_t pixelSize = getTtfPixelSize();
      deriveTtfDisplayName(filename, info.name, sizeof(info.name));
      info.size = pixelSize;
      info.width = pixelSize;
      info.height = static_cast<uint8_t>(pixelSize + 6);
      info.type = FontFileType::TrueType;
    } else {
      ParsedFontFilename parsed;
      if (!parseFontFilename(filename, parsed)) {
        continue;
      }
      std::strncpy(info.name, parsed.name, sizeof(info.name) - 1);
      info.name[sizeof(info.name) - 1] = '\0';
      info.size = parsed.size;
      info.width = parsed.width;
      info.height = parsed.height;
      info.type = endsWithNoCase(filename, ".epdf") ? FontFileType::EpdFont : FontFileType::BitmapBin;
    }

    LOG_DBG("FONT_MGR", "Found %s font: %s (%d, %dx%d)", fontTypeLabel(info.type), info.name, info.size,
            info.width, info.height);
    ++_fontCount;
  }

  dir.close();

  std::sort(_fonts, _fonts + _fontCount, [](const FontInfo& lhs, const FontInfo& rhs) {
    return std::strcmp(lhs.filename, rhs.filename) < 0;
  });

  auto findFilename = [this](const char* filename) -> int {
    if (filename == nullptr || filename[0] == '\0') return -1;
    for (int i = 0; i < _fontCount; ++i) {
      if (std::strcmp(_fonts[i].filename, filename) == 0) return i;
    }
    return -1;
  };

  if (hadReaderSelection) {
    _selectedIndex = findFilename(selectedReaderFilename);
    if (_selectedIndex < 0) {
      LOG_ERR("FONT_MGR", "Selected reader font disappeared: %s", selectedReaderFilename);
      _activeFont.unload();
    }
  }
  if (hadUiSelection) {
    _selectedUiIndex = findFilename(selectedUiFilename);
    if (_selectedUiIndex < 0) {
      LOG_ERR("FONT_MGR", "Selected UI font disappeared: %s", selectedUiFilename);
      _activeUiFont.unload();
    }
  }

  LOG_INF("FONT_MGR", "Scan complete: %d reader fonts found", _fontCount);
}

const FontInfo* FontManager::getFontInfo(int index) const {
  if (index < 0 || index >= _fontCount) return nullptr;
  return &_fonts[index];
}

bool FontManager::loadSelectedFont() {
  _activeFont.unload();

  if (_selectedIndex < 0 || _selectedIndex >= _fontCount) return false;

  const FontInfo& info = _fonts[_selectedIndex];
  char filepath[192];
  std::snprintf(filepath, sizeof(filepath), "%s/%s", FONTS_DIR, info.filename);

  const uint8_t ttfPixelSize = info.type == FontFileType::TrueType ? getTtfPixelSize() : 0;
  const bool loaded = _activeFont.load(filepath, ttfPixelSize);
  if (isUiSharingReaderFont()) _activeUiFont.unload();
  return loaded;
}

bool FontManager::loadSelectedUiFont() {
  if (_selectedUiIndex < 0 || _selectedUiIndex >= _fontCount) {
    _activeUiFont.unload();
    return false;
  }

  // Phase 1 deliberately keeps the UI on bitmap fonts. Runtime TTF is only
  // permitted in the reader slot so File Browser and settings remain fast.
  if (_fonts[_selectedUiIndex].type == FontFileType::TrueType) {
    LOG_ERR("FONT_MGR", "TTF cannot be selected as UI font in phase 1: %s", _fonts[_selectedUiIndex].filename);
    _activeUiFont.unload();
    return false;
  }

  if (isUiSharingReaderFont()) {
    _activeUiFont.unload();
    if (!_activeFont.isLoaded()) return loadSelectedFont();
    return true;
  }

  _activeUiFont.unload();
  char filepath[192];
  std::snprintf(filepath, sizeof(filepath), "%s/%s", FONTS_DIR, _fonts[_selectedUiIndex].filename);
  return _activeUiFont.load(filepath);
}

void FontManager::selectFont(int index) {
  if (index < -1 || index >= _fontCount) return;
  if (index == _selectedIndex && (index < 0 || _activeFont.isLoaded())) return;

  _selectedIndex = index;
  bool loaded = true;
  if (index >= 0) {
    loaded = loadSelectedFont();
  } else {
    _activeFont.unload();
  }

  if (!loaded) {
    LOG_ERR("FONT_MGR", "Reader font selection failed; using built-in font");
    _selectedIndex = -1;
    _activeFont.unload();
  }

  saveSettings();
  invalidateReaderLayoutCaches();
}

bool FontManager::reloadReaderFontForSize() {
  if (_selectedIndex < 0 || _selectedIndex >= _fontCount ||
      _fonts[_selectedIndex].type != FontFileType::TrueType) {
    return false;
  }

  const uint8_t pixelSize = getTtfPixelSize();
  _fonts[_selectedIndex].size = pixelSize;
  _fonts[_selectedIndex].width = pixelSize;
  _fonts[_selectedIndex].height = static_cast<uint8_t>(pixelSize + 6);

  const bool loaded = loadSelectedFont();
  if (loaded) {
    invalidateReaderLayoutCaches();
    LOG_INF("FONT_MGR", "Reloaded TTF reader font at %upx", pixelSize);
  }
  return loaded;
}

void FontManager::invalidateReaderLayoutCaches() {
  HalFile root = Storage.open("/.crosspoint", O_RDONLY);
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return;
  }

  char name[128];
  int removed = 0;
  for (HalFile entry = root.openNextFile(); entry; entry = root.openNextFile()) {
    entry.getName(name, sizeof(name));
    const bool bookCache = entry.isDirectory() &&
                           (std::strncmp(name, "epub_", 5) == 0 || std::strncmp(name, "xtc_", 4) == 0);
    entry.close();
    if (!bookCache) continue;

    char sectionsPath[176];
    std::snprintf(sectionsPath, sizeof(sectionsPath), "/.crosspoint/%s/sections", name);
    if (Storage.exists(sectionsPath) && Storage.removeDir(sectionsPath)) ++removed;
  }
  root.close();
  LOG_INF("FONT_MGR", "Invalidated reader section caches: %d", removed);
}

void FontManager::selectUiFont(int index) {
  if (index == _selectedUiIndex) return;
  if (index >= 0 && index < _fontCount && _fonts[index].type == FontFileType::TrueType) {
    LOG_ERR("FONT_MGR", "Ignoring TTF UI font selection");
    return;
  }

  _selectedUiIndex = index;
  if (index >= 0) loadSelectedUiFont(); else _activeUiFont.unload();
  saveSettings();
}

bool FontManager::previewFont(int index) {
  if (index < -1 || index >= _fontCount) return false;
  _selectedIndex = index;
  if (index >= 0) return loadSelectedFont();
  _activeFont.unload();
  return true;
}

bool FontManager::previewUiFont(int index) {
  if (index < -1 || index >= _fontCount) return false;
  if (index >= 0 && _fonts[index].type == FontFileType::TrueType) return false;
  _selectedUiIndex = index;
  if (index >= 0) return loadSelectedUiFont();
  _activeUiFont.unload();
  return true;
}

void FontManager::restoreFontSelection(int readerIndex, int uiIndex) {
  _selectedIndex = readerIndex;
  _selectedUiIndex = uiIndex;
  if (_selectedIndex >= 0) loadSelectedFont(); else _activeFont.unload();
  if (_selectedUiIndex >= 0) loadSelectedUiFont(); else _activeUiFont.unload();
}

ExternalFont* FontManager::getActiveFont() {
  return (_selectedIndex >= 0 && _activeFont.isLoaded()) ? &_activeFont : nullptr;
}

ExternalFont* FontManager::getActiveUiFont() {
  if (_selectedUiIndex < 0) return nullptr;
  if (isUiSharingReaderFont()) return _activeFont.isLoaded() ? &_activeFont : nullptr;
  return _activeUiFont.isLoaded() ? &_activeUiFont : nullptr;
}

void FontManager::releaseGlyphCaches() {
  _activeFont.releaseGlyphCache();
  if (!isUiSharingReaderFont()) _activeUiFont.releaseGlyphCache();
}

void FontManager::flushPersistentCaches() {
  _activeFont.flushPersistentCache();
  if (!isUiSharingReaderFont()) _activeUiFont.flushPersistentCache();
}

void FontManager::setGlyphCachesSuspended(bool suspended) {
  _glyphCachesSuspended = suspended;
  if (suspended) _activeFont.releaseGlyphCache();
}

bool FontManager::isGlyphCacheSuspendedFor(const ExternalFont* font) const {
  if (!_glyphCachesSuspended || !font) return false;
  if (isUiSharingReaderFont()) return false;
  return font == &_activeFont;
}

FontManager::ScopedGlyphCacheSuspension::ScopedGlyphCacheSuspension(FontManager& manager)
    : _manager(manager), _previous(manager.areGlyphCachesSuspended()) {
  _manager.setGlyphCachesSuspended(true);
}

FontManager::ScopedGlyphCacheSuspension::~ScopedGlyphCacheSuspension() {
  _manager.setGlyphCachesSuspended(_previous);
}

void FontManager::writeFontChoice(HalFile& file, const int index) const {
  serialization::writePod(file, index);
  serialization::writeString(file, index >= 0 && index < _fontCount ? std::string(_fonts[index].filename)
                                                                    : std::string(""));
}

void FontManager::readFontChoice(HalFile& file, const char* label, int& outIndex,
                                 bool (FontManager::*loader)()) {
  int savedIndex = -1;
  serialization::readPod(file, savedIndex);
  std::string savedFilename;
  serialization::readString(file, savedFilename);

  if (savedIndex < 0 || savedFilename.empty()) {
    outIndex = -1;
    return;
  }

  for (int i = 0; i < _fontCount; ++i) {
    if (savedFilename == _fonts[i].filename) {
      outIndex = i;
      if ((this->*loader)()) {
        LOG_INF("FONT_MGR", "Restored %s font: %s", label, savedFilename.c_str());
      } else {
        LOG_ERR("FONT_MGR", "Failed to restore %s font: %s", label, savedFilename.c_str());
        outIndex = -1;
      }
      return;
    }
  }
  outIndex = -1;
  LOG_ERR("FONT_MGR", "Saved %s font not found: %s", label, savedFilename.c_str());
}

void FontManager::saveSettings() {
  Storage.mkdir("/.crosspoint");
  HalFile file;
  if (!Storage.openFileForWrite("FONT_MGR", SETTINGS_FILE, file)) {
    LOG_ERR("FONT_MGR", "Failed to save settings");
    return;
  }
  serialization::writePod(file, SETTINGS_VERSION);
  writeFontChoice(file, _selectedIndex);
  writeFontChoice(file, _selectedUiIndex);
  file.close();
  _settingsLoaded = true;
  LOG_DBG("FONT_MGR", "Settings saved");
}

void FontManager::loadSettings() {
  _settingsLoaded = false;
  HalFile file;
  if (!Storage.openFileForRead("FONT_MGR", SETTINGS_FILE, file)) {
    LOG_DBG("FONT_MGR", "No settings file, using defaults");
    return;
  }

  uint8_t version = 0;
  serialization::readPod(file, version);
  if (version < 1 || version > SETTINGS_VERSION) {
    LOG_ERR("FONT_MGR", "Settings version mismatch (%d vs %d)", version, SETTINGS_VERSION);
    file.close();
    return;
  }

  _settingsLoaded = true;
  readFontChoice(file, "reader", _selectedIndex, &FontManager::loadSelectedFont);
  if (version >= 2) readFontChoice(file, "UI", _selectedUiIndex, &FontManager::loadSelectedUiFont);
  file.close();
}
