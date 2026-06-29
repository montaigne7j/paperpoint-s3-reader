#pragma once

#include <HalStorage.h>

#include <cstddef>
#include <cstdint>

class OpenFontRender;
struct ExternalGlyphMetrics;

/**
 * Runtime TrueType rasterizer used only by the reader font slot.
 *
 * Glyphs are rendered on demand by OpenFontRender/FreeType, converted to the
 * same row-aligned 1-bpp bitmap format used by ExternalFont, and appended to a
 * persistent SD cache. ExternalFont supplies the PSRAM LRU layer above this
 * class, so normal page rendering normally avoids both FreeType and SD reads.
 */
class TtfFontEngine {
 public:
  TtfFontEngine();
  ~TtfFontEngine();

  TtfFontEngine(const TtfFontEngine&) = delete;
  TtfFontEngine& operator=(const TtfFontEngine&) = delete;

  bool load(const char* filepath, uint8_t pixelSize, uint8_t cellWidth, uint8_t cellHeight);
  void unload();
  bool isLoaded() const { return loaded_; }

  bool loadGlyph(uint32_t codepoint, uint8_t* bitmap, size_t bitmapCapacity, ExternalGlyphMetrics* metrics);
  // Load only from the persistent SD glyph cache.  This never calls
  // OpenFontRender/FreeType rasterize and is safe for background frame-cache
  // rendering.
  bool loadCachedGlyph(uint32_t codepoint, uint8_t* bitmap, size_t bitmapCapacity, ExternalGlyphMetrics* metrics);
  bool hasCachedGlyph(uint32_t codepoint) const;
  bool loadMetrics(uint32_t codepoint, ExternalGlyphMetrics* metrics);

  /** Flush pending persistent-cache writes. Normally called automatically. */
  void flushPersistentCache();

  const char* cachePath() const { return cachePath_; }

 private:
#pragma pack(push, 1)
  struct CacheHeader {
    char magic[4];
    uint16_t version;
    uint16_t pixelSize;
    uint8_t cellWidth;
    uint8_t cellHeight;
    uint16_t bytesPerGlyph;
    uint32_t sourceSize;
    uint32_t sourceFingerprint;
  };

  struct RecordHeader {
    uint32_t codepoint;
    uint8_t width;
    uint8_t height;
    uint16_t advanceX;
    uint16_t flags;
    int16_t left;
    int16_t top;
    uint16_t dataLength;
  };
#pragma pack(pop)

  // Reordered to remain naturally aligned while occupying 20 bytes rather
  // than 24 bytes on ESP32. This table lives in PSRAM.
  struct DiskIndexEntry {
    uint32_t codepoint = 0xFFFFFFFFu;
    uint32_t bitmapOffset = 0;
    uint16_t dataLength = 0;
    uint16_t advanceX = 0;
    uint16_t flags = 0;
    int16_t left = 0;
    int16_t top = 0;
    uint8_t width = 0;
    uint8_t height = 0;
  };

  static constexpr uint16_t CACHE_VERSION = 2;
  static constexpr size_t DISK_INDEX_CAPACITY = 16384;
  static constexpr uint16_t CACHE_FLUSH_INTERVAL = 16;
  static constexpr size_t INDEX_READ_BUFFER_SIZE = 16 * 1024;

  OpenFontRender* renderer_ = nullptr;
  bool loaded_ = false;
  uint8_t pixelSize_ = 0;
  uint8_t cellWidth_ = 0;
  uint8_t cellHeight_ = 0;
  uint16_t bytesPerGlyph_ = 0;
  uint32_t sourceSize_ = 0;
  uint32_t sourceFingerprint_ = 0;
  char fontPath_[128] = {};
  char cachePath_[128] = {};

  uint8_t* scratch_ = nullptr;
  DiskIndexEntry* diskIndex_ = nullptr;
  size_t diskIndexCount_ = 0;

  // Keep one append handle open. This avoids an SD open/flush/close cycle for
  // every newly encountered glyph. Writes are flushed in small batches and
  // before any newly-appended bitmap is read back.
  HalFile cacheAppendFile_;
  uint16_t pendingCacheWrites_ = 0;

  bool initializeCacheFile();
  bool openAppendCache();
  bool rebuildDiskIndex();
  bool findDiskEntry(uint32_t codepoint, DiskIndexEntry* out) const;
  bool insertDiskEntry(const DiskIndexEntry& entry);
  bool readDiskGlyph(const DiskIndexEntry& entry, uint8_t* bitmap, size_t bitmapCapacity,
                     ExternalGlyphMetrics* metrics);
  bool appendDiskGlyph(uint32_t codepoint, const uint8_t* bitmap, const ExternalGlyphMetrics& metrics,
                       uint16_t dataLength);
  bool rasterize(uint32_t codepoint, uint8_t* bitmap, size_t bitmapCapacity, ExternalGlyphMetrics* metrics);
  uint32_t computeSourceFingerprint(HalFile& source, const char* filepath) const;

  static uint32_t fnv1a(const char* text, uint32_t seed = 2166136261u);
  static uint32_t fnv1aBytes(const uint8_t* data, size_t length, uint32_t seed);
  static size_t encodeUtf8(uint32_t codepoint, char out[5]);
};
