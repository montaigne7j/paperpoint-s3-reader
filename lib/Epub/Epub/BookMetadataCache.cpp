#include "BookMetadataCache.h"

#include <Logging.h>
#include <Serialization.h>
#include <ZipFile.h>

#include <cstring>
#include <deque>
#include <memory>
#include <new>

#include "FsHelpers.h"

namespace {
constexpr uint8_t BOOK_CACHE_VERSION = 5;
constexpr char bookBinFile[] = "/book.bin";
constexpr char tmpBookBinFile[] = "/book.bin.tmp";
constexpr char tmpSpineBinFile[] = "/spine.bin.tmp";
constexpr char tmpTocBinFile[] = "/toc.bin.tmp";

class BufferedFileReader {
 public:
  explicit BufferedFileReader(FsFile& file, const size_t start = 0) : file(file), logicalPosition(start) {
    buffer.reset(new (std::nothrow) uint8_t[BUFFER_SIZE]);
    file.seek(start);
  }

  bool readExact(void* destination, size_t length) {
    auto* out = static_cast<uint8_t*>(destination);
    if (!buffer) {
      while (length > 0) {
        const int bytesRead = file.read(out, length);
        if (bytesRead <= 0) return false;
        out += bytesRead;
        length -= static_cast<size_t>(bytesRead);
        logicalPosition += static_cast<size_t>(bytesRead);
      }
      return true;
    }
    while (length > 0) {
      if (bufferPos >= bufferSize) {
        if (!refill()) return false;
      }
      const size_t available = bufferSize - bufferPos;
      const size_t chunk = std::min(available, length);
      memcpy(out, buffer.get() + bufferPos, chunk);
      bufferPos += chunk;
      logicalPosition += chunk;
      out += chunk;
      length -= chunk;
    }
    return true;
  }

  template <typename T>
  bool readPod(T& value) {
    return readExact(&value, sizeof(T));
  }

  bool readString(std::string& value) {
    uint32_t length = 0;
    if (!readPod(length)) return false;
    value.resize(length);
    return length == 0 || readExact(value.data(), length);
  }

  bool skip(size_t length) {
    uint8_t scratch[64];
    while (length > 0) {
      const size_t chunk = std::min(length, sizeof(scratch));
      if (!readExact(scratch, chunk)) return false;
      length -= chunk;
    }
    return true;
  }

  bool skipString() {
    uint32_t length = 0;
    return readPod(length) && skip(length);
  }

  size_t position() const { return logicalPosition; }

 private:
  static constexpr size_t BUFFER_SIZE = 4096;
  FsFile& file;
  std::unique_ptr<uint8_t[]> buffer;
  size_t bufferPos = 0;
  size_t bufferSize = 0;
  size_t logicalPosition = 0;

  bool refill() {
    if (!buffer) return false;
    const int bytesRead = file.read(buffer.get(), BUFFER_SIZE);
    if (bytesRead <= 0) return false;
    bufferPos = 0;
    bufferSize = static_cast<size_t>(bytesRead);
    return true;
  }
};

class BufferedFileWriter {
 public:
  explicit BufferedFileWriter(FsFile& file) : file(file) { buffer.reset(new (std::nothrow) uint8_t[BUFFER_SIZE]); }
  ~BufferedFileWriter() = default;

  bool writeBytes(const void* source, size_t length) {
    const auto* input = static_cast<const uint8_t*>(source);
    while (length > 0) {
      if (!buffer) {
        const size_t written = file.write(input, length);
        logicalPosition += written;
        return written == length;
      }
      const size_t space = BUFFER_SIZE - used;
      if (space == 0 && !flush()) return false;
      const size_t chunk = std::min(BUFFER_SIZE - used, length);
      memcpy(buffer.get() + used, input, chunk);
      used += chunk;
      logicalPosition += chunk;
      input += chunk;
      length -= chunk;
    }
    return true;
  }

  template <typename T>
  bool writePod(const T& value) {
    return writeBytes(&value, sizeof(T));
  }

