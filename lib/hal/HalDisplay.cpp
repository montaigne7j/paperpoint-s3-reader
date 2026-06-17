#include <EPD_Painter.h>
#include <EPD_Painter_presets.h>
#include <HalDisplay.h>

#include <cstring>

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
  bool ok = epd->begin();

  // Full white paint on boot to physically clear the sleep cover and sync
  // EPD_Painter's internal state with the panel. clear() resets the internal
  // buffer, then paint() drives the e-ink particles to white.
  if (ok && frameBuffer) {
    epd->clear();
    epd->setQuality(EPD_Painter::Quality::QUALITY_HIGH);
    epd->paint(frameBuffer);  // frameBuffer is all-zero (white) from memset above
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
      // Full clear + repaint — eliminates all ghosting
      epd->clear();
      epd->setQuality(EPD_Painter::Quality::QUALITY_HIGH);
      break;
    case HALF_REFRESH:
    case FAST_REFRESH:
    default:
      // Use QUALITY_HIGH for all modes — QUALITY_NORMAL leaves ghost traces
      // on e-paper because it doesn't fully drive the ink particles.
      epd->setQuality(EPD_Painter::Quality::QUALITY_HIGH);
      break;
  }

  epd->paint(frameBuffer);
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
