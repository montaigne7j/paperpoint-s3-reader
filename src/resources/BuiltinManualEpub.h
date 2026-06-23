#pragma once

#include <cstddef>

namespace BuiltinManualEpub {
const char* storagePath();
size_t size();
bool ensureInstalled();
}