  bool writeString(const std::string& value) {
    const uint32_t length = static_cast<uint32_t>(value.size());
    return writePod(length) && (length == 0 || writeBytes(value.data(), length));
  }

  bool flush() {
    if (used == 0) return true;
    const size_t written = file.write(buffer.get(), used);
    if (written != used) return false;
    used = 0;
    return true;
  }

  size_t position() const { return logicalPosition; }

 private:
  static constexpr size_t BUFFER_SIZE = 4096;
  FsFile& file;
  std::unique_ptr<uint8_t[]> buffer;
  size_t used = 0;
  size_t logicalPosition = 0;
};
}  // namespace

/* ============= WRITING / BUILDING FUNCTIONS ================ */

bool BookMetadataCache::beginWrite() {
  buildMode = true;
  spineCount = 0;
  tocCount = 0;
  spineWriteUsed = 0;
  tocWriteUsed = 0;
  tempWriteFailed = false;
  LOG_DBG("BMC", "Entering write mode");
  return true;
}

bool BookMetadataCache::beginContentOpfPass() {
  LOG_DBG("BMC", "Beginning content opf pass");
  spineWriteUsed = 0;
  tempWriteFailed = false;

  // Open spine file for writing
  return Storage.openFileForWrite("BMC", cachePath + tmpSpineBinFile, spineFile);
}

bool BookMetadataCache::endContentOpfPass() {
  const bool flushed = flushTempBuffer(spineFile, spineWriteBuffer, spineWriteUsed);
  // Explicit close() required: member variable persists beyond function scope
  spineFile.close();
  return flushed && !tempWriteFailed;
}

bool BookMetadataCache::beginTocPass() {
  LOG_DBG("BMC", "Beginning toc pass");

  if (!Storage.openFileForRead("BMC", cachePath + tmpSpineBinFile, spineFile)) {
    return false;
  }
  if (!Storage.openFileForWrite("BMC", cachePath + tmpTocBinFile, tocFile)) {
    // Explicit close() required: member variable persists beyond function scope
    spineFile.close();
    return false;
  }
  tocWriteUsed = 0;
  tempWriteFailed = false;

  if (spineCount >= LARGE_SPINE_THRESHOLD) {
    spineHrefIndex.clear();
    spineHrefIndex.resize(spineCount);
    BufferedFileReader spineReader(spineFile, 0);
    for (int i = 0; i < spineCount; i++) {
      std::string href;
      size_t ignoredSize = 0;
      int16_t ignoredToc = -1;
      if (!spineReader.readString(href) || !spineReader.readPod(ignoredSize) || !spineReader.readPod(ignoredToc)) {
        LOG_ERR("BMC", "Failed to build spine href index at item %d", i);
        tocFile.close();
        spineFile.close();
        return false;
      }
      SpineHrefIndexEntry idx;
      idx.hrefHash = fnvHash64(href);
      idx.hrefLen = static_cast<uint16_t>(href.size());
      idx.spineIndex = static_cast<int16_t>(i);
      spineHrefIndex[i] = idx;
    }
    std::sort(spineHrefIndex.begin(), spineHrefIndex.end(),
              [](const SpineHrefIndexEntry& a, const SpineHrefIndexEntry& b) {
                return a.hrefHash < b.hrefHash || (a.hrefHash == b.hrefHash && a.hrefLen < b.hrefLen);
              });
    spineFile.seek(0);
    useSpineHrefIndex = true;
    LOG_DBG("BMC", "Using fast index for %d spine items", spineCount);
  } else {
    useSpineHrefIndex = false;
  }

  return true;
}

bool BookMetadataCache::endTocPass() {
  const bool flushed = flushTempBuffer(tocFile, tocWriteBuffer, tocWriteUsed);
  // Explicit close() required: member variables persist beyond function scope
  tocFile.close();
  spineFile.close();

  spineHrefIndex.clear();
  spineHrefIndex.shrink_to_fit();
  useSpineHrefIndex = false;

  return flushed && !tempWriteFailed;
}

