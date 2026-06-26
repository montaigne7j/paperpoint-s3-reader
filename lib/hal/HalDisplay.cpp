#include <EPD_Painter.h>
#include <EPD_Painter_presets.h>
#include <HalDisplay.h>

#include <cstring>

#include <Bitmap.h>
#include <Logging.h>
#include <esp_heap_caps.h>

namespace {

const char* gc16ResultToString(
    const EPD_Painter::Gc16Result result
) {
  switch (result) {
    case EPD_Painter::Gc16Result::Success:
      return "Success";

    case EPD_Painter::Gc16Result::NotInitialized:
      return "NotInitialized";

    case EPD_Painter::Gc16Result::InvalidArgument:
      return "InvalidArgument";

    case EPD_Painter::Gc16Result::InvalidBufferSize:
      return "InvalidBufferSize";

    case EPD_Painter::Gc16Result::UnsupportedDevice:
      return "UnsupportedDevice";

    case EPD_Painter::Gc16Result::UnsupportedGeometry:
      return "UnsupportedGeometry";

    case EPD_Painter::Gc16Result::AllocationFailed:
      return "AllocationFailed";

    default:
      return "Unknown";
  }
}

}  // namespace

namespace {

inline void setGc16PackedPixel(
    uint8_t* buffer,
    const int panelWidth,
    const int x,
    const int y,
    const uint8_t grayLevel
) {
  const size_t rowBytes =
      static_cast<size_t>(
          panelWidth
      ) /
      2;

  const size_t index =
      static_cast<size_t>(y) *
          rowBytes +
      static_cast<size_t>(x >> 1);

  const uint8_t level =
      grayLevel & 0x0F;

  if ((x & 1) == 0) {
    // 偶數 X 放在高 nibble。
    buffer[index] =
        static_cast<uint8_t>(
            (buffer[index] & 0x0F) |
            (level << 4)
        );
  } else {
    // 奇數 X 放在低 nibble。
    buffer[index] =
        static_cast<uint8_t>(
            (buffer[index] & 0xF0) |
            level
        );
  }
}

inline uint8_t rgbToLuminance(
    const uint8_t red,
    const uint8_t green,
    const uint8_t blue
) {
  /*
   * BT.601 整數近似：
   *
   * 0   = 黑
   * 255 = 白
   */
  return static_cast<uint8_t>(
      (
          77u *
              static_cast<uint16_t>(red) +
          150u *
              static_cast<uint16_t>(green) +
          29u *
              static_cast<uint16_t>(blue)
      ) >>
      8
  );
}

inline uint8_t luminanceToGc16(
    const uint8_t luminance
) {
  /*
   * 255 / 15 = 17。
   *
   * 加 8 做四捨五入：
   * 0   → level 0
   * 255 → level 15
   */
  return static_cast<uint8_t>(
      (static_cast<uint16_t>(luminance) + 8u) /
      17u
  );
}

inline int16_t distributeFsError(
    const int32_t error,
    const int32_t weight
) {
  /*
   * Floyd–Steinberg 除數為 16。
   * 對正負數都做對稱四捨五入。
   */
  const int32_t weighted =
      error * weight;

  if (weighted >= 0) {
    return static_cast<int16_t>(
        (weighted + 8) / 16
    );
  }

  return static_cast<int16_t>(
      (weighted - 8) / 16
  );
}

}  // namespace

HalDisplay::HalDisplay() : frameBuffer(nullptr) {}

HalDisplay::~HalDisplay() {
  freeGrayscaleBuffers();
  if (epd) {
    epd->end();
    delete epd;
    epd = nullptr;
  }
  if (frameBuffer) {
    heap_caps_free(frameBuffer);
    frameBuffer = nullptr;
  }
}

void HalDisplay::freeGrayscaleBuffers() {
  if (grayLsbBuffer) {
    heap_caps_free(grayLsbBuffer);
    grayLsbBuffer = nullptr;
  }
  if (grayMsbBuffer) {
    heap_caps_free(grayMsbBuffer);
    grayMsbBuffer = nullptr;
  }
}

