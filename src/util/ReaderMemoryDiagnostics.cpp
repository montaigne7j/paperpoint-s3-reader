#include "ReaderMemoryDiagnostics.h"

#include <Logging.h>

#if CROSSPOINT_PAPERS3
#include <esp_heap_caps.h>
#if __has_include(<esp_memory_utils.h>)
#include <esp_memory_utils.h>
#define CROSSPOINT_MEMDIAG_HAS_ESP_MEMORY_UTILS 1
#else
#define CROSSPOINT_MEMDIAG_HAS_ESP_MEMORY_UTILS 0
#endif
#endif

#include <cstdlib>

namespace {

long deltaU32(const uint32_t after, const uint32_t before) {
  return static_cast<long>(after) - static_cast<long>(before);
}

uint32_t absDeltaU32(const uint32_t a, const uint32_t b) {
  return a >= b ? a - b : b - a;
}

}  // namespace

namespace ReaderMemoryDiagnostics {

ReaderMemoryDiagTrace capture() {
  ReaderMemoryDiagTrace trace{};
#if CROSSPOINT_PAPERS3
  trace.internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  trace.internalMaxAlloc = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  trace.psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  trace.psramMaxAlloc = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
  return trace;
}

const char* bufferLocation(const void* ptr) {
  if (!ptr) return "NULL";
#if CROSSPOINT_PAPERS3 && CROSSPOINT_MEMDIAG_HAS_ESP_MEMORY_UTILS
  if (esp_ptr_external_ram(ptr)) return "PSRAM";
  if (esp_ptr_internal(ptr)) return "INTERNAL";
#endif
  return "UNKNOWN";
}

void logCheckpoint(const char* phase) {
  const ReaderMemoryDiagTrace trace = capture();
  LOG_INF(
      "MEMD",
      "Internal heap checkpoint: phase=%s internalFree=%lu internalMaxAlloc=%lu psramFree=%lu psramMaxAlloc=%lu",
      phase ? phase : "?",
      static_cast<unsigned long>(trace.internalFree),
      static_cast<unsigned long>(trace.internalMaxAlloc),
      static_cast<unsigned long>(trace.psramFree),
      static_cast<unsigned long>(trace.psramMaxAlloc));
}

void logLargeBuffer(const char* tag, const void* ptr, const size_t size) {
  const ReaderMemoryDiagTrace trace = capture();
  LOG_INF(
      "MEMD",
      "Large buffer alloc: tag=%s ptr=%p size=%u loc=%s internalFree=%lu internalMaxAlloc=%lu psramFree=%lu psramMaxAlloc=%lu",
      tag ? tag : "?",
      ptr,
      static_cast<unsigned>(size),
      bufferLocation(ptr),
      static_cast<unsigned long>(trace.internalFree),
      static_cast<unsigned long>(trace.internalMaxAlloc),
      static_cast<unsigned long>(trace.psramFree),
      static_cast<unsigned long>(trace.psramMaxAlloc));
}

void logDelta(const char* phase, const ReaderMemoryDiagTrace& before,
              const ReaderMemoryDiagTrace& after, const unsigned long elapsedMs) {
  LOG_INF(
      "MEMD",
      "Internal heap delta: phase=%s elapsed=%lums freeBefore=%lu freeAfter=%lu freeDelta=%ld maxBefore=%lu maxAfter=%lu maxDelta=%ld psramFreeBefore=%lu psramFreeAfter=%lu psramFreeDelta=%ld",
      phase ? phase : "?",
      elapsedMs,
      static_cast<unsigned long>(before.internalFree),
      static_cast<unsigned long>(after.internalFree),
      deltaU32(after.internalFree, before.internalFree),
      static_cast<unsigned long>(before.internalMaxAlloc),
      static_cast<unsigned long>(after.internalMaxAlloc),
      deltaU32(after.internalMaxAlloc, before.internalMaxAlloc),
      static_cast<unsigned long>(before.psramFree),
      static_cast<unsigned long>(after.psramFree),
      deltaU32(after.psramFree, before.psramFree));
}

void logDeltaIfChanged(const char* phase, const ReaderMemoryDiagTrace& before,
                       const ReaderMemoryDiagTrace& after, const unsigned long elapsedMs,
                       const uint32_t internalThresholdBytes,
                       const uint32_t psramThresholdBytes,
                       const unsigned long slowThresholdMs) {
  const bool internalMoved =
      absDeltaU32(before.internalFree, after.internalFree) >= internalThresholdBytes ||
      absDeltaU32(before.internalMaxAlloc, after.internalMaxAlloc) >= internalThresholdBytes;
  const bool psramMoved =
      absDeltaU32(before.psramFree, after.psramFree) >= psramThresholdBytes ||
      absDeltaU32(before.psramMaxAlloc, after.psramMaxAlloc) >= psramThresholdBytes;
  const bool slow = elapsedMs >= slowThresholdMs;
  if (!internalMoved && !psramMoved && !slow) return;
  logDelta(phase, before, after, elapsedMs);
}

}  // namespace ReaderMemoryDiagnostics
