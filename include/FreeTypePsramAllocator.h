#pragma once

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SYSTEM_H

namespace CrossPointFtPsramAllocator {

// V1.8.0 release note:
// Create a FreeType library using the CrossPoint PSRAM-preferred allocator.
// This replaces FT_Init_FreeType() inside the patched OpenFontRender runtime.
FT_Error newLibrary(FT_Library* library);

// Log allocator counters.  context may be nullptr.
void logSummary(const char* context);

// Reset diagnostic counters before a new FT_Library is created.
void resetSummary();

}  // namespace CrossPointFtPsramAllocator
