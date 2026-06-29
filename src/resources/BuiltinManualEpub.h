#pragma once

#include <cstddef>
#include <cstdint>

namespace BuiltinManualEpub {
const uint8_t* data();
size_t size();
const char* path();
bool ensureInstalled();
}
