#pragma once
#include <Arduino.h>

class EPD_Painter;

class HalDisplay {
 public:
  // Constructor
  HalDisplay();

  // Destructor
  ~HalDisplay();

  // Refresh modes
  enum RefreshMode {
    FULL_REFRESH,  // Full refresh with complete waveform
    HALF_REFRESH,  // Half refresh - balanced quality and speed
    FAST_REFRESH   // Fast refresh
  };

  // Initialize the display hardware and driver
  void begin();

  // Display dimensions (M5PaperS3: 960x540 physical landscape)
  static constexpr uint16_t DISPLAY_WIDTH = 960;
  static constexpr uint16_t DISPLAY_HEIGHT = 540;
  // 8bpp framebuffer: 1 byte per pixel, values 0(white)..3(black) — EPD_Painter native format
  static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH * DISPLAY_HEIGHT;
  // For unpacking 1-bit source images only
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;

  // Runtime accessors — match the X3 HalDisplay surface for shared lib code
  // (JpegToBmpConverter, GfxRenderer). The Paper S3 panel is fixed-size so
  // these can return constants directly.
  uint16_t getDisplayWidth() const { return DISPLAY_WIDTH; }
  uint16_t getDisplayHeight() const { return DISPLAY_HEIGHT; }
  uint16_t getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
  uint32_t getBufferSize() const { return BUFFER_SIZE; }

  // Frame buffer operations
  void clearScreen(uint8_t color = 0xFF) const;
  void drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                 bool fromProgmem = false) const;
  void drawImageTransparent(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                            bool fromProgmem = false) const;

  void displayBuffer(RefreshMode mode = RefreshMode::FAST_REFRESH, bool turnOffScreen = false);
  void refreshDisplay(RefreshMode mode = RefreshMode::FAST_REFRESH, bool turnOffScreen = false);

  // Power management
  void deepSleep();

  // Access to frame buffer
  uint8_t* getFrameBuffer() const;

  void copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer);
  void copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer);
  void copyGrayscaleMsbBuffers(const uint8_t* msbBuffer);
  void cleanupGrayscaleBuffers(const uint8_t* bwBuffer);

  void displayGrayBuffer(bool turnOffScreen = false);
    /*
  * 臨時 GC16 實機測試入口。
  *
  * 顯示 16 階測試條後，不應再呼叫一般
  * displayBuffer()，否則現有 2bpp 畫面狀態
  * 會覆蓋 GC16 畫面。
  */
  bool showGc16TestBars(
      bool clearFirst = true
  );

 private:
  // 8bpp framebuffer in PSRAM — EPD_Painter native format (0=white, 3=black)
  uint8_t* frameBuffer;

  // EPD_Painter driver instance
  EPD_Painter* epd = nullptr;

  // Grayscale buffers (8bpp, allocated on demand for two-pass rendering)
  uint8_t* grayLsbBuffer = nullptr;
  uint8_t* grayMsbBuffer = nullptr;
  void freeGrayscaleBuffers();
};

// Global singleton — defined in src/main.cpp.
extern HalDisplay display;