void HalDisplay::begin() {
  // Allocate 8bpp framebuffer in PSRAM — EPD_Painter native format
  frameBuffer = static_cast<uint8_t*>(heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!frameBuffer) {
    frameBuffer = static_cast<uint8_t*>(malloc(BUFFER_SIZE));
  }
  if (frameBuffer) {
    memset(frameBuffer, 0, BUFFER_SIZE);  // 0 = white in EPD_Painter
  }

  EPD_Painter::Config epdConfig = EPD_PAINTER_PRESET;
  epdConfig.rotation = EPD_Painter::Rotation::ROTATION_0;
  epdConfig.quality = EPD_Painter::Quality::QUALITY_NORMAL;
  epdConfig.i2c.scl = -1;
  epdConfig.i2c.sda = -1;

  epd = new EPD_Painter(epdConfig);

  const bool ok =
      epd->begin();

  if (ok &&
      frameBuffer != nullptr) {
    if (!force2bppWhiteResync()) {
      LOG_ERR(
          "DISP",
          "Initial 2bpp resync failed"
      );
    }
  }

  if (Serial)
    Serial.printf("[%lu] HalDisplay: begin() - framebuffer %s, EPD_Painter %s\n", millis(), frameBuffer ? "OK" : "FAIL",
                  ok ? "OK" : "FAIL");
}

void HalDisplay::clearScreen(uint8_t color) const {
  if (!frameBuffer) return;
  // Map old 1-bit convention to 8bpp EPD_Painter values:
  // 0xFF (old white) → 0 (EPD white), 0x00 (old black) → 3 (EPD black)
  uint8_t epdColor = (color == 0xFF) ? 0 : 3;
  memset(frameBuffer, epdColor, BUFFER_SIZE);
}

void HalDisplay::drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                           bool fromProgmem) const {
  if (!frameBuffer) return;

  // Source images are 1-bit packed (8 pixels/byte, MSB first, bit=1=white, bit=0=black)
  // Unpack into 8bpp framebuffer: 0=white, 3=black
  const uint16_t imageWidthBytes = w / 8;

  for (uint16_t row = 0; row < h; row++) {
    const uint16_t destY = y + row;
    if (destY >= DISPLAY_HEIGHT) break;

    const uint32_t destRowStart = destY * DISPLAY_WIDTH + x;
    const uint16_t srcOffset = row * imageWidthBytes;

    for (uint16_t col = 0; col < imageWidthBytes; col++) {
      if (x + col * 8 >= DISPLAY_WIDTH) break;
      uint8_t srcByte = fromProgmem ? pgm_read_byte(&imageData[srcOffset + col]) : imageData[srcOffset + col];
      uint32_t dstIdx = destRowStart + col * 8;
      for (int bit = 7; bit >= 0; bit--) {
        if (dstIdx < BUFFER_SIZE) {
          frameBuffer[dstIdx] = (srcByte & (1 << bit)) ? 0 : 3;  // bit=1→white(0), bit=0→black(3)
        }
        dstIdx++;
      }
    }
  }
}

void HalDisplay::drawImageTransparent(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                      bool fromProgmem) const {
  if (!frameBuffer) return;

  // Transparent draw: only set black pixels, leave white pixels unchanged
  const uint16_t imageWidthBytes = w / 8;

  for (uint16_t row = 0; row < h; row++) {
    const uint16_t destY = y + row;
    if (destY >= DISPLAY_HEIGHT) break;

    const uint32_t destRowStart = destY * DISPLAY_WIDTH + x;
    const uint16_t srcOffset = row * imageWidthBytes;

    for (uint16_t col = 0; col < imageWidthBytes; col++) {
      if (x + col * 8 >= DISPLAY_WIDTH) break;
      uint8_t srcByte = fromProgmem ? pgm_read_byte(&imageData[srcOffset + col]) : imageData[srcOffset + col];
      uint32_t dstIdx = destRowStart + col * 8;
      for (int bit = 7; bit >= 0; bit--) {
        if (dstIdx < BUFFER_SIZE && !(srcByte & (1 << bit))) {
          frameBuffer[dstIdx] = 3;  // source bit=0 → black
        }
        dstIdx++;
      }
    }
  }
}

