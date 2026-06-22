#include "SleepImageManager.h"

#include <Bitmap.h>
#include "../../src/CrossPointSettings.h"
#include "../../src/CrossPointState.h"
#include "../../src/components/UITheme.h"
#include <FsHelpers.h>
#include <HalDisplay.h>
#include <HalStorage.h>
// JPEGDEC's public header declares an overload taking the Arduino `File`
// type.  In Arduino-ESP32 3.x the concrete type is fs::File, and this file is
// compiled as an independent PlatformIO library where the global File alias is
// not always visible while parsing JPEGDEC.h.  Map the token only while the
// JPEGDEC header is parsed so its declaration becomes open(fs::File&, ...),
// then immediately remove the macro to avoid affecting the rest of this file.
#include <Arduino.h>
#include <FS.h>
#define File fs::File
#include <JPEGDEC.h>
#undef File
#ifdef INTELSHORT
#undef INTELSHORT
#endif
#ifdef INTELLONG
#undef INTELLONG
#endif
#ifdef MOTOSHORT
#undef MOTOSHORT
#endif
#ifdef MOTOLONG
#undef MOTOLONG
#endif
#include <Logging.h>
#include <PNGdec.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

namespace {

constexpr int TARGET_WIDTH = 540;
constexpr int TARGET_HEIGHT = 960;
constexpr size_t TARGET_PIXELS =
    static_cast<size_t>(TARGET_WIDTH) * TARGET_HEIGHT;
constexpr size_t OPAQUE_PAYLOAD_SIZE = TARGET_PIXELS / 2;
constexpr size_t OVERLAY_PAYLOAD_SIZE = TARGET_PIXELS;
constexpr uint8_t CACHE_VERSION = 2;
constexpr uint32_t START_AFTER_IDLE_MS = 3000;
constexpr uint32_t PAUSE_AFTER_ACTIVITY_MS = 1200;
constexpr size_t MAX_SOURCE_PIXELS = 2500000;
constexpr int MAX_SOURCE_DIMENSION = 4096;
constexpr char CACHE_DIR[] = "/.crosspoint/sleepcache";
constexpr char LAST_CACHE_FILE[] = "/.crosspoint/sleepcache/last.txt";

#pragma pack(push, 1)
struct SleepCacheHeader {
  char magic[4];
  uint8_t version;
  uint8_t type;
  uint16_t width;
  uint16_t height;
  uint32_t payloadSize;
  uint32_t sourceSize;
  uint32_t sourceFingerprint;
  uint32_t sourcePathHash;
  uint32_t payloadFingerprint;
};
#pragma pack(pop)

enum CacheType : uint8_t {
  CACHE_OPAQUE = 1,
  CACHE_OVERLAY = 2,
};

struct TaskControl {
  volatile bool* cancel;
  volatile uint32_t* lastActivityMs;
};

bool shouldCancel(const TaskControl& control) {
  return control.cancel != nullptr && *control.cancel;
}

bool pauseForForeground(const TaskControl& control) {
  while (!shouldCancel(control) &&
         millis() - *control.lastActivityMs < PAUSE_AFTER_ACTIVITY_MS) {
    vTaskDelay(pdMS_TO_TICKS(30));
  }
  return !shouldCancel(control);
}

uint32_t fnv1aUpdate(uint32_t hash, const uint8_t* data, size_t size) {
  constexpr uint32_t PRIME = 16777619u;
  for (size_t i = 0; i < size; ++i) {
    hash ^= data[i];
    hash *= PRIME;
  }
  return hash;
}

uint32_t fnv1aString(const std::string& value) {
  return fnv1aUpdate(
      2166136261u,
      reinterpret_cast<const uint8_t*>(value.data()),
      value.size());
}

bool readExact(FsFile& file, void* destination, size_t size) {
  auto* bytes = static_cast<uint8_t*>(destination);
  size_t done = 0;
  while (done < size) {
    const int count = file.read(bytes + done, size - done);
    if (count <= 0) return false;
    done += static_cast<size_t>(count);
  }
  return true;
}

bool writeExact(FsFile& file, const void* source, size_t size) {
  const auto* bytes = static_cast<const uint8_t*>(source);
  size_t done = 0;
  while (done < size) {
    const size_t count = file.write(bytes + done, size - done);
    if (count == 0) return false;
    done += count;
  }
  return true;
}

inline void setPackedNibble(
    uint8_t* buffer,
    int width,
    int x,
    int y,
    uint8_t value) {
  const size_t index =
      static_cast<size_t>(y) * static_cast<size_t>(width / 2) +
      static_cast<size_t>(x >> 1);
  value &= 0x0F;
  if ((x & 1) == 0) {
    buffer[index] = static_cast<uint8_t>((buffer[index] & 0x0F) | (value << 4));
  } else {
    buffer[index] = static_cast<uint8_t>((buffer[index] & 0xF0) | value);
  }
}


struct CropRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

CropRect calculateCenterCrop(int sourceWidth, int sourceHeight) {
  CropRect crop{0, 0, sourceWidth, sourceHeight};
  const int64_t sourceCross =
      static_cast<int64_t>(sourceWidth) * TARGET_HEIGHT;
  const int64_t targetCross =
      static_cast<int64_t>(TARGET_WIDTH) * sourceHeight;

  if (sourceCross > targetCross) {
    crop.width = static_cast<int>(
        (static_cast<int64_t>(sourceHeight) * TARGET_WIDTH) / TARGET_HEIGHT);
    crop.width = std::max(1, std::min(crop.width, sourceWidth));
    crop.x = (sourceWidth - crop.width) / 2;
  } else if (sourceCross < targetCross) {
    crop.height = static_cast<int>(
        (static_cast<int64_t>(sourceWidth) * TARGET_HEIGHT) / TARGET_WIDTH);
    crop.height = std::max(1, std::min(crop.height, sourceHeight));
    crop.y = (sourceHeight - crop.height) / 2;
  }
  return crop;
}

void calculateFitRect(
    int sourceWidth,
    int sourceHeight,
    int& outX,
    int& outY,
    int& outWidth,
    int& outHeight) {
  const float scaleX = static_cast<float>(TARGET_WIDTH) / sourceWidth;
  const float scaleY = static_cast<float>(TARGET_HEIGHT) / sourceHeight;
  const float scale = std::min(scaleX, scaleY);
  outWidth = std::max(1, static_cast<int>(std::round(sourceWidth * scale)));
  outHeight = std::max(1, static_cast<int>(std::round(sourceHeight * scale)));
  outWidth = std::min(outWidth, TARGET_WIDTH);
  outHeight = std::min(outHeight, TARGET_HEIGHT);
  outX = (TARGET_WIDTH - outWidth) / 2;
  outY = (TARGET_HEIGHT - outHeight) / 2;
}

uint8_t bilinearSample(
    const uint8_t* source,
    int sourceWidth,
    int sourceHeight,
    uint32_t sourceX16,
    uint32_t sourceY16) {
  int x0 = static_cast<int>(sourceX16 >> 16);
  int y0 = static_cast<int>(sourceY16 >> 16);
  uint32_t fx = sourceX16 & 0xFFFFu;
  uint32_t fy = sourceY16 & 0xFFFFu;

  x0 = std::max(0, std::min(x0, sourceWidth - 1));
  y0 = std::max(0, std::min(y0, sourceHeight - 1));
  const int x1 = std::min(x0 + 1, sourceWidth - 1);
  const int y1 = std::min(y0 + 1, sourceHeight - 1);

  const uint32_t top =
      (static_cast<uint32_t>(source[y0 * sourceWidth + x0]) * (65536u - fx) +
       static_cast<uint32_t>(source[y0 * sourceWidth + x1]) * fx) >> 16;
  const uint32_t bottom =
      (static_cast<uint32_t>(source[y1 * sourceWidth + x0]) * (65536u - fx) +
       static_cast<uint32_t>(source[y1 * sourceWidth + x1]) * fx) >> 16;
  return static_cast<uint8_t>(
      (top * (65536u - fy) + bottom * fy) >> 16);
}


void bilinearSampleOverlay(
    const uint8_t* sourceGray,
    const uint8_t* sourceAlpha,
    int sourceWidth,
    int sourceHeight,
    uint32_t sourceX16,
    uint32_t sourceY16,
    uint8_t& grayOut,
    uint8_t& alphaOut) {
  int x0 = static_cast<int>(sourceX16 >> 16);
  int y0 = static_cast<int>(sourceY16 >> 16);
  const uint32_t fx = sourceX16 & 0xFFFFu;
  const uint32_t fy = sourceY16 & 0xFFFFu;

  x0 = std::max(0, std::min(x0, sourceWidth - 1));
  y0 = std::max(0, std::min(y0, sourceHeight - 1));
  const int x1 = std::min(x0 + 1, sourceWidth - 1);
  const int y1 = std::min(y0 + 1, sourceHeight - 1);

  const auto lerp16 = [](uint32_t a, uint32_t b, uint32_t fraction) {
    return static_cast<uint32_t>(
        (static_cast<uint64_t>(a) * (65536u - fraction) +
         static_cast<uint64_t>(b) * fraction) >> 16);
  };
  const auto alphaAt = [&](int x, int y) -> uint32_t {
    return sourceAlpha[static_cast<size_t>(y) * sourceWidth + x];
  };
  const auto premultipliedAt = [&](int x, int y) -> uint32_t {
    const size_t index = static_cast<size_t>(y) * sourceWidth + x;
    return static_cast<uint32_t>(sourceGray[index]) * sourceAlpha[index];
  };

  const uint32_t alphaTop = lerp16(alphaAt(x0, y0), alphaAt(x1, y0), fx);
  const uint32_t alphaBottom = lerp16(alphaAt(x0, y1), alphaAt(x1, y1), fx);
  const uint32_t alpha = lerp16(alphaTop, alphaBottom, fy);

  const uint32_t premulTop = lerp16(
      premultipliedAt(x0, y0), premultipliedAt(x1, y0), fx);
  const uint32_t premulBottom = lerp16(
      premultipliedAt(x0, y1), premultipliedAt(x1, y1), fx);
  const uint32_t premul = lerp16(premulTop, premulBottom, fy);

  alphaOut = static_cast<uint8_t>(std::min<uint32_t>(255, alpha));
  grayOut = alphaOut == 0
      ? 255
      : static_cast<uint8_t>(std::min<uint32_t>(
            255, (premul + alphaOut / 2u) / alphaOut));
}

bool hasVisibleTransparency(const uint8_t* alpha, size_t count) {
  if (!alpha) return false;
  for (size_t i = 0; i < count; ++i) {
    if (alpha[i] != 255) return true;
  }
  return false;
}

bool resampleOpaque(
    const uint8_t* source,
    int sourceWidth,
    int sourceHeight,
    uint8_t* destination,
    const TaskControl& control) {
  if (!source || !destination || sourceWidth <= 0 || sourceHeight <= 0) return false;
  const CropRect crop = calculateCenterCrop(sourceWidth, sourceHeight);

  for (int y = 0; y < TARGET_HEIGHT; ++y) {
    if ((y & 31) == 0 && !pauseForForeground(control)) return false;
    const uint32_t sourceY16 =
        static_cast<uint32_t>(crop.y << 16) +
        static_cast<uint32_t>(
            (static_cast<uint64_t>(y) * crop.height << 16) / TARGET_HEIGHT);
    for (int x = 0; x < TARGET_WIDTH; ++x) {
      const uint32_t sourceX16 =
          static_cast<uint32_t>(crop.x << 16) +
          static_cast<uint32_t>(
              (static_cast<uint64_t>(x) * crop.width << 16) / TARGET_WIDTH);
      destination[static_cast<size_t>(y) * TARGET_WIDTH + x] =
          bilinearSample(source, sourceWidth, sourceHeight, sourceX16, sourceY16);
    }
  }
  return true;
}

bool resampleOverlay(
    const uint8_t* sourceGray,
    const uint8_t* sourceAlpha,
    int sourceWidth,
    int sourceHeight,
    uint8_t* destination,
    const TaskControl& control) {
  if (!sourceGray || !sourceAlpha || !destination) return false;
  std::memset(destination, 0xF0, OVERLAY_PAYLOAD_SIZE);

  int outX = 0;
  int outY = 0;
  int outWidth = 0;
  int outHeight = 0;
  calculateFitRect(sourceWidth, sourceHeight, outX, outY, outWidth, outHeight);

  for (int y = 0; y < outHeight; ++y) {
    if ((y & 31) == 0 && !pauseForForeground(control)) return false;
    const uint32_t sourceY16 = static_cast<uint32_t>(
        (static_cast<uint64_t>(y) * sourceHeight << 16) / outHeight);
    for (int x = 0; x < outWidth; ++x) {
      const uint32_t sourceX16 = static_cast<uint32_t>(
          (static_cast<uint64_t>(x) * sourceWidth << 16) / outWidth);
      uint8_t gray = 255;
      uint8_t alpha = 0;
      bilinearSampleOverlay(
          sourceGray,
          sourceAlpha,
          sourceWidth,
          sourceHeight,
          sourceX16,
          sourceY16,
          gray,
          alpha);
      destination[static_cast<size_t>(outY + y) * TARGET_WIDTH + outX + x] =
          static_cast<uint8_t>(((gray + 8u) / 17u) << 4) |
          static_cast<uint8_t>((alpha + 8u) / 17u);
    }
  }
  return true;
}

bool quantizeOpaqueToGc16(
    const uint8_t* gray,
    uint8_t* packed,
    const TaskControl& control) {
  if (!gray || !packed) return false;
  std::memset(packed, 0xFF, OPAQUE_PAYLOAD_SIZE);

  constexpr size_t ERROR_COUNT = TARGET_WIDTH + 2;
  auto* currentError = static_cast<int16_t*>(
      heap_caps_calloc(ERROR_COUNT, sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  auto* nextError = static_cast<int16_t*>(
      heap_caps_calloc(ERROR_COUNT, sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!currentError || !nextError) {
    if (currentError) heap_caps_free(currentError);
    if (nextError) heap_caps_free(nextError);
    return false;
  }

  auto distribute = [](int32_t error, int32_t weight) -> int16_t {
    const int32_t weighted = error * weight;
    return static_cast<int16_t>(
        weighted >= 0 ? (weighted + 8) / 16 : (weighted - 8) / 16);
  };

  for (int y = 0; y < TARGET_HEIGHT; ++y) {
    if ((y & 31) == 0 && !pauseForForeground(control)) {
      heap_caps_free(currentError);
      heap_caps_free(nextError);
      return false;
    }
    std::memset(nextError, 0, ERROR_COUNT * sizeof(int16_t));
    const bool leftToRight = (y & 1) == 0;
    for (int scan = 0; scan < TARGET_WIDTH; ++scan) {
      const int x = leftToRight ? scan : TARGET_WIDTH - 1 - scan;
      const int errorIndex = x + 1;
      int32_t adjusted =
          static_cast<int32_t>(gray[static_cast<size_t>(y) * TARGET_WIDTH + x]) * 16 +
          currentError[errorIndex];
      adjusted = std::max<int32_t>(0, std::min<int32_t>(4080, adjusted));
      int32_t level = (adjusted + 136) / 272;
      level = std::max<int32_t>(0, std::min<int32_t>(15, level));
      setPackedNibble(packed, TARGET_WIDTH, x, y, static_cast<uint8_t>(level));
      const int32_t error = adjusted - level * 272;

      if (leftToRight) {
        currentError[errorIndex + 1] += distribute(error, 7);
        nextError[errorIndex - 1] += distribute(error, 3);
        nextError[errorIndex] += distribute(error, 5);
        nextError[errorIndex + 1] += distribute(error, 1);
      } else {
        currentError[errorIndex - 1] += distribute(error, 7);
        nextError[errorIndex + 1] += distribute(error, 3);
        nextError[errorIndex] += distribute(error, 5);
        nextError[errorIndex - 1] += distribute(error, 1);
      }
    }
    std::swap(currentError, nextError);
  }

  heap_caps_free(currentError);
  heap_caps_free(nextError);
  return true;
}

bool computeSourceIdentity(
    const std::string& path,
    uint32_t& fileSize,
    uint32_t& fingerprint,
    uint32_t& pathHash) {
  FsFile file;
  if (!Storage.openFileForRead("SLPCACHE", path, file)) return false;
  fileSize = static_cast<uint32_t>(file.size());
  pathHash = fnv1aString(path);
  uint32_t hash = 2166136261u;
  hash = fnv1aUpdate(hash, reinterpret_cast<const uint8_t*>(&fileSize), sizeof(fileSize));

  uint8_t sample[512];
  const size_t firstCount = std::min<size_t>(sizeof(sample), fileSize);
  if (firstCount > 0) {
    file.seek(0);
    const int read = file.read(sample, firstCount);
    if (read > 0) hash = fnv1aUpdate(hash, sample, static_cast<size_t>(read));
  }
  if (fileSize > sizeof(sample)) {
    file.seek(fileSize - sizeof(sample));
    const int read = file.read(sample, sizeof(sample));
    if (read > 0) hash = fnv1aUpdate(hash, sample, static_cast<size_t>(read));
  }
  file.close();
  fingerprint = hash;
  return true;
}

std::string makeCachePath(uint32_t pathHash, uint32_t fingerprint) {
  char name[96];
  snprintf(
      name,
      sizeof(name),
      "%s/%08lx_%08lx.sgc",
      CACHE_DIR,
      static_cast<unsigned long>(pathHash),
      static_cast<unsigned long>(fingerprint));
  return name;
}

bool validateCacheHeader(
    const SleepCacheHeader& header,
    size_t fileSize = 0) {
  if (std::memcmp(header.magic, "SLPC", 4) != 0 ||
      header.version != CACHE_VERSION ||
      header.width != TARGET_WIDTH ||
      header.height != TARGET_HEIGHT) {
    return false;
  }
  const uint32_t expected = header.type == CACHE_OPAQUE
      ? static_cast<uint32_t>(OPAQUE_PAYLOAD_SIZE)
      : header.type == CACHE_OVERLAY
          ? static_cast<uint32_t>(OVERLAY_PAYLOAD_SIZE)
          : 0;
  if (expected == 0 || header.payloadSize != expected) return false;
  return fileSize == 0 || fileSize == sizeof(SleepCacheHeader) + expected;
}

bool validateCacheFile(const std::string& path, SleepCacheHeader& header) {
  FsFile file;
  if (!Storage.openFileForRead("SLPCACHE", path, file)) return false;
  const size_t size = file.size();
  if (!readExact(file, &header, sizeof(header)) ||
      !validateCacheHeader(header, size)) {
    file.close();
    return false;
  }

  uint8_t chunk[512];
  uint32_t hash = 2166136261u;
  size_t remaining = header.payloadSize;
  while (remaining > 0) {
    const size_t wanted = std::min(remaining, sizeof(chunk));
    const int count = file.read(chunk, wanted);
    if (count <= 0) {
      file.close();
      return false;
    }
    hash = fnv1aUpdate(hash, chunk, static_cast<size_t>(count));
    remaining -= static_cast<size_t>(count);
  }
  file.close();
  return hash == header.payloadFingerprint;
}

bool writeCache(
    const std::string& finalPath,
    CacheType type,
    const uint8_t* payload,
    size_t payloadSize,
    uint32_t sourceSize,
    uint32_t fingerprint,
    uint32_t pathHash) {
  if (!payload) return false;
  const std::string tempPath = finalPath + ".tmp";
  Storage.remove(tempPath.c_str());
  FsFile file;
  if (!Storage.openFileForWrite("SLPCACHE", tempPath, file)) return false;

  SleepCacheHeader header{};
  std::memcpy(header.magic, "SLPC", 4);
  header.version = CACHE_VERSION;
  header.type = type;
  header.width = TARGET_WIDTH;
  header.height = TARGET_HEIGHT;
  header.payloadSize = static_cast<uint32_t>(payloadSize);
  header.sourceSize = sourceSize;
  header.sourceFingerprint = fingerprint;
  header.sourcePathHash = pathHash;
  header.payloadFingerprint = fnv1aUpdate(2166136261u, payload, payloadSize);

  const bool ok = writeExact(file, &header, sizeof(header)) &&
                  writeExact(file, payload, payloadSize);
  file.flush();
  file.close();
  if (!ok) {
    Storage.remove(tempPath.c_str());
    return false;
  }
  Storage.remove(finalPath.c_str());
  if (!Storage.rename(tempPath.c_str(), finalPath.c_str())) {
    Storage.remove(tempPath.c_str());
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// JPEG decoding
// ---------------------------------------------------------------------------

struct JpegDecodeContext {
  uint8_t* gray = nullptr;
  int width = 0;
  int height = 0;
  TaskControl control{};
  bool failed = false;
  int maxX = 0;
  int maxY = 0;
};

void* jpegOpen(const char* filename, int32_t* size) {
  auto* file = new (std::nothrow) FsFile();
  if (!file || !Storage.openFileForRead("SLPJPG", filename, *file)) {
    delete file;
    return nullptr;
  }
  *size = static_cast<int32_t>(file->size());
  return file;
}

void jpegClose(void* handle) {
  auto* file = static_cast<FsFile*>(handle);
  if (file) {
    file->close();
    delete file;
  }
}

int32_t jpegRead(JPEGFILE* jpegFile, uint8_t* buffer, int32_t length) {
  auto* file = static_cast<FsFile*>(jpegFile->fHandle);
  if (!file) return 0;
  const int32_t count = file->read(buffer, length);
  if (count > 0) jpegFile->iPos += count;
  return std::max<int32_t>(0, count);
}

int32_t jpegSeek(JPEGFILE* jpegFile, int32_t position) {
  auto* file = static_cast<FsFile*>(jpegFile->fHandle);
  if (!file || !file->seek(position)) return -1;
  jpegFile->iPos = position;
  return position;
}

int jpegDraw(JPEGDRAW* draw) {
  auto* context = static_cast<JpegDecodeContext*>(draw->pUser);
  if (!context || !context->gray || shouldCancel(context->control)) return 0;
  if (!pauseForForeground(context->control)) return 0;

  const int validWidth = draw->iWidthUsed;
  const int stride = draw->iWidth;
  if (draw->x < 0 || draw->y < 0 ||
      draw->x + validWidth > context->width ||
      draw->y + draw->iHeight > context->height) {
    context->failed = true;
    return 0;
  }

  context->maxX = std::max(context->maxX, draw->x + validWidth);
  context->maxY = std::max(context->maxY, draw->y + draw->iHeight);

  const auto* pixels = reinterpret_cast<const uint8_t*>(draw->pPixels);
  for (int row = 0; row < draw->iHeight; ++row) {
    std::memcpy(
        context->gray +
            static_cast<size_t>(draw->y + row) * context->width + draw->x,
        pixels + static_cast<size_t>(row) * stride,
        validWidth);
  }
  return 1;
}

int chooseJpegScaleDenominator(int width, int height) {
  const int candidates[] = {8, 4, 2, 1};
  for (int denominator : candidates) {
    const int scaledWidth = (width + denominator - 1) / denominator;
    const int scaledHeight = (height + denominator - 1) / denominator;
    const CropRect crop = calculateCenterCrop(scaledWidth, scaledHeight);
    const size_t pixels = static_cast<size_t>(scaledWidth) * scaledHeight;
    if (crop.width >= TARGET_WIDTH && crop.height >= TARGET_HEIGHT &&
        pixels <= MAX_SOURCE_PIXELS) {
      return denominator;
    }
  }
  for (int denominator : candidates) {
    const size_t pixels =
        static_cast<size_t>((width + denominator - 1) / denominator) *
        ((height + denominator - 1) / denominator);
    if (pixels <= MAX_SOURCE_PIXELS) return denominator;
  }
  return 0;
}

bool decodeJpegToGray(
    const std::string& path,
    uint8_t*& grayOut,
    int& widthOut,
    int& heightOut,
    const TaskControl& control) {
  JPEGDEC* jpeg = new (std::nothrow) JPEGDEC();
  if (!jpeg) return false;
  int rc = jpeg->open(
      path.c_str(), jpegOpen, jpegClose, jpegRead, jpegSeek, jpegDraw);
  if (rc != 1) {
    delete jpeg;
    return false;
  }

  const int originalWidth = jpeg->getWidth();
  const int originalHeight = jpeg->getHeight();
  if (originalWidth <= 0 || originalHeight <= 0 ||
      originalWidth > MAX_SOURCE_DIMENSION || originalHeight > MAX_SOURCE_DIMENSION) {
    jpeg->close();
    delete jpeg;
    return false;
  }

  const int denominator = chooseJpegScaleDenominator(originalWidth, originalHeight);
  if (denominator == 0) {
    jpeg->close();
    delete jpeg;
    return false;
  }
  int option = 0;
  if (denominator == 2) option = JPEG_SCALE_HALF;
  else if (denominator == 4) option = JPEG_SCALE_QUARTER;
  else if (denominator == 8) option = JPEG_SCALE_EIGHTH;

  const int decodedWidth = (originalWidth + denominator - 1) / denominator;
  const int decodedHeight = (originalHeight + denominator - 1) / denominator;
  const size_t bytes = static_cast<size_t>(decodedWidth) * decodedHeight;
  auto* gray = static_cast<uint8_t*>(
      heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!gray) {
    jpeg->close();
    delete jpeg;
    return false;
  }
  std::memset(gray, 0xFF, bytes);

  JpegDecodeContext context;
  context.gray = gray;
  context.width = decodedWidth;
  context.height = decodedHeight;
  context.control = control;
  jpeg->setPixelType(EIGHT_BIT_GRAYSCALE);
  jpeg->setUserPointer(&context);

  LOG_INF(
      "SLPCACHE",
      "JPEG decode: %dx%d at 1/%d -> %dx%d",
      originalWidth,
      originalHeight,
      denominator,
      decodedWidth,
      decodedHeight);

  rc = jpeg->decode(0, 0, option);
  jpeg->close();
  delete jpeg;
  if (rc != 1 || context.failed || shouldCancel(control) ||
      context.maxX <= 0 || context.maxY <= 0) {
    heap_caps_free(gray);
    return false;
  }

  // Progressive JPEGs may internally force a coarser scale than requested.
  // Compact the actual decoded area so resampling never includes the unused
  // white tail of the originally allocated buffer.
  if (context.maxX != decodedWidth || context.maxY != decodedHeight) {
    const size_t compactBytes =
        static_cast<size_t>(context.maxX) * context.maxY;
    auto* compact = static_cast<uint8_t*>(
        heap_caps_malloc(compactBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!compact) {
      heap_caps_free(gray);
      return false;
    }
    for (int y = 0; y < context.maxY; ++y) {
      std::memcpy(
          compact + static_cast<size_t>(y) * context.maxX,
          gray + static_cast<size_t>(y) * decodedWidth,
          context.maxX);
    }
    heap_caps_free(gray);
    gray = compact;
  }

  grayOut = gray;
  widthOut = context.maxX;
  heightOut = context.maxY;
  return true;
}

// ---------------------------------------------------------------------------
// PNG decoding
// ---------------------------------------------------------------------------

struct PngInfo {
  int width = 0;
  int height = 0;
  uint8_t bitDepth = 0;
  uint8_t colorType = 0;
  bool hasTransparency = false;
};

uint32_t readBigEndian32(const uint8_t bytes[4]) {
  return (static_cast<uint32_t>(bytes[0]) << 24) |
         (static_cast<uint32_t>(bytes[1]) << 16) |
         (static_cast<uint32_t>(bytes[2]) << 8) |
         bytes[3];
}

bool inspectPng(const std::string& path, PngInfo& info) {
  FsFile file;
  if (!Storage.openFileForRead("SLPPNG", path, file)) return false;
  uint8_t signature[8];
  if (!readExact(file, signature, sizeof(signature)) ||
      std::memcmp(signature, "\x89PNG\r\n\x1a\n", 8) != 0) {
    file.close();
    return false;
  }

  bool foundIhdr = false;
  while (file.available()) {
    uint8_t lengthBytes[4];
    uint8_t type[4];
    if (!readExact(file, lengthBytes, 4) || !readExact(file, type, 4)) break;
    const uint32_t length = readBigEndian32(lengthBytes);
    if (std::memcmp(type, "IHDR", 4) == 0 && length >= 13) {
      uint8_t ihdr[13];
      if (!readExact(file, ihdr, sizeof(ihdr))) break;
      info.width = static_cast<int>(readBigEndian32(ihdr));
      info.height = static_cast<int>(readBigEndian32(ihdr + 4));
      info.bitDepth = ihdr[8];
      info.colorType = ihdr[9];
      info.hasTransparency = info.colorType == 4 || info.colorType == 6;
      if (length > sizeof(ihdr)) file.seekCur(length - sizeof(ihdr));
      foundIhdr = true;
    } else {
      if (std::memcmp(type, "tRNS", 4) == 0) info.hasTransparency = true;
      file.seekCur(length);
    }
    file.seekCur(4);  // CRC
    if (std::memcmp(type, "IDAT", 4) == 0) break;
  }
  file.close();
  return foundIhdr;
}

void* pngOpen(const char* filename, int32_t* size) {
  auto* file = new (std::nothrow) FsFile();
  if (!file || !Storage.openFileForRead("SLPPNG", filename, *file)) {
    delete file;
    return nullptr;
  }
  *size = static_cast<int32_t>(file->size());
  return file;
}

void pngClose(void* handle) {
  auto* file = static_cast<FsFile*>(handle);
  if (file) {
    file->close();
    delete file;
  }
}

int32_t pngRead(PNGFILE* pngFile, uint8_t* buffer, int32_t length) {
  auto* file = static_cast<FsFile*>(pngFile->fHandle);
  if (!file) return 0;
  return std::max(0, file->read(buffer, length));
}

int32_t pngSeek(PNGFILE* pngFile, int32_t position) {
  auto* file = static_cast<FsFile*>(pngFile->fHandle);
  if (!file || !file->seek(position)) return 0;
  return position;
}

struct PngDecodeContext {
  uint8_t* gray = nullptr;
  uint8_t* alpha = nullptr;
  int width = 0;
  int height = 0;
  TaskControl control{};
};

void convertPngLine(
    const PNGDRAW* draw,
    uint8_t* gray,
    uint8_t* alpha,
    int width) {
  const uint8_t* pixels = draw->pPixels;
  switch (draw->iPixelType) {
    case PNG_PIXEL_GRAYSCALE:
      std::memcpy(gray, pixels, width);
      std::memset(alpha, 0xFF, width);
      break;
    case PNG_PIXEL_TRUECOLOR:
      for (int x = 0; x < width; ++x) {
        const uint8_t* p = pixels + x * 3;
        gray[x] = static_cast<uint8_t>((77u * p[0] + 150u * p[1] + 29u * p[2]) >> 8);
        alpha[x] = 255;
      }
      break;
    case PNG_PIXEL_INDEXED:
      for (int x = 0; x < width; ++x) {
        const uint8_t index = pixels[x];
        if (draw->pPalette) {
          const uint8_t* p = draw->pPalette + index * 3;
          gray[x] = static_cast<uint8_t>((77u * p[0] + 150u * p[1] + 29u * p[2]) >> 8);
          alpha[x] = draw->iHasAlpha ? draw->pPalette[768 + index] : 255;
        } else {
          gray[x] = index;
          alpha[x] = 255;
        }
      }
      break;
    case PNG_PIXEL_GRAY_ALPHA:
      for (int x = 0; x < width; ++x) {
        gray[x] = pixels[x * 2];
        alpha[x] = pixels[x * 2 + 1];
      }
      break;
    case PNG_PIXEL_TRUECOLOR_ALPHA:
      for (int x = 0; x < width; ++x) {
        const uint8_t* p = pixels + x * 4;
        gray[x] = static_cast<uint8_t>((77u * p[0] + 150u * p[1] + 29u * p[2]) >> 8);
        alpha[x] = p[3];
      }
      break;
    default:
      std::memset(gray, 0xFF, width);
      std::memset(alpha, 0xFF, width);
      break;
  }
}

int pngDraw(PNGDRAW* draw) {
  auto* context = static_cast<PngDecodeContext*>(draw->pUser);
  if (!context || !context->gray || !context->alpha ||
      draw->y < 0 || draw->y >= context->height ||
      shouldCancel(context->control)) {
    return 0;
  }
  if (!pauseForForeground(context->control)) return 0;
  convertPngLine(
      draw,
      context->gray + static_cast<size_t>(draw->y) * context->width,
      context->alpha + static_cast<size_t>(draw->y) * context->width,
      context->width);
  return 1;
}

bool decodePng(
    const std::string& path,
    const PngInfo& info,
    uint8_t*& grayOut,
    uint8_t*& alphaOut,
    const TaskControl& control) {
  if (info.width <= 0 || info.height <= 0 || info.bitDepth != 8 ||
      info.width > MAX_SOURCE_DIMENSION || info.height > MAX_SOURCE_DIMENSION ||
      static_cast<size_t>(info.width) * info.height > MAX_SOURCE_PIXELS) {
    LOG_ERR(
        "SLPCACHE",
        "Unsupported PNG geometry/depth: %dx%d, %u-bit",
        info.width,
        info.height,
        static_cast<unsigned>(info.bitDepth));
    return false;
  }

  const size_t pixels = static_cast<size_t>(info.width) * info.height;
  auto* gray = static_cast<uint8_t*>(
      heap_caps_malloc(pixels, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  auto* alpha = static_cast<uint8_t*>(
      heap_caps_malloc(pixels, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!gray || !alpha) {
    if (gray) heap_caps_free(gray);
    if (alpha) heap_caps_free(alpha);
    return false;
  }

  PNG* png = new (std::nothrow) PNG();
  if (!png) {
    heap_caps_free(gray);
    heap_caps_free(alpha);
    return false;
  }
  const int rcOpen = png->open(
      path.c_str(), pngOpen, pngClose, pngRead, pngSeek, pngDraw);
  if (rcOpen != PNG_SUCCESS) {
    delete png;
    heap_caps_free(gray);
    heap_caps_free(alpha);
    return false;
  }

  const int bytesPerPixel =
      png->getPixelType() == PNG_PIXEL_TRUECOLOR_ALPHA ? 4 :
      png->getPixelType() == PNG_PIXEL_TRUECOLOR ? 3 :
      png->getPixelType() == PNG_PIXEL_GRAY_ALPHA ? 2 : 1;
  const int requiredInternal = ((info.width * bytesPerPixel + 1) * 2) + 32;
#ifdef PNG_MAX_BUFFERED_PIXELS
  if (requiredInternal > PNG_MAX_BUFFERED_PIXELS) {
    LOG_ERR(
        "SLPCACHE",
        "PNG row needs %d bytes, PNG_MAX_BUFFERED_PIXELS=%d",
        requiredInternal,
        PNG_MAX_BUFFERED_PIXELS);
    png->close();
    delete png;
    heap_caps_free(gray);
    heap_caps_free(alpha);
    return false;
  }
#endif

  PngDecodeContext context;
  context.gray = gray;
  context.alpha = alpha;
  context.width = info.width;
  context.height = info.height;
  context.control = control;
  const int rc = png->decode(&context, 0);
  png->close();
  delete png;
  if (rc != PNG_SUCCESS || shouldCancel(control)) {
    heap_caps_free(gray);
    heap_caps_free(alpha);
    return false;
  }

  grayOut = gray;
  alphaOut = alpha;
  return true;
}

// ---------------------------------------------------------------------------
// BMP decoding
// ---------------------------------------------------------------------------

bool decodeBmpToTarget(
    const std::string& path,
    uint8_t* targetGray,
    const TaskControl& control) {
  FsFile file;
  if (!Storage.openFileForRead("SLPBMP", path, file)) return false;
  Bitmap bitmap(file, false);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    file.close();
    return false;
  }
  const int sourceWidth = bitmap.getWidth();
  const int sourceHeight = bitmap.getHeight();
  const CropRect crop = calculateCenterCrop(sourceWidth, sourceHeight);
  auto* raw = static_cast<uint8_t*>(
      heap_caps_malloc(bitmap.getRowBytes(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  auto* luminance = static_cast<uint8_t*>(
      heap_caps_malloc(sourceWidth, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!raw || !luminance) {
    if (raw) heap_caps_free(raw);
    if (luminance) heap_caps_free(luminance);
    file.close();
    return false;
  }

  std::memset(targetGray, 0xFF, TARGET_PIXELS);
  for (int fileRow = 0; fileRow < sourceHeight; ++fileRow) {
    if ((fileRow & 31) == 0 && !pauseForForeground(control)) {
      heap_caps_free(raw);
      heap_caps_free(luminance);
      file.close();
      return false;
    }
    if (bitmap.readNextLuminanceRow(luminance, raw) != BmpReaderError::Ok) {
      heap_caps_free(raw);
      heap_caps_free(luminance);
      file.close();
      return false;
    }
    const int sourceY = bitmap.isTopDown()
        ? fileRow
        : sourceHeight - 1 - fileRow;
    if (sourceY < crop.y || sourceY >= crop.y + crop.height) continue;

    const int relativeY = sourceY - crop.y;
    const int destinationStart = static_cast<int>(
        (static_cast<int64_t>(relativeY) * TARGET_HEIGHT + crop.height - 1) /
        crop.height);
    const int destinationEnd = static_cast<int>(
        (static_cast<int64_t>(relativeY + 1) * TARGET_HEIGHT + crop.height - 1) /
        crop.height);
    for (int destinationY = destinationStart;
         destinationY < destinationEnd && destinationY < TARGET_HEIGHT;
         ++destinationY) {
      uint8_t* destinationRow =
          targetGray + static_cast<size_t>(destinationY) * TARGET_WIDTH;
      for (int x = 0; x < TARGET_WIDTH; ++x) {
        const int sourceX = crop.x + static_cast<int>(
            (static_cast<int64_t>(x) * crop.width) / TARGET_WIDTH);
        destinationRow[x] = luminance[std::min(sourceX, sourceWidth - 1)];
      }
    }
  }

  heap_caps_free(raw);
  heap_caps_free(luminance);
  file.close();
  return true;
}

uint8_t sampleReaderPage(
    const uint8_t* physicalFrame,
    uint8_t orientation,
    int targetX,
    int targetY,
    int clearBottomPixels) {
  if (!physicalFrame) return 15;

  const bool portrait = orientation == 0 || orientation == 2;
  const int sourceWidth = portrait ? TARGET_WIDTH : HalDisplay::DISPLAY_WIDTH;
  const int sourceHeight = portrait ? TARGET_HEIGHT : HalDisplay::DISPLAY_HEIGHT;

  int fitX = 0;
  int fitY = 0;
  int fitWidth = 0;
  int fitHeight = 0;
  calculateFitRect(
      sourceWidth, sourceHeight, fitX, fitY, fitWidth, fitHeight);
  if (targetX < fitX || targetY < fitY ||
      targetX >= fitX + fitWidth || targetY >= fitY + fitHeight) {
    return 15;
  }

  const int sourceX = std::min(
      sourceWidth - 1,
      static_cast<int>(
          (static_cast<int64_t>(targetX - fitX) * sourceWidth) / fitWidth));
  const int sourceY = std::min(
      sourceHeight - 1,
      static_cast<int>(
          (static_cast<int64_t>(targetY - fitY) * sourceHeight) / fitHeight));

  if (clearBottomPixels > 0 &&
      sourceY >= sourceHeight - std::min(sourceHeight, clearBottomPixels)) {
    return 15;
  }

  int physicalX = 0;
  int physicalY = 0;
  switch (orientation) {
    case 1:  // LandscapeClockwise
      physicalX = HalDisplay::DISPLAY_WIDTH - 1 - sourceX;
      physicalY = HalDisplay::DISPLAY_HEIGHT - 1 - sourceY;
      break;
    case 2:  // PortraitInverted
      physicalX = HalDisplay::DISPLAY_WIDTH - 1 - sourceY;
      physicalY = sourceX;
      break;
    case 3:  // LandscapeCounterClockwise
      physicalX = sourceX;
      physicalY = sourceY;
      break;
    case 0:  // Portrait
    default:
      physicalX = sourceY;
      physicalY = HalDisplay::DISPLAY_HEIGHT - 1 - sourceX;
      break;
  }

  if (physicalX < 0 || physicalX >= HalDisplay::DISPLAY_WIDTH ||
      physicalY < 0 || physicalY >= HalDisplay::DISPLAY_HEIGHT) {
    return 15;
  }
  const uint8_t epd = physicalFrame[
      static_cast<size_t>(physicalY) * HalDisplay::DISPLAY_WIDTH + physicalX];
  return static_cast<uint8_t>(15 - std::min<uint8_t>(3, epd) * 5);
}

bool isSupportedSleepImage(const std::string& name) {
  return FsHelpers::hasBmpExtension(name) ||
         FsHelpers::hasJpgExtension(name) ||
         FsHelpers::hasPngExtension(name);
}

}  // namespace

SleepImageManager& SleepImageManager::getInstance() {
  static SleepImageManager instance;
  return instance;
}

void SleepImageManager::begin() {
  preparedReady = false;
  preparationAttempted = false;
  preparedCachePath.clear();
  cancelRequested = false;

  Storage.ensureDirectoryExists("/.crosspoint");
  Storage.ensureDirectoryExists(CACHE_DIR);

  // A reset or forced sleep may interrupt a background write. Temporary files
  // are never valid caches and are cleaned on the next boot.
  {
    std::vector<std::string> temporaryFiles;
    auto cacheDir = Storage.open(CACHE_DIR);
    if (cacheDir && cacheDir.isDirectory()) {
      char name[160];
      for (auto file = cacheDir.openNextFile(); file; file = cacheDir.openNextFile()) {
        if (file.isDirectory()) continue;
        file.getName(name, sizeof(name));
        std::string filename(name);
        if (filename.size() >= 4 &&
            filename.compare(filename.size() - 4, 4, ".tmp") == 0) {
          temporaryFiles.emplace_back(std::string(CACHE_DIR) + "/" + filename);
        }
      }
      cacheDir.close();
    }
    for (const auto& path : temporaryFiles) Storage.remove(path.c_str());
  }

  lastUserActivityMs = millis();
  scanCandidates();
  if (candidates.empty()) {
    LOG_INF("SLPCACHE", "No BMP/JPG/PNG files found in /.sleep or /sleep");
    return;
  }

  const size_t recentWindow = std::min<size_t>(
      APP_STATE.recentSleepFill,
      candidates.size() > 0 ? candidates.size() - 1 : 0);
  selectedCandidate = static_cast<size_t>(random(candidates.size()));
  for (uint8_t attempt = 0;
       attempt < 20 && APP_STATE.isRecentSleep(
           static_cast<uint16_t>(selectedCandidate),
           static_cast<uint8_t>(recentWindow));
       ++attempt) {
    selectedCandidate = static_cast<size_t>(random(candidates.size()));
  }
  LOG_INF("SLPCACHE", "Selected next sleep image: %s", candidates[selectedCandidate].c_str());
}

void SleepImageManager::scanCandidates() {
  candidates.clear();
  const char* directories[] = {"/.sleep", "/sleep"};
  for (const char* directory : directories) {
    auto dir = Storage.open(directory);
    if (!dir || !dir.isDirectory()) continue;
    char name[500];
    for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
      if (file.isDirectory()) continue;
      file.getName(name, sizeof(name));
      std::string filename(name);
      if (filename.empty() || filename[0] == '.' || !isSupportedSleepImage(filename)) continue;
      candidates.emplace_back(std::string(directory) + "/" + filename);
    }
    dir.close();
    if (!candidates.empty()) break;
  }

  if (candidates.empty()) {
    const char* roots[] = {"/sleep.bmp", "/sleep.jpg", "/sleep.jpeg", "/sleep.png"};
    for (const char* path : roots) {
      if (Storage.exists(path)) candidates.emplace_back(path);
    }
  }
}

void SleepImageManager::noteUserActivity() {
  lastUserActivityMs = millis();
}

void SleepImageManager::loop(uint32_t idleMs, bool foregroundBusy) {
  const bool customMayBeNeeded =
      SETTINGS.sleepScreen == CrossPointSettings::CUSTOM ||
      SETTINGS.sleepScreen == CrossPointSettings::COVER_CUSTOM;
  if (!customMayBeNeeded || candidates.empty() || preparedReady ||
      preparationAttempted || taskRunning || foregroundBusy ||
      idleMs < START_AFTER_IDLE_MS) {
    return;
  }

  cancelRequested = false;
  preparationAttempted = true;
  taskRunning = true;
  const BaseType_t created = xTaskCreatePinnedToCore(
      taskTrampoline,
      "SleepImagePrep",
      12288,
      this,
      1,
      &taskHandle,
      0);
  if (created != pdPASS) {
    preparationAttempted = false;
    taskRunning = false;
    taskHandle = nullptr;
    LOG_ERR("SLPCACHE", "Failed to create background preparation task");
  }
}

void SleepImageManager::taskTrampoline(void* parameter) {
  static_cast<SleepImageManager*>(parameter)->prepareTask();
}

void SleepImageManager::prepareTask() {
  const uint32_t started = millis();
  const size_t count = candidates.size();
  bool success = false;
  std::string cachePath;
  size_t successfulIndex = selectedCandidate;

  for (size_t attempt = 0; attempt < count && !cancelRequested; ++attempt) {
    const size_t index = (selectedCandidate + attempt) % count;
    LOG_INF("SLPCACHE", "Preparing in background: %s", candidates[index].c_str());
    if (prepareCandidate(index, cachePath)) {
      success = true;
      successfulIndex = index;
      break;
    }
    LOG_ERR("SLPCACHE", "Preparation failed: %s", candidates[index].c_str());
  }

  if (success && !cancelRequested) {
    preparedCachePath = cachePath;
    selectedCandidate = successfulIndex;
    preparedReady = true;
    LOG_INF(
        "SLPCACHE",
        "Prepared successfully in %lu ms: %s",
        millis() - started,
        cachePath.c_str());
  } else if (!cancelRequested) {
    LOG_ERR(
        "SLPCACHE",
        "No custom sleep image could be prepared; retrying after next boot");
  }

  taskENTER_CRITICAL(&stateMux);
  taskRunning = false;
  taskHandle = nullptr;
  taskEXIT_CRITICAL(&stateMux);
  vTaskDelete(nullptr);
}

bool SleepImageManager::prepareCandidate(
    size_t candidateIndex,
    std::string& cachePathOut) {
  if (candidateIndex >= candidates.size()) return false;
  const std::string& sourcePath = candidates[candidateIndex];
  uint32_t sourceSize = 0;
  uint32_t fingerprint = 0;
  uint32_t pathHash = 0;
  if (!computeSourceIdentity(sourcePath, sourceSize, fingerprint, pathHash)) return false;
  const std::string cachePath = makeCachePath(pathHash, fingerprint);

  SleepCacheHeader existing{};
  if (validateCacheFile(cachePath, existing) &&
      existing.sourceSize == sourceSize &&
      existing.sourceFingerprint == fingerprint &&
      existing.sourcePathHash == pathHash) {
    LOG_INF("SLPCACHE", "Cache hit: %s", cachePath.c_str());
    cachePathOut = cachePath;
    return true;
  }

  TaskControl control{&cancelRequested, &lastUserActivityMs};
  bool overlay = false;
  uint8_t* outputGray = nullptr;
  uint8_t* overlayPayload = nullptr;
  uint8_t* sourceGray = nullptr;
  uint8_t* sourceAlpha = nullptr;
  int sourceWidth = 0;
  int sourceHeight = 0;
  bool decoded = false;

  if (FsHelpers::hasJpgExtension(sourcePath)) {
    decoded = decodeJpegToGray(
        sourcePath, sourceGray, sourceWidth, sourceHeight, control);
    if (decoded) {
      outputGray = static_cast<uint8_t*>(
          heap_caps_malloc(TARGET_PIXELS, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      decoded = outputGray && resampleOpaque(
          sourceGray, sourceWidth, sourceHeight, outputGray, control);
    }
  } else if (FsHelpers::hasPngExtension(sourcePath)) {
    PngInfo info;
    decoded = inspectPng(sourcePath, info);
    if (decoded) {
      sourceWidth = info.width;
      sourceHeight = info.height;
      decoded = decodePng(sourcePath, info, sourceGray, sourceAlpha, control);
    }
    overlay = decoded && info.hasTransparency && hasVisibleTransparency(
        sourceAlpha, static_cast<size_t>(sourceWidth) * sourceHeight);
    if (decoded && overlay) {
      overlayPayload = static_cast<uint8_t*>(
          heap_caps_malloc(OVERLAY_PAYLOAD_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      decoded = overlayPayload && resampleOverlay(
          sourceGray, sourceAlpha, sourceWidth, sourceHeight, overlayPayload, control);
    } else if (decoded) {
      outputGray = static_cast<uint8_t*>(
          heap_caps_malloc(TARGET_PIXELS, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      decoded = outputGray && resampleOpaque(
          sourceGray, sourceWidth, sourceHeight, outputGray, control);
    }
  } else if (FsHelpers::hasBmpExtension(sourcePath)) {
    outputGray = static_cast<uint8_t*>(
        heap_caps_malloc(TARGET_PIXELS, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    decoded = outputGray && decodeBmpToTarget(sourcePath, outputGray, control);
  }

  if (sourceGray) heap_caps_free(sourceGray);
  if (sourceAlpha) heap_caps_free(sourceAlpha);
  if (!decoded || cancelRequested) {
    if (outputGray) heap_caps_free(outputGray);
    if (overlayPayload) heap_caps_free(overlayPayload);
    return false;
  }

  bool written = false;
  if (overlay) {
    written = writeCache(
        cachePath,
        CACHE_OVERLAY,
        overlayPayload,
        OVERLAY_PAYLOAD_SIZE,
        sourceSize,
        fingerprint,
        pathHash);
  } else {
    auto* packed = static_cast<uint8_t*>(
        heap_caps_malloc(OPAQUE_PAYLOAD_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (packed && quantizeOpaqueToGc16(outputGray, packed, control) && !cancelRequested) {
      written = writeCache(
          cachePath,
          CACHE_OPAQUE,
          packed,
          OPAQUE_PAYLOAD_SIZE,
          sourceSize,
          fingerprint,
          pathHash);
    }
    if (packed) heap_caps_free(packed);
  }

  if (outputGray) heap_caps_free(outputGray);
  if (overlayPayload) heap_caps_free(overlayPayload);
  if (written) cachePathOut = cachePath;
  return written;
}

void SleepImageManager::captureReaderFrame(
    const uint8_t* physicalFrameBuffer,
    uint8_t readerOrientation) {
  if (!physicalFrameBuffer) return;
  if (!readerFrame) {
    readerFrame = static_cast<uint8_t*>(
        heap_caps_malloc(HalDisplay::BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (!readerFrame) {
    readerFrameValid = false;
    return;
  }
  std::memcpy(readerFrame, physicalFrameBuffer, HalDisplay::BUFFER_SIZE);
  readerFrameOrientation = readerOrientation <= 3 ? readerOrientation : 0;
  readerFrameValid = true;
  LOG_DBG(
      "SLPCACHE",
      "Captured current reader framebuffer (orientation=%u)",
      static_cast<unsigned>(readerFrameOrientation));
}

void SleepImageManager::cancelForSleep() {
  cancelRequested = true;
  TaskHandle_t handle = nullptr;
  taskENTER_CRITICAL(&stateMux);
  handle = taskHandle;
  taskHandle = nullptr;
  taskRunning = false;
  taskEXIT_CRITICAL(&stateMux);
  if (handle && handle != xTaskGetCurrentTaskHandle()) {
    vTaskDelete(handle);
  }
}

bool SleepImageManager::displayPreparedOrPrevious(
    HalDisplay& halDisplay,
    bool readerContext,
    bool rotate180,
    uint8_t transparentBackgroundMode) {
  cancelForSleep();

  std::string preferred;
  if (preparedReady) preferred = preparedCachePath;

  if (!preferred.empty() && displayCache(
          halDisplay,
          preferred,
          readerContext,
          rotate180,
          transparentBackgroundMode)) {
    Storage.writeFile(LAST_CACHE_FILE, String(preferred.c_str()));
    APP_STATE.pushRecentSleep(static_cast<uint16_t>(selectedCandidate));
    APP_STATE.saveToFile();
    LOG_INF("SLP", "Using prepared sleep cache: %s", preferred.c_str());
    return true;
  }

  const String last = Storage.readFile(LAST_CACHE_FILE);
  if (last.length() > 0) {
    std::string path(last.c_str());
    while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) path.pop_back();
    if (displayCache(
            halDisplay,
            path,
            readerContext,
            rotate180,
            transparentBackgroundMode)) {
      LOG_INF("SLP", "Using previous valid sleep cache: %s", path.c_str());
      return true;
    }
  }

  LOG_INF("SLP", "No valid prepared sleep cache; using built-in fallback");
  return false;
}

bool SleepImageManager::displayCache(
    HalDisplay& halDisplay,
    const std::string& cachePath,
    bool readerContext,
    bool rotate180,
    uint8_t transparentBackgroundMode) {
  FsFile file;
  if (!Storage.openFileForRead("SLPCACHE", cachePath, file)) return false;
  SleepCacheHeader header{};
  if (!readExact(file, &header, sizeof(header)) ||
      !validateCacheHeader(header, file.size())) {
    file.close();
    return false;
  }

  if (header.type == CACHE_OPAQUE) {
    auto* logical = static_cast<uint8_t*>(
        heap_caps_malloc(OPAQUE_PAYLOAD_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!logical || !readExact(file, logical, OPAQUE_PAYLOAD_SIZE) ||
        fnv1aUpdate(2166136261u, logical, OPAQUE_PAYLOAD_SIZE) !=
            header.payloadFingerprint) {
      if (logical) heap_caps_free(logical);
      file.close();
      return false;
    }
    file.close();
    const bool result = halDisplay.showGc16LogicalBuffer(
        logical, OPAQUE_PAYLOAD_SIZE, true, rotate180);
    heap_caps_free(logical);
    return result;
  }

  auto* overlay = static_cast<uint8_t*>(
      heap_caps_malloc(OVERLAY_PAYLOAD_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  auto* logical = static_cast<uint8_t*>(
      heap_caps_malloc(OPAQUE_PAYLOAD_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!overlay || !logical || !readExact(file, overlay, OVERLAY_PAYLOAD_SIZE) ||
      fnv1aUpdate(2166136261u, overlay, OVERLAY_PAYLOAD_SIZE) !=
          header.payloadFingerprint) {
    if (overlay) heap_caps_free(overlay);
    if (logical) heap_caps_free(logical);
    file.close();
    return false;
  }
  file.close();
  std::memset(logical, 0xFF, OPAQUE_PAYLOAD_SIZE);

  const bool useReaderPage =
      transparentBackgroundMode ==
          CrossPointSettings::TRANSPARENT_SLEEP_CURRENT_READING_PAGE &&
      readerContext && readerFrameValid && readerFrame != nullptr;
  const int readerSnapshotClearBottom = useReaderPage
      ? static_cast<int>(UITheme::getInstance().getStatusBarHeight()) + 2
      : 0;

  for (int y = 0; y < TARGET_HEIGHT; ++y) {
    for (int x = 0; x < TARGET_WIDTH; ++x) {
      const uint8_t background = useReaderPage
          ? sampleReaderPage(readerFrame, readerFrameOrientation, x, y, readerSnapshotClearBottom)
          : 15;
      const uint8_t pixel = overlay[static_cast<size_t>(y) * TARGET_WIDTH + x];
      const uint8_t foreground = static_cast<uint8_t>(pixel >> 4);
      const uint8_t alpha = static_cast<uint8_t>(pixel & 0x0F);
      const uint8_t result = static_cast<uint8_t>(
          (foreground * alpha + background * (15 - alpha) + 7) / 15);
      setPackedNibble(logical, TARGET_WIDTH, x, y, result);
    }
    if ((y & 31) == 31) vTaskDelay(1);
  }

  LOG_INF(
      "SLP",
      "Transparent PNG background: %s",
      useReaderPage ? "current reading page" : "white");
  const bool result = halDisplay.showGc16LogicalBuffer(
      logical, OPAQUE_PAYLOAD_SIZE, true, rotate180);
  heap_caps_free(overlay);
  heap_caps_free(logical);
  return result;
}
