#pragma once

#include <HalStorage.h>

#include <algorithm>
#include <array>
#include <deque>
#include <string>
#include <vector>

class BookMetadataCache {
 public:
  struct BookMetadata {
    std::string title;
    std::string author;
    std::string language;
    std::string coverItemHref;
    std::string textReferenceHref;
  };

  struct SpineEntry {
    std::string href;
    size_t cumulativeSize;
    int16_t tocIndex;

    SpineEntry() : cumulativeSize(0), tocIndex(-1) {}
    SpineEntry(std::string href, const size_t cumulativeSize, const int16_t tocIndex)
        : href(std::move(href)), cumulativeSize(cumulativeSize), tocIndex(tocIndex) {}
  };

  struct TocNodeInfo {
    uint8_t level = 0;
    int16_t spineIndex = -1;
  };

  struct TocEntry {
    std::string title;
    std::string href;
    std::string anchor;
    uint8_t level;
    int16_t spineIndex;

    TocEntry() : level(0), spineIndex(-1) {}
    TocEntry(std::string title, std::string href, std::string anchor, const uint8_t level, const int16_t spineIndex)
        : title(std::move(title)),
          href(std::move(href)),
          anchor(std::move(anchor)),
          level(level),
          spineIndex(spineIndex) {}
  };

 private:
  std::string cachePath;
  size_t lutOffset;
  uint16_t spineCount;
  uint16_t tocCount;
  bool loaded;
  bool buildMode;

  FsFile bookFile;
  // Temp file handles during build
  FsFile spineFile;
  FsFile tocFile;

  // Index for fast href→spineIndex lookup (used only for large EPUBs)
  struct SpineHrefIndexEntry {
    uint64_t hrefHash;  // FNV-1a 64-bit hash
    uint16_t hrefLen;   // length for collision reduction
    int16_t spineIndex;
  };
  std::deque<SpineHrefIndexEntry> spineHrefIndex;
  bool useSpineHrefIndex = false;

  static constexpr uint16_t LARGE_SPINE_THRESHOLD = 400;
  static constexpr size_t TEMP_WRITE_BUFFER_SIZE = 4096;

  // Parser callbacks can create thousands of entries. Buffering their small
  // writes avoids taking the storage mutex and touching the SD card for every
  // individual string length, string body, and POD field.
  std::array<uint8_t, TEMP_WRITE_BUFFER_SIZE> spineWriteBuffer{};
  std::array<uint8_t, TEMP_WRITE_BUFFER_SIZE> tocWriteBuffer{};
  size_t spineWriteUsed = 0;
  size_t tocWriteUsed = 0;
  bool tempWriteFailed = false;

  bool appendTempBytes(FsFile& file, std::array<uint8_t, TEMP_WRITE_BUFFER_SIZE>& buffer, size_t& used,
                       const void* data, size_t length);
  bool flushTempBuffer(FsFile& file, std::array<uint8_t, TEMP_WRITE_BUFFER_SIZE>& buffer, size_t& used);
  bool writeTempString(FsFile& file, std::array<uint8_t, TEMP_WRITE_BUFFER_SIZE>& buffer, size_t& used,
                       const std::string& value);
  bool writeTempSpineEntry(const SpineEntry& entry);
  bool writeTempTocEntry(const TocEntry& entry);

  // FNV-1a 64-bit hash function
  static uint64_t fnvHash64(const std::string& s) {
    uint64_t hash = 14695981039346656037ull;
    for (char c : s) {
      hash ^= static_cast<uint8_t>(c);
      hash *= 1099511628211ull;
    }
    return hash;
  }

  uint32_t writeSpineEntry(FsFile& file, const SpineEntry& entry) const;
  uint32_t writeTocEntry(FsFile& file, const TocEntry& entry) const;
  SpineEntry readSpineEntry(FsFile& file) const;
  TocEntry readTocEntry(FsFile& file) const;

 public:
  BookMetadata coreMetadata;

  explicit BookMetadataCache(std::string cachePath)
      : cachePath(std::move(cachePath)), lutOffset(0), spineCount(0), tocCount(0), loaded(false), buildMode(false) {}
  ~BookMetadataCache() = default;

  // Building phase (stream to disk immediately)
  bool beginWrite();
  bool beginContentOpfPass();
  void createSpineEntry(const std::string& href);
  bool endContentOpfPass();
  bool beginTocPass();
  void createTocEntry(const std::string& title, const std::string& href, const std::string& anchor, uint8_t level);
  bool endTocPass();
  bool endWrite();
  bool cleanupTmpFiles() const;

  // Post-processing to update mappings and sizes
  bool buildBookBin(const std::string& epubPath, const BookMetadata& metadata);

  // Reading phase (read mode)
  bool load();
  SpineEntry getSpineEntry(int index);
  TocEntry getTocEntry(int index);
  // Read only the fixed-size tree metadata for every TOC entry in one
  // sequential pass. This avoids two SD seeks and three string allocations per
  // entry when opening the hierarchical chapter selector.
  bool getTocNodeInfos(std::vector<TocNodeInfo>& nodes);
  int getSpineCount() const { return spineCount; }
  int getTocCount() const { return tocCount; }
  bool isLoaded() const { return loaded; }
};