void HalDisplay::displayBuffer(RefreshMode mode, bool turnOffScreen) {
  if (!epd || !frameBuffer) return;

  switch (mode) {
    case FULL_REFRESH:
      // Full clear + repaint — eliminates all ghosting.
      epd->clear();
      epd->setQuality(EPD_Painter::Quality::QUALITY_HIGH);
      break;
    case PAGE_TURN_REFRESH_ORIGINAL:
      // Original reader page-turn path: use the normal painter timing instead of
      // the experimental band-scan path.
      epd->setQuality(EPD_Painter::Quality::QUALITY_HIGH);
      break;
    case PAGE_TURN_REFRESH:
      epd->setQuality(EPD_Painter::Quality::QUALITY_HIGH);
      epd->paintRowMajor(frameBuffer, false);
      return;
    case PAGE_TURN_REFRESH_REVERSE:
      epd->setQuality(EPD_Painter::Quality::QUALITY_HIGH);
      epd->paintRowMajor(frameBuffer, true);
      return;
    case HALF_REFRESH:
    case FAST_REFRESH:
    default:
      // Keep non-reader UI paths conservative.  QUALITY_NORMAL/FAST can leave
      // ghost traces on menus and file browser rows, so these modes stay clean.
      epd->setQuality(EPD_Painter::Quality::QUALITY_HIGH);
      break;
  }

  epd->paint(frameBuffer);
}

void HalDisplay::displayBufferRows(int rowStart, int rowEnd, bool turnOffScreen) {
  (void)turnOffScreen;
  if (!epd || !frameBuffer) return;
  // Avoid overwriting EPD_Painter's one-slot packed_paintbuffer while a prior
  // progressive row-range waveform is still using its packed_fastbuffer copy
  // and updating the internal screenbuffer.  Rendering of the next stripe can
  // still overlap with the previous waveform; this wait happens only when the
  // next row-range display is submitted.
  epd->waitUntilIdle();
  epd->setQuality(EPD_Painter::Quality::QUALITY_HIGH);
  epd->paintRowRange(frameBuffer, rowStart, rowEnd);
}


void HalDisplay::waitUntilIdle() {
  if (!epd) return;
  epd->waitUntilIdle();
}

void HalDisplay::refreshDisplay(RefreshMode mode, bool turnOffScreen) {
  // With EPD_Painter, refresh = repaint the current buffer
  displayBuffer(mode, turnOffScreen);
}

bool HalDisplay::showGc16TestBars(
    const bool clearFirst
) {
  if (epd == nullptr) {
    if (Serial) {
      Serial.printf(
          "[%lu] [ERR] [GC16] "
          "EPD_Painter is not initialized\n",
          millis()
      );
    }

    return false;
  }

  if (Serial) {
    Serial.printf(
        "[%lu] [INF] [GC16] "
        "Starting 16-level grayscale test "
        "(clearFirst=%d)\n",
        millis(),
        clearFirst ? 1 : 0
    );
  }

  const EPD_Painter::Gc16Result result =
      epd->paintGc16TestBars(
          clearFirst
      );

  if (Serial) {
    Serial.printf(
        "[%lu] [%s] [GC16] "
        "Test completed: result=%u (%s)\n",
        millis(),
        result ==
                EPD_Painter::
                    Gc16Result::Success
            ? "INF"
            : "ERR",
        static_cast<unsigned>(result),
        gc16ResultToString(result)
    );
  }

  return result ==
      EPD_Painter::Gc16Result::Success;
}

