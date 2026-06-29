#pragma once

#include <cstddef>
#include <cstdint>

struct ReaderMemoryDiagTrace {
  uint32_t internalFree = 0;
  uint32_t internalMaxAlloc = 0;
  uint32_t psramFree = 0;
  uint32_t psramMaxAlloc = 0;
};

namespace ReaderMemoryDiagnostics {

ReaderMemoryDiagTrace capture();
const char* bufferLocation(const void* ptr);

void logCheckpoint(const char* phase);
void logLargeBuffer(const char* tag, const void* ptr, size_t size);
void logDelta(const char* phase, const ReaderMemoryDiagTrace& before,
              const ReaderMemoryDiagTrace& after, unsigned long elapsedMs);

// For very hot paths, only print when memory moved or the operation was slow.
void logDeltaIfChanged(const char* phase, const ReaderMemoryDiagTrace& before,
                       const ReaderMemoryDiagTrace& after, unsigned long elapsedMs,
                       uint32_t internalThresholdBytes = 512,
                       uint32_t psramThresholdBytes = 4096,
                       unsigned long slowThresholdMs = 80);

}  // namespace ReaderMemoryDiagnostics