bool BookMetadataCache::endWrite() {
  if (!buildMode) {
    LOG_DBG("BMC", "endWrite called but not in build mode");
    return false;
  }

  buildMode = false;
  LOG_DBG("BMC", "Wrote %d spine, %d TOC entries", spineCount, tocCount);
  return !tempWriteFailed;
}

bool BookMetadataCache::flushTempBuffer(FsFile& file,
                                        std::array<uint8_t, TEMP_WRITE_BUFFER_SIZE>& buffer,
                                        size_t& used) {
  if (used == 0) return true;
  const size_t written = file.write(buffer.data(), used);
  if (written != used) {
    tempWriteFailed = true;
    return false;
  }
  used = 0;
  return true;
}

bool BookMetadataCache::appendTempBytes(FsFile& file,
                                        std::array<uint8_t, TEMP_WRITE_BUFFER_SIZE>& buffer,
                                        size_t& used, const void* data, size_t length) {
  const auto* input = static_cast<const uint8_t*>(data);
  while (length > 0) {
    if (used == buffer.size() && !flushTempBuffer(file, buffer, used)) return false;
    const size_t chunk = std::min(buffer.size() - used, length);
    memcpy(buffer.data() + used, input, chunk);
    used += chunk;
    input += chunk;
    length -= chunk;
  }
  return true;
}

bool BookMetadataCache::writeTempString(FsFile& file,
                                        std::array<uint8_t, TEMP_WRITE_BUFFER_SIZE>& buffer,
                                        size_t& used, const std::string& value) {
  const uint32_t length = static_cast<uint32_t>(value.size());
  return appendTempBytes(file, buffer, used, &length, sizeof(length)) &&
         (length == 0 || appendTempBytes(file, buffer, used, value.data(), length));
}

bool BookMetadataCache::writeTempSpineEntry(const SpineEntry& entry) {
  return writeTempString(spineFile, spineWriteBuffer, spineWriteUsed, entry.href) &&
         appendTempBytes(spineFile, spineWriteBuffer, spineWriteUsed, &entry.cumulativeSize,
                         sizeof(entry.cumulativeSize)) &&
         appendTempBytes(spineFile, spineWriteBuffer, spineWriteUsed, &entry.tocIndex,
                         sizeof(entry.tocIndex));
}

bool BookMetadataCache::writeTempTocEntry(const TocEntry& entry) {
  return writeTempString(tocFile, tocWriteBuffer, tocWriteUsed, entry.title) &&
         writeTempString(tocFile, tocWriteBuffer, tocWriteUsed, entry.href) &&
         writeTempString(tocFile, tocWriteBuffer, tocWriteUsed, entry.anchor) &&
         appendTempBytes(tocFile, tocWriteBuffer, tocWriteUsed, &entry.level, sizeof(entry.level)) &&
         appendTempBytes(tocFile, tocWriteBuffer, tocWriteUsed, &entry.spineIndex,
                         sizeof(entry.spineIndex));
}