bool HalDisplay::showGc16Bitmap(
    const Bitmap& bitmap,
    const bool clearFirst,
    const Gc16DitherMode ditherMode,
    const bool rotate180
) {
  if (epd == nullptr) {
    LOG_ERR(
        "GC16",
        "EPD_Painter is not initialized"
    );

    return false;
  }

  constexpr int SOURCE_WIDTH = 540;
  constexpr int SOURCE_HEIGHT = 960;

  constexpr int PANEL_WIDTH = 960;
  constexpr int PANEL_HEIGHT = 540;

  if (bitmap.getWidth() != SOURCE_WIDTH ||
      bitmap.getHeight() != SOURCE_HEIGHT) {
    LOG_ERR(
        "GC16",
        "Unsupported BMP dimensions: "
        "%dx%d, expected %dx%d",
        bitmap.getWidth(),
        bitmap.getHeight(),
        SOURCE_WIDTH,
        SOURCE_HEIGHT
    );

    return false;
  }

  const uint16_t bpp =
      bitmap.getBpp();

  if (bpp != 24 &&
      bpp != 32) {
    LOG_ERR(
        "GC16",
        "Unsupported BMP depth: %u bpp, "
        "expected 24 or 32",
        static_cast<unsigned>(bpp)
    );

    return false;
  }

  const size_t panelRowBytes =
      PANEL_WIDTH / 2;

  const size_t gc16BufferSize =
      panelRowBytes *
      PANEL_HEIGHT;

  uint8_t* gc16Buffer =
      static_cast<uint8_t*>(
          heap_caps_aligned_alloc(
              16,
              gc16BufferSize,
              MALLOC_CAP_SPIRAM |
                  MALLOC_CAP_8BIT
          )
      );

  if (gc16Buffer == nullptr) {
    LOG_ERR(
        "GC16",
        "Failed to allocate GC16 buffer: "
        "%u bytes",
        static_cast<unsigned>(
            gc16BufferSize
        )
    );

    return false;
  }

  /*
   * 0xFF：
   * 兩個 pixel 都是 level 15，也就是白色。
   */
  std::memset(
      gc16Buffer,
      0xFF,
      gc16BufferSize
  );

  const size_t sourceRowBytes =
      static_cast<size_t>(
          bitmap.getRowBytes()
      );

  uint8_t* rawRow =
      static_cast<uint8_t*>(
          heap_caps_malloc(
              sourceRowBytes,
              MALLOC_CAP_8BIT
          )
      );

  if (rawRow == nullptr) {
    LOG_ERR(
        "GC16",
        "Failed to allocate BMP row: "
        "%u bytes",
        static_cast<unsigned>(
            sourceRowBytes
        )
    );

    heap_caps_free(gc16Buffer);

    return false;
  }

  /*
   * Floyd–Steinberg 使用兩列誤差 buffer。
   *
   * 左右各多一格 padding：
   * index 0            = 左側 padding
   * index 1..540       = 實際像素
   * index 541          = 右側 padding
   */
  constexpr size_t ERROR_COUNT =
      SOURCE_WIDTH + 2;

  constexpr size_t ERROR_BYTES =
      ERROR_COUNT *
      sizeof(int16_t);

  int16_t* currentError = nullptr;
  int16_t* nextError = nullptr;

  if (ditherMode ==
      Gc16DitherMode::FloydSteinberg) {
    currentError =
        static_cast<int16_t*>(
            heap_caps_calloc(
                ERROR_COUNT,
                sizeof(int16_t),
                MALLOC_CAP_INTERNAL |
                    MALLOC_CAP_8BIT
            )
        );

    nextError =
        static_cast<int16_t*>(
            heap_caps_calloc(
                ERROR_COUNT,
                sizeof(int16_t),
                MALLOC_CAP_INTERNAL |
                    MALLOC_CAP_8BIT
            )
        );

    /*
     * 若 internal RAM 不足，改用 PSRAM。
     */
    if (currentError == nullptr ||
        nextError == nullptr) {
      if (currentError != nullptr) {
        heap_caps_free(currentError);
        currentError = nullptr;
      }

      if (nextError != nullptr) {
        heap_caps_free(nextError);
        nextError = nullptr;
      }

      currentError =
          static_cast<int16_t*>(
              heap_caps_calloc(
                  ERROR_COUNT,
                  sizeof(int16_t),
                  MALLOC_CAP_SPIRAM |
                      MALLOC_CAP_8BIT
              )
          );

      nextError =
          static_cast<int16_t*>(
              heap_caps_calloc(
                  ERROR_COUNT,
                  sizeof(int16_t),
                  MALLOC_CAP_SPIRAM |
                      MALLOC_CAP_8BIT
              )
          );
    }

    if (currentError == nullptr ||
        nextError == nullptr) {
      LOG_ERR(
          "GC16",
          "Failed to allocate Floyd-Steinberg "
          "error buffers"
      );

      if (currentError != nullptr) {
        heap_caps_free(currentError);
      }

      if (nextError != nullptr) {
        heap_caps_free(nextError);
      }

      heap_caps_free(rawRow);
      heap_caps_free(gc16Buffer);

      return false;
    }
  }

  auto freeConversionBuffers = [&]() {
    if (currentError != nullptr) {
      heap_caps_free(currentError);
      currentError = nullptr;
    }

    if (nextError != nullptr) {
      heap_caps_free(nextError);
      nextError = nullptr;
    }

    if (rawRow != nullptr) {
      heap_caps_free(rawRow);
      rawRow = nullptr;
    }

    if (gc16Buffer != nullptr) {
      heap_caps_free(gc16Buffer);
      gc16Buffer = nullptr;
    }
  };

  const BmpReaderError rewindResult =
      bitmap.rewindToData();

  if (rewindResult !=
      BmpReaderError::Ok) {
    LOG_ERR(
        "GC16",
        "Failed to rewind BMP: %s",
        Bitmap::errorToString(
            rewindResult
        )
    );

    freeConversionBuffers();

    return false;
  }

  const int bytesPerPixel =
      bpp / 8;

  LOG_INF(
      "GC16",
      "Converting BMP: %dx%d, %u bpp, "
      "rowBytes=%u, topDown=%d, dither=%s, rotate180=%d",
      bitmap.getWidth(),
      bitmap.getHeight(),
      static_cast<unsigned>(bpp),
      static_cast<unsigned>(
          sourceRowBytes
      ),
      bitmap.isTopDown() ? 1 : 0,
      ditherMode ==
              Gc16DitherMode::FloydSteinberg
          ? "FloydSteinberg"
          : "None",
      rotate180 ? 1 : 0
  );

  const uint32_t convertStart =
      millis();

  for (int fileRow = 0;
       fileRow < SOURCE_HEIGHT;
       ++fileRow) {
    const BmpReaderError readResult =
        bitmap.readNextRawRow(
            rawRow
        );

    if (readResult !=
        BmpReaderError::Ok) {
      LOG_ERR(
          "GC16",
          "Failed to read BMP row %d: %s",
          fileRow,
          Bitmap::errorToString(
              readResult
          )
      );

      freeConversionBuffers();

      return false;
    }

    /*
     * BMP 若不是 top-down：
     * fileRow 0 是圖片最下方。
     */
    const int sourceY =
        bitmap.isTopDown()
            ? fileRow
            : SOURCE_HEIGHT -
                  1 -
                  fileRow;

    /*
     * 蛇形 Floyd–Steinberg：
     *
     * 偶數列：左 → 右
     * 奇數列：右 → 左
     *
     * 可減少單向誤差造成的斜紋。
     */
    const bool leftToRight =
        (fileRow & 1) == 0;

    if (ditherMode ==
        Gc16DitherMode::FloydSteinberg) {
      std::memset(
          nextError,
          0,
          ERROR_BYTES
      );
    }

    for (int scanIndex = 0;
         scanIndex < SOURCE_WIDTH;
         ++scanIndex) {
      const int sourceX =
          leftToRight
              ? scanIndex
              : SOURCE_WIDTH -
                    1 -
                    scanIndex;

      const uint8_t* pixel =
          rawRow +
          static_cast<size_t>(
              sourceX
          ) *
              bytesPerPixel;

      /*
       * BMP 原始像素順序：
       *
       * 24-bit：B G R
       * 32-bit：B G R A
       */
      const uint8_t blue =
          pixel[0];

      const uint8_t green =
          pixel[1];

      const uint8_t red =
          pixel[2];

      const uint8_t luminance =
          rgbToLuminance(
              red,
              green,
              blue
          );

      uint8_t grayLevel = 0;

      if (ditherMode ==
          Gc16DitherMode::FloydSteinberg) {
        /*
         * 誤差值採 1/16 luminance 單位。
         *
         * luminance 0..255
         * → scaled 0..4080
         *
         * 一個 GC16 level：
         * 17 luminance × 16 = 272
         */
        const int errorIndex =
            sourceX + 1;

        int32_t adjusted =
            static_cast<int32_t>(
                luminance
            ) *
                16 +
            currentError[
                errorIndex
            ];

        if (adjusted < 0) {
          adjusted = 0;
        } else if (adjusted > 4080) {
          adjusted = 4080;
        }

        /*
         * 272 / 2 = 136，用於四捨五入。
         */
        int32_t quantizedLevel =
            (adjusted + 136) /
            272;

        if (quantizedLevel < 0) {
          quantizedLevel = 0;
        } else if (quantizedLevel > 15) {
          quantizedLevel = 15;
        }

        grayLevel =
            static_cast<uint8_t>(
                quantizedLevel
            );

        const int32_t quantizedValue =
            quantizedLevel *
            272;

        const int32_t quantizationError =
            adjusted -
            quantizedValue;

        if (leftToRight) {
          /*
           *          X   7/16
           *    3/16  5/16  1/16
           */
          currentError[
              errorIndex + 1
          ] +=
              distributeFsError(
                  quantizationError,
                  7
              );

          nextError[
              errorIndex - 1
          ] +=
              distributeFsError(
                  quantizationError,
                  3
              );

          nextError[
              errorIndex
          ] +=
              distributeFsError(
                  quantizationError,
                  5
              );

          nextError[
              errorIndex + 1
          ] +=
              distributeFsError(
                  quantizationError,
                  1
              );
        } else {
          /*
           * 7/16  X
           * 1/16  5/16  3/16
           */
          currentError[
              errorIndex - 1
          ] +=
              distributeFsError(
                  quantizationError,
                  7
              );

          nextError[
              errorIndex + 1
          ] +=
              distributeFsError(
                  quantizationError,
                  3
              );

          nextError[
              errorIndex
          ] +=
              distributeFsError(
                  quantizationError,
                  5
              );

          nextError[
              errorIndex - 1
          ] +=
              distributeFsError(
                  quantizationError,
                  1
              );
        }
      } else {
        grayLevel =
            luminanceToGc16(
                luminance
            );
      }

      /*
       * 直式 logical image：
       * 540 × 960
       *
       * 轉換成實體 panel：
       * 960 × 540
       */
      const int panelX =
          rotate180
              ? PANEL_WIDTH - 1 - sourceY
              : sourceY;

      const int panelY =
          rotate180
              ? sourceX
              : PANEL_HEIGHT - 1 - sourceX;

      setGc16PackedPixel(
          gc16Buffer,
          PANEL_WIDTH,
          panelX,
          panelY,
          grayLevel
      );
    }

    if (ditherMode ==
        Gc16DitherMode::FloydSteinberg) {
      int16_t* temporary =
          currentError;

      currentError =
          nextError;

      nextError =
          temporary;
    }

    if ((fileRow & 31) == 31) {
      vTaskDelay(1);
    }
  }

  LOG_INF(
      "GC16",
      "BMP conversion completed in "
      "%lu ms",
      millis() - convertStart
  );

  /*
   * raw row 與 error buffer 已經不再需要。
   */
  if (rawRow != nullptr) {
    heap_caps_free(rawRow);
    rawRow = nullptr;
  }

  if (currentError != nullptr) {
    heap_caps_free(currentError);
    currentError = nullptr;
  }

  if (nextError != nullptr) {
    heap_caps_free(nextError);
    nextError = nullptr;
  }

  const uint32_t paintStart =
      millis();

  const EPD_Painter::Gc16Result result =
      epd->paintGc16Full(
          gc16Buffer,
          gc16BufferSize,
          clearFirst
      );

  LOG_INF(
      "GC16",
      "GC16 BMP paint completed: "
      "result=%u (%s), time=%lu ms",
      static_cast<unsigned>(result),
      gc16ResultToString(result),
      millis() - paintStart
  );

  heap_caps_free(gc16Buffer);
  gc16Buffer = nullptr;

  return result ==
      EPD_Painter::Gc16Result::
          Success;
}

