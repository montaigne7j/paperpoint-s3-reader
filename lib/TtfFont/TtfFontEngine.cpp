#include "TtfFontEngine.h"

#include <ExternalFont.h>
#include <FreeTypePsramAllocator.h>
#include <Logging.h>
#include <OpenFontRender.h>
#include <ReaderMemoryDiagnostics.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>

namespace {
constexpr char CACHE_MAGIC[4] = {'T', 'T', 'F', 'C'};
constexpr const char* CACHE_DIR = "/.crosspoint/fontcache";
constexpr size_t FINGERPRINT_SAMPLE_SIZE = 512;

uint16_t brightness565(const uint16_t color) {
  const uint16_t r = (color >> 11) & 0x1F;
  const uint16_t g = (color >> 5) & 0x3F;
  const uint16_t b = color & 0x1F;
  return static_cast<uint16_t>(r * 2 + g + b * 2);  // max 187
}
}  // namespace

TtfFontEngine::TtfFontEngine() = default;
TtfFontEngine::~TtfFontEngine() { unload(); }

uint32_t TtfFontEngine::fnv1a(const char* text, const uint32_t seed) {
  if (text == nullptr) return seed;
  uint32_t hash = seed;
  while (*text != '\0') {
    hash ^= static_cast<uint8_t>(*text++);
    hash *= 16777619u;
  }
  return hash;
}

uint32_t TtfFontEngine::fnv1aBytes(const uint8_t* data, const size_t length, const uint32_t seed) {
  if (data == nullptr || length == 0) return seed;
  uint32_t hash = seed;
  for (size_t i = 0; i < length; ++i) {
    hash ^= data[i];
    hash *= 16777619u;
  }
  return hash;
}

size_t TtfFontEngine::encodeUtf8(const uint32_t cp, char out[5]) {
  if (cp <= 0x7F) {
    out[0] = static_cast<char>(cp);
    out[1] = '\0';
    return 1;
  }
  if (cp <= 0x7FF) {
    out[0] = static_cast<char>(0xC0 | (cp >> 6));
    out[1] = static_cast<char>(0x80 | (cp & 0x3F));
    out[2] = '\0';
    return 2;
  }
  if (cp <= 0xFFFF) {
    out[0] = static_cast<char>(0xE0 | (cp >> 12));
    out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[2] = static_cast<char>(0x80 | (cp & 0x3F));
    out[3] = '\0';
    return 3;
  }
  out[0] = '\0';
  return 0;  // OpenFontRender 1.2 decodes BMP codepoints as uint16_t.
}

uint32_t TtfFontEngine::computeSourceFingerprint(HalFile& source, const char* filepath) const {
  uint32_t hash = fnv1a(filepath);

  const uint32_t metadata[] = {
      sourceSize_,
      static_cast<uint32_t>(pixelSize_),
      static_cast<uint32_t>(cellWidth_),
      static_cast<uint32_t>(cellHeight_),
      static_cast<uint32_t>(bytesPerGlyph_),
  };
  hash = fnv1aBytes(reinterpret_cast<const uint8_t*>(metadata), sizeof(metadata), hash);

  uint8_t sample[FINGERPRINT_SAMPLE_SIZE];
  const size_t sourceBytes = source.size();
  const size_t firstLength = std::min(sourceBytes, FINGERPRINT_SAMPLE_SIZE);
  if (firstLength > 0 && source.seekSet(0)) {
    const int bytesRead = source.read(sample, firstLength);
    if (bytesRead > 0) hash = fnv1aBytes(sample, static_cast<size_t>(bytesRead), hash);
  }

  if (sourceBytes > FINGERPRINT_SAMPLE_SIZE) {
    const size_t lastOffset = sourceBytes - FINGERPRINT_SAMPLE_SIZE;
    if (source.seekSet(lastOffset)) {
      const int bytesRead = source.read(sample, FINGERPRINT_SAMPLE_SIZE);
      if (bytesRead > 0) hash = fnv1aBytes(sample, static_cast<size_t>(bytesRead), hash);
    }
  }

  source.seekSet(0);
  return hash;
}