bool BookMetadataCache::buildBookBin(const std::string& epubPath, const BookMetadata& metadata) {
  const std::string tmpBookPath = cachePath + tmpBookBinFile;
  const std::string finalBookPath = cachePath + bookBinFile;
  if (Storage.exists(tmpBookPath.c_str())) Storage.remove(tmpBookPath.c_str());

  // Build into a temporary file. The final cache is replaced only after every
  // pass succeeds, so a reset or power loss cannot leave a corrupt book.bin.
  if (!Storage.openFileForWrite("BMC", tmpBookPath, bookFile)) return false;
  if (!Storage.openFileForRead("BMC", cachePath + tmpSpineBinFile, spineFile)) {
    bookFile.close();
    Storage.remove(tmpBookPath.c_str());
    return false;
  }
  if (!Storage.openFileForRead("BMC", cachePath + tmpTocBinFile, tocFile)) {
    bookFile.close();
    spineFile.close();
    Storage.remove(tmpBookPath.c_str());
    return false;
  }

  auto failBuild = [&]() {
    bookFile.close();
    spineFile.close();
    tocFile.close();
    Storage.remove(tmpBookPath.c_str());
    return false;
  };

  constexpr uint32_t headerASize =
      sizeof(BOOK_CACHE_VERSION) + sizeof(uint32_t) + sizeof(spineCount) + sizeof(tocCount);
  const uint32_t metadataSize = metadata.title.size() + metadata.author.size() + metadata.language.size() +
                                metadata.coverItemHref.size() + metadata.textReferenceHref.size() +
                                sizeof(uint32_t) * 5;
  const uint32_t lutSize = sizeof(uint32_t) * spineCount + sizeof(uint32_t) * tocCount;
  const uint32_t finalLutOffset = headerASize + metadataSize;
  const uint32_t spineDataOffset = finalLutOffset + lutSize;
  const uint32_t tocDataOffset = spineDataOffset + static_cast<uint32_t>(spineFile.size());

  BufferedFileWriter bookWriter(bookFile);
  if (!bookWriter.writePod(BOOK_CACHE_VERSION) || !bookWriter.writePod(finalLutOffset) ||
      !bookWriter.writePod(spineCount) || !bookWriter.writePod(tocCount) ||
      !bookWriter.writeString(metadata.title) || !bookWriter.writeString(metadata.author) ||
      !bookWriter.writeString(metadata.language) || !bookWriter.writeString(metadata.coverItemHref) ||
      !bookWriter.writeString(metadata.textReferenceHref)) {
    return failBuild();
  }

  // Build both LUTs using buffered sequential reads. We only need to skip the
  // variable-length strings to discover each entry's relative offset.
  {
    BufferedFileReader reader(spineFile, 0);
    for (int i = 0; i < spineCount; ++i) {
      const uint32_t entryPos = spineDataOffset + static_cast<uint32_t>(reader.position());
      if (!bookWriter.writePod(entryPos) || !reader.skipString() ||
          !reader.skip(sizeof(size_t) + sizeof(int16_t))) {
        LOG_ERR("BMC", "Failed while building spine LUT at %d", i);
        return failBuild();
      }
    }
  }
  {
    BufferedFileReader reader(tocFile, 0);
    for (int i = 0; i < tocCount; ++i) {
      const uint32_t entryPos = tocDataOffset + static_cast<uint32_t>(reader.position());
      if (!bookWriter.writePod(entryPos) || !reader.skipString() || !reader.skipString() ||
          !reader.skipString() || !reader.skip(sizeof(uint8_t) + sizeof(int16_t))) {
        LOG_ERR("BMC", "Failed while building TOC LUT at %d", i);
        return failBuild();
      }
    }
  }

  // Build spineIndex -> first tocIndex in one buffered sequential TOC pass.
  std::deque<int16_t> spineToTocIndex(spineCount, -1);
  {
    BufferedFileReader reader(tocFile, 0);
    for (int j = 0; j < tocCount; ++j) {
      uint8_t level = 0;
      int16_t spineIndex = -1;
      if (!reader.skipString() || !reader.skipString() || !reader.skipString() ||
          !reader.readPod(level) || !reader.readPod(spineIndex)) {
        LOG_ERR("BMC", "Failed while building spine/TOC map at %d", j);
        return failBuild();
      }
      if (spineIndex >= 0 && spineIndex < spineCount && spineToTocIndex[spineIndex] == -1) {
        spineToTocIndex[spineIndex] = static_cast<int16_t>(j);
      }
    }
  }

  ZipFile zip(epubPath);
  if (!zip.open()) {
    LOG_ERR("BMC", "Could not open EPUB zip for size calculations");
    return failBuild();
  }

  std::deque<uint32_t> spineSizes;
  bool useBatchSizes = false;
  if (spineCount >= LARGE_SPINE_THRESHOLD) {
    LOG_DBG("BMC", "Using batch size lookup for %d spine items", spineCount);
    std::deque<ZipFile::SizeTarget> targets;
    targets.resize(spineCount);

    BufferedFileReader reader(spineFile, 0);
    for (int i = 0; i < spineCount; ++i) {
      std::string href;
      size_t ignoredSize = 0;
      int16_t ignoredToc = -1;
      if (!reader.readString(href) || !reader.readPod(ignoredSize) || !reader.readPod(ignoredToc)) {
        zip.close();
        LOG_ERR("BMC", "Failed while preparing batch size target %d", i);
        return failBuild();
      }
      const std::string path = FsHelpers::normalisePath(href);
      targets[i] = {ZipFile::fnvHash64(path.c_str(), path.size()), static_cast<uint16_t>(path.size()),
                    static_cast<uint16_t>(i)};
    }

    std::sort(targets.begin(), targets.end(), [](const ZipFile::SizeTarget& a, const ZipFile::SizeTarget& b) {
      return a.hash < b.hash || (a.hash == b.hash && a.len < b.len);
    });

    spineSizes.resize(spineCount, 0);
    const int matched = zip.fillUncompressedSizes(targets, spineSizes);
    LOG_DBG("BMC", "Batch lookup matched %d/%d spine items", matched, spineCount);
    useBatchSizes = true;
  }

  uint32_t cumulativeSize = 0;
  int lastSpineTocIndex = -1;
  int missingTocCount = 0;
  {
    BufferedFileReader reader(spineFile, 0);
    for (int i = 0; i < spineCount; ++i) {
      std::string href;
      size_t ignoredSize = 0;
      int16_t ignoredToc = -1;
      if (!reader.readString(href) || !reader.readPod(ignoredSize) || !reader.readPod(ignoredToc)) {
        zip.close();
        LOG_ERR("BMC", "Failed while writing final spine item %d", i);
        return failBuild();
      }

      int16_t tocIndex = spineToTocIndex[i];
      if (tocIndex == -1) {
        if (missingTocCount < 5) {
          LOG_DBG("BMC", "No TOC entry for spine item %d: %s; using previous section", i, href.c_str());
        }
        ++missingTocCount;
        tocIndex = static_cast<int16_t>(lastSpineTocIndex);
      }
      lastSpineTocIndex = tocIndex;

      size_t itemSize = 0;
      if (useBatchSizes) {
        itemSize = spineSizes[i];
      }
      if (itemSize == 0) {
        const std::string path = FsHelpers::normalisePath(href);
        if (!zip.getInflatedFileSize(path.c_str(), &itemSize)) {
          LOG_ERR("BMC", "Could not get size for spine item: %s", path.c_str());
        }
      }

      cumulativeSize += static_cast<uint32_t>(itemSize);
      const size_t storedCumulativeSize = cumulativeSize;
      if (!bookWriter.writeString(href) || !bookWriter.writePod(storedCumulativeSize) ||
          !bookWriter.writePod(tocIndex)) {
        zip.close();
        return failBuild();
      }
    }
  }
  zip.close();

  if (missingTocCount > 5) {
    LOG_DBG("BMC", "%d spine items had no direct TOC entry (only first 5 logged)", missingTocCount);
  }

  // TOC records do not need any transformation. Copy the complete temp file in
  // large blocks rather than deserialising and serialising five fields per row.
  tocFile.seek(0);
  std::unique_ptr<uint8_t[]> copyBuffer(new (std::nothrow) uint8_t[4096]);
  if (!copyBuffer) return failBuild();
  while (tocFile.available()) {
    const int bytesRead = tocFile.read(copyBuffer.get(), 4096);
    if (bytesRead < 0 || (bytesRead > 0 && !bookWriter.writeBytes(copyBuffer.get(), bytesRead))) {
      return failBuild();
    }
    if (bytesRead == 0) break;
  }

  if (!bookWriter.flush()) return failBuild();
  bookFile.close();
  spineFile.close();
  tocFile.close();

  if (Storage.exists(finalBookPath.c_str()) && !Storage.remove(finalBookPath.c_str())) {
    Storage.remove(tmpBookPath.c_str());
    return false;
  }
  if (!Storage.rename(tmpBookPath.c_str(), finalBookPath.c_str())) {
    LOG_ERR("BMC", "Could not promote temporary book cache");
    Storage.remove(tmpBookPath.c_str());
    return false;
  }

  LOG_DBG("BMC", "Successfully built book.bin with buffered I/O");
  return true;
}

