/*
 * SPDX-License-Identifier: BSD-2-Clause
 * GC16 waveform data adapted from LovyanGFX/M5GFX Panel_EPD.cpp.
 * Copyright (c) 2020 lovyan03 (https://github.com/lovyan03).
 * All rights reserved.
 *
 * Modified and integrated for CrossPoint/PaperPoint on 2026-06-20.
 * Full terms: LICENSES/BSD-2-Clause-LovyanGFX.txt
 */

#ifndef EPD_PAINTER_GC16_LUT_H
#define EPD_PAINTER_GC16_LUT_H

#include <cstddef>
#include <cstdint>

/*
 * LUT value:
 *   0 = end / neutral
 *   1 = drive toward black
 *   2 = drive toward white
 *   3 = no operation
 */

namespace epd_gc16 {

#define GC16_LUT_MAKE(                                      \
    d0, d1, d2, d3, d4, d5, d6, d7,                       \
    d8, d9, da, db, dc, dd, de, df)                        \
  static_cast<uint32_t>(                                   \
      ((d0) << 0) | ((d1) << 2) |                         \
      ((d2) << 4) | ((d3) << 6) |                         \
      ((d4) << 8) | ((d5) << 10) |                        \
      ((d6) << 12) | ((d7) << 14) |                       \
      ((d8) << 16) | ((d9) << 18) |                       \
      ((da) << 20) | ((db) << 22) |                       \
      ((dc) << 24) | ((dd) << 26) |                       \
      ((de) << 28) | ((df) << 30))

inline constexpr uint32_t kQualityLut[] = {
    GC16_LUT_MAKE(
        1, 1, 1, 1, 1, 1, 1, 2,
        1, 2, 2, 1, 1, 1, 1, 1
    ),

    GC16_LUT_MAKE(
        2, 2, 2, 2, 2, 2, 2, 2,
        2, 2, 2, 2, 2, 2, 2, 2
    ),

    GC16_LUT_MAKE(
        2, 2, 2, 2, 2, 2, 2, 2,
        2, 2, 2, 2, 2, 2, 2, 2
    ),

    GC16_LUT_MAKE(
        1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1
    ),

    GC16_LUT_MAKE(
        1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1
    ),

    GC16_LUT_MAKE(
        1, 1, 1, 1, 1, 1, 1, 1,
        2, 1, 1, 1, 1, 1, 1, 1
    ),

    GC16_LUT_MAKE(
        1, 1, 2, 2, 1, 1, 1, 2,
        1, 2, 1, 1, 1, 1, 1, 3
    ),

    GC16_LUT_MAKE(
        1, 1, 1, 1, 1, 2, 1, 1,
        2, 2, 1, 2, 1, 2, 2, 2
    ),

    GC16_LUT_MAKE(
        1, 1, 3, 2, 2, 1, 2, 2,
        2, 2, 2, 2, 1, 1, 2, 2
    ),

    GC16_LUT_MAKE(
        3, 2, 2, 2, 2, 2, 2, 2,
        2, 2, 2, 2, 2, 2, 2, 2
    ),

    GC16_LUT_MAKE(
        3, 2, 2, 2, 2, 2, 2, 2,
        2, 2, 2, 2, 2, 2, 2, 2
    ),

    GC16_LUT_MAKE(
        1, 1, 1, 1, 3, 2, 2, 2,
        2, 2, 2, 2, 2, 2, 2, 2
    ),

    GC16_LUT_MAKE(
        1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 3, 3, 2, 2, 2, 2
    ),

    GC16_LUT_MAKE(
        3, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 3
    ),

    GC16_LUT_MAKE(
        3, 3, 3, 3, 3, 3, 3, 3,
        3, 3, 3, 3, 2, 2, 2, 3
    ),

    ~0u, ~0u, ~0u, ~0u,
    ~0u, ~0u, ~0u, ~0u,
    ~0u, ~0u, ~0u, ~0u,
    ~0u, ~0u, ~0u, ~0u,

    0u,
};

inline constexpr uint32_t kEraserLut[] = {
    GC16_LUT_MAKE(
        2, 2, 2, 2, 2, 2, 2, 2,
        2, 2, 2, 3, 3, 3, 1, 1
    ),

    GC16_LUT_MAKE(
        2, 2, 3, 3, 3, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1
    ),

    ~0u,
    0u,
};

inline constexpr size_t kQualityStepCount =
    sizeof(kQualityLut) /
    sizeof(kQualityLut[0]);

inline constexpr size_t kEraserStepCount =
    sizeof(kEraserLut) /
    sizeof(kEraserLut[0]);

#undef GC16_LUT_MAKE

}  // namespace epd_gc16

#endif