bool TtfFontEngine::load(const char* filepath, const uint8_t pixelSize, const uint8_t cellWidth,
                         const uint8_t cellHeight) {
  unload();
  if (filepath == nullptr || pixelSize == 0 || cellWidth == 0 || cellHeight == 0) return false;

  std::strncpy(fontPath_, filepath, sizeof(fontPath_) - 1);
  fontPath_[sizeof(fontPath_) - 1] = '\0';
  pixelSize_ = pixelSize;
  cellWidth_ = cellWidth;
  cellHeight_ = cellHeight;
  bytesPerGlyph_ = static_cast<uint16_t>(((cellWidth_ + 7) / 8) * cellHeight_);

  HalFile source;
  if (!Storage.openFileForRead("TTF", filepath, source)) return false;
  sourceSize_ = static_cast<uint32_t>(source.size());
  sourceFingerprint_ = computeSourceFingerprint(source, filepath);
  source.close();

  Storage.mkdir("/.crosspoint");
  Storage.mkdir(CACHE_DIR);
  std::snprintf(cachePath_, sizeof(cachePath_), "%s/%08lx_%u.ttfc", CACHE_DIR,
                static_cast<unsigned long>(sourceFingerprint_), static_cast<unsigned>(pixelSize_));

  scratch_ = static_cast<uint8_t*>(
      heap_caps_calloc(bytesPerGlyph_, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  diskIndex_ = static_cast<DiskIndexEntry*>(heap_caps_malloc(
      sizeof(DiskIndexEntry) * DISK_INDEX_CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (scratch_ == nullptr || diskIndex_ == nullptr) {
    LOG_ERR("TTF", "PSRAM allocation failed (scratch=%u, index=%u)", bytesPerGlyph_,
            static_cast<unsigned>(sizeof(DiskIndexEntry) * DISK_INDEX_CAPACITY));
    unload();
    return false;
  }
  for (size_t i = 0; i < DISK_INDEX_CAPACITY; ++i) diskIndex_[i] = DiskIndexEntry{};

  renderer_ = new (std::nothrow) OpenFontRender();
  if (renderer_ == nullptr) {
    unload();
    return false;
  }

  renderer_->setUseRenderTask(false);
  renderer_->setCacheSize(1, 4, 256 * 1024UL);
  renderer_->setDebugLevel(OFR_NONE);
  renderer_->setFontSize(pixelSize_);
  renderer_->setBackgroundFillMethod(BgFillMethod::None);
  renderer_->setAlignment(Align::TopLeft);
  renderer_->setLayout(Layout::Horizontal);
  renderer_->set_drawPixel([this](int32_t x, int32_t y, uint16_t color) {
    if (scratch_ == nullptr || x < 0 || y < 0 || x >= cellWidth_ || y >= cellHeight_) return;
    if (brightness565(color) < 94) return;
    const size_t byteIndex = static_cast<size_t>(y) * ((cellWidth_ + 7) / 8) + (x >> 3);
    scratch_[byteIndex] |= static_cast<uint8_t>(0x80u >> (x & 7));
  });
  renderer_->set_drawFastHLine([this](int32_t x, int32_t y, int32_t width, uint16_t color) {
    if (brightness565(color) < 94 || y < 0 || y >= cellHeight_) return;
    const int32_t start = std::max<int32_t>(0, x);
    const int32_t end = std::min<int32_t>(cellWidth_, x + width);
    for (int32_t px = start; px < end; ++px) {
      const size_t byteIndex = static_cast<size_t>(y) * ((cellWidth_ + 7) / 8) + (px >> 3);
      scratch_[byteIndex] |= static_cast<uint8_t>(0x80u >> (px & 7));
    }
  });
  renderer_->set_startWrite([] {});
  renderer_->set_endWrite([] {});

  const FT_Error error = renderer_->loadFont(filepath);
  if (error != 0) {
    LOG_ERR("TTF", "OpenFontRender failed to load %s (error=%d)", filepath, static_cast<int>(error));
    unload();
    return false;
  }

  if (!initializeCacheFile() || !rebuildDiskIndex() || !openAppendCache()) {
    LOG_ERR("TTF", "Persistent cache initialization failed: %s", cachePath_);
    unload();
    return false;
  }

  loaded_ = true;
  LOG_INF("TTF", "Loaded %s at %upx, disk glyphs=%u, cache=%s", filepath, pixelSize_,
          static_cast<unsigned>(diskIndexCount_), cachePath_);
  return true;
}

void TtfFontEngine::flushPersistentCache() {
  if (!cacheAppendFile_.isOpen() || pendingCacheWrites_ == 0) return;
  cacheAppendFile_.flush();
  pendingCacheWrites_ = 0;
}

void TtfFontEngine::unload() {
  loaded_ = false;
  flushPersistentCache();
  if (cacheAppendFile_.isOpen()) cacheAppendFile_.close();
  pendingCacheWrites_ = 0;

  if (renderer_ != nullptr) {
    renderer_->unloadFont();
    delete renderer_;
    renderer_ = nullptr;
  }
  if (scratch_ != nullptr) {
    heap_caps_free(scratch_);
    scratch_ = nullptr;
  }
  if (diskIndex_ != nullptr) {
    heap_caps_free(diskIndex_);
    diskIndex_ = nullptr;
  }

  diskIndexCount_ = 0;
  pixelSize_ = 0;
  cellWidth_ = 0;
  cellHeight_ = 0;
  bytesPerGlyph_ = 0;
  sourceSize_ = 0;
  sourceFingerprint_ = 0;
  fontPath_[0] = '\0';
  cachePath_[0] = '\0';
}

bool TtfFontEngine::initializeCacheFile() {
  CacheHeader expected{};
  std::memcpy(expected.magic, CACHE_MAGIC, sizeof(CACHE_MAGIC));
  expected.version = CACHE_VERSION;
  expected.pixelSize = pixelSize_;
  expected.cellWidth = cellWidth_;
  expected.cellHeight = cellHeight_;
  expected.bytesPerGlyph = bytesPerGlyph_;
  expected.sourceSize = sourceSize_;
  expected.sourceFingerprint = sourceFingerprint_;

  bool valid = false;
  HalFile file;
  if (Storage.openFileForRead("TTF", cachePath_, file)) {
    CacheHeader existing{};
    valid = file.read(&existing, sizeof(existing)) == static_cast<int>(sizeof(existing)) &&
            std::memcmp(existing.magic, CACHE_MAGIC, sizeof(CACHE_MAGIC)) == 0 &&
            existing.version == expected.version && existing.pixelSize == expected.pixelSize &&
            existing.cellWidth == expected.cellWidth && existing.cellHeight == expected.cellHeight &&
            existing.bytesPerGlyph == expected.bytesPerGlyph && existing.sourceSize == expected.sourceSize &&
            existing.sourceFingerprint == expected.sourceFingerprint;
    file.close();
  }

  if (valid) return true;

  Storage.remove(cachePath_);
  if (!Storage.openFileForWrite("TTF", cachePath_, file)) return false;
  const bool ok = file.write(&expected, sizeof(expected)) == sizeof(expected);
  file.flush();
  file.close();
  return ok;
}

bool TtfFontEngine::openAppendCache() {
  if (cacheAppendFile_.isOpen()) cacheAppendFile_.close();
  cacheAppendFile_ = Storage.open(cachePath_, O_RDWR | O_CREAT);
  if (!cacheAppendFile_) return false;
  pendingCacheWrites_ = 0;
  return cacheAppendFile_.seekSet(cacheAppendFile_.size());
}

bool TtfFontEngine::insertDiskEntry(const DiskIndexEntry& entry) {
  if (diskIndex_ == nullptr || entry.codepoint == 0xFFFFFFFFu) return false;
  const size_t slot = entry.codepoint % DISK_INDEX_CAPACITY;
  for (size_t probe = 0; probe < DISK_INDEX_CAPACITY; ++probe) {
    DiskIndexEntry& current = diskIndex_[(slot + probe) % DISK_INDEX_CAPACITY];
    if (current.codepoint == entry.codepoint) {
      current = entry;
      return true;
    }
    if (current.codepoint == 0xFFFFFFFFu) {
      current = entry;
      ++diskIndexCount_;
      return true;
    }
  }
  return false;
}

bool TtfFontEngine::findDiskEntry(const uint32_t codepoint, DiskIndexEntry* out) const {
  if (diskIndex_ == nullptr) return false;
  const size_t slot = codepoint % DISK_INDEX_CAPACITY;
  for (size_t probe = 0; probe < DISK_INDEX_CAPACITY; ++probe) {
    const DiskIndexEntry& current = diskIndex_[(slot + probe) % DISK_INDEX_CAPACITY];
    if (current.codepoint == 0xFFFFFFFFu) return false;
    if (current.codepoint == codepoint) {
      if (out != nullptr) *out = current;
      return true;
    }
  }
  return false;
}

bool TtfFontEngine::rebuildDiskIndex() {
  if (diskIndex_ == nullptr) return false;
  for (size_t i = 0; i < DISK_INDEX_CAPACITY; ++i) diskIndex_[i] = DiskIndexEntry{};
  diskIndexCount_ = 0;

  HalFile file;
  if (!Storage.openFileForRead("TTF", cachePath_, file)) return false;
  const size_t fileSize = file.size();
  if (fileSize < sizeof(CacheHeader) || !file.seekSet(sizeof(CacheHeader))) {
    file.close();
    return false;
  }

  uint8_t* readBuffer = static_cast<uint8_t*>(
      heap_caps_malloc(INDEX_READ_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (readBuffer == nullptr) {
    LOG_ERR("TTF", "Cannot allocate %u-byte cache index read buffer",
            static_cast<unsigned>(INDEX_READ_BUFFER_SIZE));
    file.close();
    return false;
  }

  size_t bufferPos = 0;
  size_t bufferLength = 0;
  size_t logicalPosition = sizeof(CacheHeader);

  auto refill = [&]() -> bool {
    const int read = file.read(readBuffer, INDEX_READ_BUFFER_SIZE);
    if (read <= 0) {
      bufferPos = 0;
      bufferLength = 0;
      return false;
    }
    bufferPos = 0;
    bufferLength = static_cast<size_t>(read);
    return true;
  };

  auto readExact = [&](void* destination, size_t length) -> bool {
    uint8_t* output = static_cast<uint8_t*>(destination);
    while (length > 0) {
      if (bufferPos >= bufferLength && !refill()) return false;
      const size_t available = bufferLength - bufferPos;
      const size_t chunk = std::min(available, length);
      std::memcpy(output, readBuffer + bufferPos, chunk);
      output += chunk;
      bufferPos += chunk;
      logicalPosition += chunk;
      length -= chunk;
    }
    return true;
  };

  auto skipExact = [&](size_t length) -> bool {
    while (length > 0) {
      if (bufferPos >= bufferLength && !refill()) return false;
      const size_t available = bufferLength - bufferPos;
      const size_t chunk = std::min(available, length);
      bufferPos += chunk;
      logicalPosition += chunk;
      length -= chunk;
    }
    return true;
  };

  bool valid = true;
  while (logicalPosition + sizeof(RecordHeader) <= fileSize) {
    RecordHeader record{};
    if (!readExact(&record, sizeof(record))) {
      valid = false;
      break;
    }

    const uint32_t bitmapOffset = static_cast<uint32_t>(logicalPosition);
    if (record.dataLength > bytesPerGlyph_ || logicalPosition + record.dataLength > fileSize) {
      valid = false;
      break;
    }

    DiskIndexEntry entry{};
    entry.codepoint = record.codepoint;
    entry.bitmapOffset = bitmapOffset;
    entry.dataLength = record.dataLength;
    entry.advanceX = record.advanceX;
    entry.flags = record.flags;
    entry.left = record.left;
    entry.top = record.top;
    entry.width = record.width;
    entry.height = record.height;

    if (!insertDiskEntry(entry)) {
      LOG_ERR("TTF", "Persistent glyph index is full at %u entries",
              static_cast<unsigned>(diskIndexCount_));
      valid = false;
      break;
    }

    if (!skipExact(record.dataLength)) {
      valid = false;
      break;
    }
  }

  heap_caps_free(readBuffer);
  file.close();

  if (!valid) {
    LOG_ERR("TTF", "Persistent cache is truncated or invalid; rebuilding it");
    Storage.remove(cachePath_);
    if (!initializeCacheFile()) return false;
    for (size_t i = 0; i < DISK_INDEX_CAPACITY; ++i) diskIndex_[i] = DiskIndexEntry{};
    diskIndexCount_ = 0;
  }

  LOG_DBG("TTF", "Persistent glyph index: %u records", static_cast<unsigned>(diskIndexCount_));
  return true;
}

bool TtfFontEngine::readDiskGlyph(const DiskIndexEntry& entry, uint8_t* bitmap,
                                  const size_t bitmapCapacity, ExternalGlyphMetrics* metrics) {
  if (metrics != nullptr) {
    metrics->width = entry.width;
    metrics->height = entry.height;
    metrics->advanceX = entry.advanceX;
    metrics->flags = entry.flags;
    metrics->left = entry.left;
    metrics->top = entry.top;
  }
  if (bitmap == nullptr) return true;
  if (entry.dataLength > bitmapCapacity) return false;

  // Reuse the long-lived O_RDWR cache handle instead of opening and closing a
  // file for every PSRAM-LRU miss. Restore the append position afterwards.
  flushPersistentCache();
  if (!cacheAppendFile_.isOpen()) return false;
  const size_t appendPosition = cacheAppendFile_.size();
  const bool ok = cacheAppendFile_.seekSet(entry.bitmapOffset) &&
                  cacheAppendFile_.read(bitmap, entry.dataLength) ==
                      static_cast<int>(entry.dataLength);
  const bool restored = cacheAppendFile_.seekSet(appendPosition);
  return ok && restored;
}

bool TtfFontEngine::appendDiskGlyph(const uint32_t codepoint, const uint8_t* bitmap,
                                    const ExternalGlyphMetrics& metrics, const uint16_t dataLength) {
  const ReaderMemoryDiagTrace storeBefore = ReaderMemoryDiagnostics::capture();
  const unsigned long storeStart = millis();
  if (!cacheAppendFile_.isOpen() || bitmap == nullptr || dataLength > bytesPerGlyph_) return false;

  DiskIndexEntry existing{};
  if (findDiskEntry(codepoint, &existing)) return true;
  if (diskIndexCount_ >= DISK_INDEX_CAPACITY) return false;

  const uint32_t recordOffset = static_cast<uint32_t>(cacheAppendFile_.position());
  RecordHeader record{};
  record.codepoint = codepoint;
  record.width = metrics.width;
  record.height = metrics.height;
  record.advanceX = metrics.advanceX;
  record.flags = metrics.flags;
  record.left = metrics.left;
  record.top = metrics.top;
  record.dataLength = dataLength;

  bool ok = cacheAppendFile_.write(&record, sizeof(record)) == sizeof(record);
  if (ok && dataLength > 0) ok = cacheAppendFile_.write(bitmap, dataLength) == dataLength;
  if (!ok) return false;

  DiskIndexEntry entry{};
  entry.codepoint = codepoint;
  entry.bitmapOffset = recordOffset + sizeof(RecordHeader);
  entry.dataLength = dataLength;
  entry.advanceX = metrics.advanceX;
  entry.flags = metrics.flags;
  entry.left = metrics.left;
  entry.top = metrics.top;
  entry.width = metrics.width;
  entry.height = metrics.height;
  if (!insertDiskEntry(entry)) return false;

  ++pendingCacheWrites_;
  if (pendingCacheWrites_ >= CACHE_FLUSH_INTERVAL) flushPersistentCache();
  {
    char phase[96];
    std::snprintf(phase, sizeof(phase), "glyph-bitmap-store-disk U+%04lx", static_cast<unsigned long>(codepoint));
    ReaderMemoryDiagnostics::logDeltaIfChanged(phase, storeBefore, ReaderMemoryDiagnostics::capture(), millis() - storeStart, 128, 4096, 20);
  }
  return true;
}

bool TtfFontEngine::rasterize(const uint32_t codepoint, uint8_t* bitmap, const size_t bitmapCapacity,
                              ExternalGlyphMetrics* metrics) {
  const ReaderMemoryDiagTrace rasterTotalBefore = ReaderMemoryDiagnostics::capture();
  const unsigned long rasterTotalStart = millis();
  if (renderer_ == nullptr || scratch_ == nullptr || metrics == nullptr || bitmap == nullptr ||
      bitmapCapacity < bytesPerGlyph_) {
    return false;
  }

  char utf8[5] = {};
  if (encodeUtf8(codepoint, utf8) == 0) return false;

  std::memset(scratch_, 0, bytesPerGlyph_);
  renderer_->setFontSize(pixelSize_);
  const ReaderMemoryDiagTrace drawBefore = ReaderMemoryDiagnostics::capture();
  const unsigned long drawStart = millis();
  const uint16_t advance = renderer_->drawString(utf8, 0, 0, 0xFFFF, 0x0000, Layout::Horizontal);
  const ReaderMemoryDiagTrace drawAfter = ReaderMemoryDiagnostics::capture();
  {
    char phase[96];
    std::snprintf(phase, sizeof(phase), "glyph-rasterize-drawString U+%04lx", static_cast<unsigned long>(codepoint));
    ReaderMemoryDiagnostics::logDeltaIfChanged(phase, drawBefore, drawAfter, millis() - drawStart, 128, 4096, 20);
    if (drawAfter.internalFree != drawBefore.internalFree ||
        drawAfter.internalMaxAlloc != drawBefore.internalMaxAlloc ||
        drawAfter.psramFree != drawBefore.psramFree) {
      CrossPointFtPsramAllocator::logSummary(phase);
    }
  }

  bool hasInk = false;
  for (uint16_t i = 0; i < bytesPerGlyph_; ++i) {
    if (scratch_[i] != 0) {
      hasInk = true;
      break;
    }
  }

  const bool whitespace =
      codepoint == ' ' || codepoint == '\t' || codepoint == 0x00A0 || codepoint == 0x3000;
  if (!hasInk && !whitespace) return false;

  if (bitmap != scratch_) std::memcpy(bitmap, scratch_, bytesPerGlyph_);
  *metrics = {};
  metrics->width = cellWidth_;
  metrics->height = cellHeight_;
  metrics->advanceX =
      std::max<uint16_t>(1, advance == 0 ? (whitespace ? cellWidth_ / 3 : cellWidth_) : advance);
  metrics->flags = 0x01;
  metrics->left = 0;
  metrics->top = cellHeight_;
  {
    char phase[96];
    std::snprintf(phase, sizeof(phase), "glyph-rasterize-total U+%04lx", static_cast<unsigned long>(codepoint));
    ReaderMemoryDiagnostics::logDeltaIfChanged(phase, rasterTotalBefore, ReaderMemoryDiagnostics::capture(), millis() - rasterTotalStart, 128, 4096, 20);
  }
  return true;
}

bool TtfFontEngine::hasCachedGlyph(const uint32_t codepoint) const {
  if (!loaded_) return false;
  return findDiskEntry(codepoint, nullptr);
}

bool TtfFontEngine::loadCachedGlyph(const uint32_t codepoint, uint8_t* bitmap, const size_t bitmapCapacity,
                                    ExternalGlyphMetrics* metrics) {
  const ReaderMemoryDiagTrace loadBefore = ReaderMemoryDiagnostics::capture();
  const unsigned long loadStart = millis();
  if (!loaded_) return false;
  DiskIndexEntry entry{};
  if (!findDiskEntry(codepoint, &entry)) return false;
  const bool ok = readDiskGlyph(entry, bitmap, bitmapCapacity, metrics);
  char phase[96];
  std::snprintf(phase, sizeof(phase), "glyph-lookup-disk-hit U+%04lx", static_cast<unsigned long>(codepoint));
  ReaderMemoryDiagnostics::logDeltaIfChanged(phase, loadBefore, ReaderMemoryDiagnostics::capture(), millis() - loadStart, 128, 4096, 20);
  return ok;
}

bool TtfFontEngine::loadGlyph(const uint32_t codepoint, uint8_t* bitmap, const size_t bitmapCapacity,
                              ExternalGlyphMetrics* metrics) {
  const ReaderMemoryDiagTrace loadBefore = ReaderMemoryDiagnostics::capture();
  const unsigned long loadStart = millis();
  if (!loaded_ || renderer_ == nullptr) return false;
  if (loadCachedGlyph(codepoint, bitmap, bitmapCapacity, metrics)) {
    return true;
  }

  ExternalGlyphMetrics renderedMetrics{};
  if (!rasterize(codepoint, bitmap, bitmapCapacity, &renderedMetrics)) return false;
  if (metrics != nullptr) *metrics = renderedMetrics;
  appendDiskGlyph(codepoint, bitmap, renderedMetrics, bytesPerGlyph_);
  {
    char phase[96];
    std::snprintf(phase, sizeof(phase), "glyph-lookup-rasterized U+%04lx", static_cast<unsigned long>(codepoint));
    ReaderMemoryDiagnostics::logDeltaIfChanged(phase, loadBefore, ReaderMemoryDiagnostics::capture(), millis() - loadStart, 128, 4096, 20);
  }
  return true;
}

bool TtfFontEngine::loadMetrics(const uint32_t codepoint, ExternalGlyphMetrics* metrics) {
  if (metrics == nullptr) return false;
  DiskIndexEntry entry{};
  if (findDiskEntry(codepoint, &entry)) return readDiskGlyph(entry, nullptr, 0, metrics);

  if (scratch_ == nullptr) return false;
  return loadGlyph(codepoint, scratch_, bytesPerGlyph_, metrics);
}