bool BookMetadataCache::cleanupTmpFiles() const {
  const auto spineBinFile = cachePath + tmpSpineBinFile;
  if (Storage.exists(spineBinFile.c_str())) {
    Storage.remove(spineBinFile.c_str());
  }
  const auto tocBinFile = cachePath + tmpTocBinFile;
  if (Storage.exists(tocBinFile.c_str())) {
    Storage.remove(tocBinFile.c_str());
  }
  const auto partialBookFile = cachePath + tmpBookBinFile;
  if (Storage.exists(partialBookFile.c_str())) {
    Storage.remove(partialBookFile.c_str());
  }
  return true;
}

uint32_t BookMetadataCache::writeSpineEntry(FsFile& file, const SpineEntry& entry) const {
  const uint32_t pos = file.position();
  serialization::writeString(file, entry.href);
  serialization::writePod(file, entry.cumulativeSize);
  serialization::writePod(file, entry.tocIndex);
  return pos;
}

uint32_t BookMetadataCache::writeTocEntry(FsFile& file, const TocEntry& entry) const {
  const uint32_t pos = file.position();
  serialization::writeString(file, entry.title);
  serialization::writeString(file, entry.href);
  serialization::writeString(file, entry.anchor);
  serialization::writePod(file, entry.level);
  serialization::writePod(file, entry.spineIndex);
  return pos;
}