bool HalDisplay::showGc16LogicalBuffer(
    const uint8_t* logicalBuffer,
    const size_t logicalBufferSize,
    const bool clearFirst,
    const bool rotate180
) {
  if (epd == nullptr || logicalBuffer == nullptr) {
    LOG_ERR("GC16", "Logical GC16 display is not initialized");
    return false;
  }

  constexpr int SOURCE_WIDTH = 540;
  constexpr int SOURCE_HEIGHT = 960;
  constexpr int PANEL_WIDTH = 960;
  constexpr int PANEL_HEIGHT = 540;
  constexpr size_t LOGICAL_SIZE =
      static_cast<size_t>(SOURCE_WIDTH) * SOURCE_HEIGHT / 2;
  constexpr size_t PANEL_SIZE =
      static_cast<size_t>(PANEL_WIDTH) * PANEL_HEIGHT / 2;

  if (logicalBufferSize != LOGICAL_SIZE) {
    LOG_ERR(
        "GC16",
        "Invalid logical GC16 buffer size: %u, expected %u",
        static_cast<unsigned>(logicalBufferSize),
        static_cast<unsigned>(LOGICAL_SIZE));
    return false;
  }

  uint8_t* panelBuffer = static_cast<uint8_t*>(
      heap_caps_aligned_alloc(
          16, PANEL_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (panelBuffer == nullptr) {
    LOG_ERR("GC16", "Failed to allocate panel GC16 buffer");
    return false;
  }
  std::memset(panelBuffer, 0xFF, PANEL_SIZE);

  for (int y = 0; y < SOURCE_HEIGHT; ++y) {
    const size_t logicalRow = static_cast<size_t>(y) * SOURCE_WIDTH / 2;
    for (int x = 0; x < SOURCE_WIDTH; ++x) {
      const uint8_t packed = logicalBuffer[logicalRow + static_cast<size_t>(x >> 1)];
      const uint8_t level =
          (x & 1) == 0 ? static_cast<uint8_t>(packed >> 4)
                       : static_cast<uint8_t>(packed & 0x0F);

      const int panelX = rotate180 ? PANEL_WIDTH - 1 - y : y;
      const int panelY = rotate180 ? x : PANEL_HEIGHT - 1 - x;
      setGc16PackedPixel(panelBuffer, PANEL_WIDTH, panelX, panelY, level);
    }

    if ((y & 31) == 31) {
      vTaskDelay(1);
    }
  }

  const uint32_t paintStart = millis();
  const EPD_Painter::Gc16Result result =
      epd->paintGc16Full(panelBuffer, PANEL_SIZE, clearFirst);
  LOG_INF(
      "GC16",
      "Logical GC16 paint completed: result=%u (%s), time=%lu ms",
      static_cast<unsigned>(result),
      gc16ResultToString(result),
      millis() - paintStart);

  heap_caps_free(panelBuffer);
  return result == EPD_Painter::Gc16Result::Success;
}

void HalDisplay::deepSleep() {
  if (epd) {
    epd->end();
  }
}

uint8_t* HalDisplay::getFrameBuffer() const { return frameBuffer; }

void HalDisplay::copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer) {
  // Not used in the current flow — grayscale uses LSB/MSB copy + displayGrayBuffer
}

void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) {
  if (!lsbBuffer) return;
  if (!grayLsbBuffer) {
    grayLsbBuffer = static_cast<uint8_t*>(heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!grayLsbBuffer) grayLsbBuffer = static_cast<uint8_t*>(malloc(BUFFER_SIZE));
  }
  if (grayLsbBuffer) memcpy(grayLsbBuffer, lsbBuffer, BUFFER_SIZE);
}

