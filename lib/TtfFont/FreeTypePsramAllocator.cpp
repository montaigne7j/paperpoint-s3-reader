#include <FreeTypePsramAllocator.h>

#include <Logging.h>
#include <esp_heap_caps.h>

#if __has_include(<esp_memory_utils.h>)
#include <esp_memory_utils.h>
#define CROSSPOINT_HAVE_ESP_MEMORY_UTILS 1
#else
#define CROSSPOINT_HAVE_ESP_MEMORY_UTILS 0
#endif

#include <cstring>

#include FT_MODULE_H

namespace CrossPointFtPsramAllocator {
namespace {
constexpr long kPsramThresholdBytes = 512;
constexpr uint32_t kDetailedLogLimit = 160;
constexpr long kDetailedLogMinSize = 512;

struct AllocStats {
  uint32_t allocCount = 0;
  uint32_t reallocCount = 0;
  uint32_t freeCount = 0;
  uint32_t failCount = 0;
  uint32_t psramAllocCount = 0;
  uint32_t internalAllocCount = 0;
  uint32_t unknownAllocCount = 0;
  uint64_t psramRequestedBytes = 0;
  uint64_t internalRequestedBytes = 0;
  uint64_t unknownRequestedBytes = 0;
  uint32_t detailLogs = 0;
};

AllocStats gStats;
FT_MemoryRec_ gMemoryRec{};
bool gMemoryReady = false;

const char* ptrLocation(const void* ptr) {
  if (ptr == nullptr) return "NULL";
#if CROSSPOINT_HAVE_ESP_MEMORY_UTILS
  if (esp_ptr_external_ram(ptr)) return "PSRAM";
  if (esp_ptr_internal(ptr)) return "INTERNAL";
#endif
  return "UNKNOWN";
}

void recordAlloc(const void* ptr, const long size) {
  const char* loc = ptrLocation(ptr);
  if (std::strcmp(loc, "PSRAM") == 0) {
    ++gStats.psramAllocCount;
    gStats.psramRequestedBytes += static_cast<uint64_t>(size > 0 ? size : 0);
  } else if (std::strcmp(loc, "INTERNAL") == 0) {
    ++gStats.internalAllocCount;
    gStats.internalRequestedBytes += static_cast<uint64_t>(size > 0 ? size : 0);
  } else {
    ++gStats.unknownAllocCount;
    gStats.unknownRequestedBytes += static_cast<uint64_t>(size > 0 ? size : 0);
  }
}

void maybeLogDetail(const char* op, const long oldSize, const long newSize, const void* oldPtr, const void* newPtr) {
  const long logSize = newSize > 0 ? newSize : oldSize;
  if (logSize < kDetailedLogMinSize || gStats.detailLogs >= kDetailedLogLimit) return;
  ++gStats.detailLogs;
  LOG_INF(
      "FTALLOC",
      "FT %s: oldSize=%ld newSize=%ld old=%p oldLoc=%s new=%p newLoc=%s",
      op,
      oldSize,
      newSize,
      oldPtr,
      ptrLocation(oldPtr),
      newPtr,
      ptrLocation(newPtr)
  );
}

void* allocImpl(FT_Memory /*memory*/, long size) {
  if (size <= 0) return nullptr;
  ++gStats.allocCount;

  void* ptr = nullptr;
  if (size >= kPsramThresholdBytes) {
    ptr = heap_caps_malloc(static_cast<size_t>(size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr == nullptr) {
      ptr = heap_caps_malloc(static_cast<size_t>(size), MALLOC_CAP_8BIT);
    }
  } else {
    ptr = heap_caps_malloc(static_cast<size_t>(size), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (ptr == nullptr) {
      ptr = heap_caps_malloc(static_cast<size_t>(size), MALLOC_CAP_8BIT);
    }
  }

  if (ptr == nullptr) {
    ++gStats.failCount;
    LOG_ERR("FTALLOC", "FT alloc failed: size=%ld", size);
    return nullptr;
  }

  recordAlloc(ptr, size);
  maybeLogDetail("alloc", 0, size, nullptr, ptr);
  return ptr;
}

void freeImpl(FT_Memory /*memory*/, void* block) {
  if (block == nullptr) return;
  ++gStats.freeCount;
  maybeLogDetail("free", 0, 0, block, nullptr);
  heap_caps_free(block);
}

void* reallocImpl(FT_Memory memory, long curSize, long newSize, void* block) {
  ++gStats.reallocCount;
  if (newSize <= 0) {
    freeImpl(memory, block);
    return nullptr;
  }
  if (block == nullptr) {
    return allocImpl(memory, newSize);
  }

  void* next = allocImpl(memory, newSize);
  if (next == nullptr) {
    ++gStats.failCount;
    maybeLogDetail("realloc-fail", curSize, newSize, block, nullptr);
    return nullptr;
  }

  if (curSize > 0) {
    const size_t copyBytes = static_cast<size_t>(curSize < newSize ? curSize : newSize);
    std::memcpy(next, block, copyBytes);
  }
  maybeLogDetail("realloc", curSize, newSize, block, next);
  heap_caps_free(block);
  return next;
}

void ensureMemoryReady() {
  if (gMemoryReady) return;
  gMemoryRec.user = nullptr;
  gMemoryRec.alloc = allocImpl;
  gMemoryRec.free = freeImpl;
  gMemoryRec.realloc = reallocImpl;
  gMemoryReady = true;
}
}  // namespace

void resetSummary() { gStats = AllocStats{}; }

void logSummary(const char* context) {
  LOG_INF(
      "FTALLOC",
      "FT allocator summary: context=%s allocCount=%lu reallocCount=%lu freeCount=%lu failCount=%lu psramAllocs=%lu internalAllocs=%lu unknownAllocs=%lu psramBytes=%llu internalBytes=%llu unknownBytes=%llu",
      context != nullptr ? context : "",
      static_cast<unsigned long>(gStats.allocCount),
      static_cast<unsigned long>(gStats.reallocCount),
      static_cast<unsigned long>(gStats.freeCount),
      static_cast<unsigned long>(gStats.failCount),
      static_cast<unsigned long>(gStats.psramAllocCount),
      static_cast<unsigned long>(gStats.internalAllocCount),
      static_cast<unsigned long>(gStats.unknownAllocCount),
      static_cast<unsigned long long>(gStats.psramRequestedBytes),
      static_cast<unsigned long long>(gStats.internalRequestedBytes),
      static_cast<unsigned long long>(gStats.unknownRequestedBytes)
  );
}

FT_Error newLibrary(FT_Library* library) {
  if (library == nullptr) return FT_Err_Invalid_Argument;
  ensureMemoryReady();
  resetSummary();

  FT_Error error = FT_New_Library(&gMemoryRec, library);
  if (error != 0) {
    LOG_ERR("FTALLOC", "FT_New_Library failed: error=0x%02X", static_cast<unsigned>(error));
    return error;
  }

  FT_Add_Default_Modules(*library);
  logSummary("after-new-library");
  return FT_Err_Ok;
}

}  // namespace CrossPointFtPsramAllocator