// Note: for the LUT to be accurate, this **MUST** be called for all spine items before `addTocEntry` is ever called
// this is because in this function we're marking positions of the items
void BookMetadataCache::createSpineEntry(const std::string& href) {
  if (!buildMode || !spineFile) {
    LOG_DBG("BMC", "createSpineEntry called but not in build mode");
    return;
  }

  const SpineEntry entry(href, 0, -1);
  if (!writeTempSpineEntry(entry)) {
    tempWriteFailed = true;
    return;
  }
  spineCount++;
}

void BookMetadataCache::createTocEntry(const std::string& title, const std::string& href, const std::string& anchor,
                                       const uint8_t level) {
  if (!buildMode || !tocFile || !spineFile) {
    LOG_DBG("BMC", "createTocEntry called but not in build mode");
    return;
  }

  int16_t spineIndex = -1;

  if (useSpineHrefIndex) {
    uint64_t targetHash = fnvHash64(href);
    uint16_t targetLen = static_cast<uint16_t>(href.size());

    auto it =
        std::lower_bound(spineHrefIndex.begin(), spineHrefIndex.end(), SpineHrefIndexEntry{targetHash, targetLen, 0},
                         [](const SpineHrefIndexEntry& a, const SpineHrefIndexEntry& b) {
                           return a.hrefHash < b.hrefHash || (a.hrefHash == b.hrefHash && a.hrefLen < b.hrefLen);
                         });

    while (it != spineHrefIndex.end() && it->hrefHash == targetHash && it->hrefLen == targetLen) {
      spineIndex = it->spineIndex;
      break;
    }

    if (spineIndex == -1) {
      LOG_DBG("BMC", "createTocEntry: Could not find spine item for TOC href %s", href.c_str());
    }
  } else {
    spineFile.seek(0);
    for (int i = 0; i < spineCount; i++) {
      auto spineEntry = readSpineEntry(spineFile);
      if (spineEntry.href == href) {
        spineIndex = static_cast<int16_t>(i);
        break;
      }
    }
    if (spineIndex == -1) {
      LOG_DBG("BMC", "createTocEntry: Could not find spine item for TOC href %s", href.c_str());
    }
  }

  const TocEntry entry(title, href, anchor, level, spineIndex);
  if (!writeTempTocEntry(entry)) {
    tempWriteFailed = true;
    return;
  }
  tocCount++;
}

/* ============= READING / LOADING FUNCTIONS ================ */