void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) {
  if (!msbBuffer) return;
  if (!grayMsbBuffer) {
    grayMsbBuffer = static_cast<uint8_t*>(heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!grayMsbBuffer) grayMsbBuffer = static_cast<uint8_t*>(malloc(BUFFER_SIZE));
  }
  if (grayMsbBuffer) memcpy(grayMsbBuffer, msbBuffer, BUFFER_SIZE);
}

void HalDisplay::cleanupGrayscaleBuffers(const uint8_t* bwBuffer) {
  // No cleanup needed — EPD_Painter uses delta updates so we don't need
  // to push the BW buffer back to "reset" the display state
}

void HalDisplay::displayGrayBuffer(bool turnOffScreen) {
  if (!grayLsbBuffer || !grayMsbBuffer || !epd || !frameBuffer) return;

  // Combine two 8bpp gray planes into 4-level grayscale.
  // Each buffer has values 0 (white/unmarked) or 3 (black/marked).
  // Map to old bit convention: 0→bit1(white), 3→bit0(black)
  // then combine: gray = (msb_bit << 1) | lsb_bit → 0=black..3=white
  // EPD_Painter: 0=white..3=black, so epd_value = 3 - gray
  for (uint32_t i = 0; i < BUFFER_SIZE; i++) {
    uint8_t lsb_bit = (grayLsbBuffer[i] == 0) ? 1 : 0;
    uint8_t msb_bit = (grayMsbBuffer[i] == 0) ? 1 : 0;
    uint8_t gray = (msb_bit << 1) | lsb_bit;
    frameBuffer[i] = 3 - gray;
  }

  epd->setQuality(EPD_Painter::Quality::QUALITY_NORMAL);
  epd->paint(frameBuffer);
  freeGrayscaleBuffers();
}

bool HalDisplay::force2bppWhiteResync() {
  if (epd == nullptr ||
      frameBuffer == nullptr) {
    LOG_ERR(
        "DISP",
        "Cannot resync 2bpp state: "
        "display not initialized"
    );

    return false;
  }

  LOG_INF(
      "DISP",
      "Starting full white 2bpp resync"
  );

  /*
   * 0 是 EPD_Painter 的白色值。
   */
  std::memset(
      frameBuffer,
      0,
      BUFFER_SIZE
  );

  /*
   * 舊 LSB／MSB 暫存若存在，也不再有效。
   */
  freeGrayscaleBuffers();

  /*
   * clear():
   *   強制執行面板清洗 waveform。
   *
   * paint(white):
   *   讓 packed_screenbuffer 與實體面板
   *   都正式記錄成白色。
   */
  epd->clear();

  epd->setQuality(
      EPD_Painter::Quality::
          QUALITY_HIGH
  );

  epd->paint(
      frameBuffer
  );

  LOG_INF(
      "DISP",
      "Full white 2bpp resync completed"
  );

  return true;
}