bool BookMetadataCache::load() {
  if (!Storage.openFileForRead("BMC", cachePath + bookBinFile, bookFile)) {
    return false;
  }

  uint8_t version;
  serialization::readPod(bookFile, version);
  if (version != BOOK_CACHE_VERSION) {
    LOG_DBG("BMC", "Cache version mismatch: expected %d, got %d", BOOK_CACHE_VERSION, version);
    // Explicit close() required: member variable persists beyond function scope
    bookFile.close();
    return false;
  }

  serialization::readPod(bookFile, lutOffset);
  serialization::readPod(bookFile, spineCount);
  serialization::readPod(bookFile, tocCount);

  serialization::readString(bookFile, coreMetadata.title);
  serialization::readString(bookFile, coreMetadata.author);
  serialization::readString(bookFile, coreMetadata.language);
  serialization::readString(bookFile, coreMetadata.coverItemHref);
  serialization::readString(bookFile, coreMetadata.textReferenceHref);

  loaded = true;
  LOG_DBG("BMC", "Loaded cache data: %d spine, %d TOC entries", spineCount, tocCount);
  return true;
}

BookMetadataCache::SpineEntry BookMetadataCache::getSpineEntry(const int index) {
  if (!loaded) {
    LOG_ERR("BMC", "getSpineEntry called but cache not loaded");
    return {};
  }

  if (index < 0 || index >= static_cast<int>(spineCount)) {
    LOG_ERR("BMC", "getSpineEntry index %d out of range", index);
    return {};
  }

  // Seek to spine LUT item, read from LUT and get out data
  bookFile.seek(lutOffset + sizeof(uint32_t) * index);
  uint32_t spineEntryPos;
  serialization::readPod(bookFile, spineEntryPos);
  bookFile.seek(spineEntryPos);
  return readSpineEntry(bookFile);
}

BookMetadataCache::TocEntry BookMetadataCache::getTocEntry(const int index) {
  if (!loaded) {
    LOG_ERR("BMC", "getTocEntry called but cache not loaded");
    return {};
  }

  if (index < 0 || index >= static_cast<int>(tocCount)) {
    LOG_ERR("BMC", "getTocEntry index %d out of range", index);
    return {};
  }

  // Seek to TOC LUT item, read from LUT and get out data
  bookFile.seek(lutOffset + sizeof(uint32_t) * spineCount + sizeof(uint32_t) * index);
  uint32_t tocEntryPos;
  serialization::readPod(bookFile, tocEntryPos);
  bookFile.seek(tocEntryPos);
  return readTocEntry(bookFile);
}

bool BookMetadataCache::getTocNodeInfos(std::vector<TocNodeInfo>& nodes) {
  nodes.clear();
  if (!loaded) {
    LOG_ERR("BMC", "getTocNodeInfos called but cache not loaded");
    return false;
  }
  if (tocCount == 0) return true;

  const size_t firstTocLutPos = lutOffset + sizeof(uint32_t) * spineCount;
  if (!bookFile.seek(firstTocLutPos)) return false;

  uint32_t firstTocEntryPos = 0;
  serialization::readPod(bookFile, firstTocEntryPos);
  if (!bookFile.seek(firstTocEntryPos)) return false;

  nodes.reserve(tocCount);
  BufferedFileReader reader(bookFile, firstTocEntryPos);
  for (int i = 0; i < tocCount; ++i) {
    TocNodeInfo info;
    if (!reader.skipString() || !reader.skipString() || !reader.skipString() ||
        !reader.readPod(info.level) || !reader.readPod(info.spineIndex)) {
      LOG_ERR("BMC", "Failed to read TOC node metadata at %d", i);
      nodes.clear();
      return false;
    }
    nodes.push_back(info);
  }
  return true;
}

BookMetadataCache::SpineEntry BookMetadataCache::readSpineEntry(FsFile& file) const {
  SpineEntry entry;
  serialization::readString(file, entry.href);
  serialization::readPod(file, entry.cumulativeSize);
  serialization::readPod(file, entry.tocIndex);
  return entry;
}

BookMetadataCache::TocEntry BookMetadataCache::readTocEntry(FsFile& file) const {
  TocEntry entry;
  serialization::readString(file, entry.title);
  serialization::readString(file, entry.href);
  serialization::readString(file, entry.anchor);
  serialization::readPod(file, entry.level);
  serialization::readPod(file, entry.spineIndex);
  return entry;
}
