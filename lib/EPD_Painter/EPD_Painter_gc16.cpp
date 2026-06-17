#include "EPD_Painter.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "EPD_Painter_gc16_lut.h"
#include "build_opt.h"
#include "esp_heap_caps.h"

namespace {

/*
 * M5GFX GC16 framebuffer:
 *
 * byte:
 *   bits 7..4 = 左側／偶數 X 像素
 *   bits 3..0 = 右側／奇數 X 像素
 *
 * LUT output:
 *   每個像素 2 bit drive action。
 */
inline uint8_t gc16MapPixelPair(
    const uint8_t grayPair,
    const uint32_t lutFrame
) {
  const uint8_t firstGray =
      static_cast<uint8_t>(
          (grayPair >> 4) & 0x0F
      );

  const uint8_t secondGray =
      static_cast<uint8_t>(
          grayPair & 0x0F
      );

  const uint8_t firstAction =
      static_cast<uint8_t>(
          (lutFrame >>
           (firstGray * 2)) &
          0x03
      );

  const uint8_t secondAction =
      static_cast<uint8_t>(
          (lutFrame >>
           (secondGray * 2)) &
          0x03
      );

  return static_cast<uint8_t>(
      (firstAction << 2) |
      secondAction
  );
}

/*
 * 4bpp target row:
 *   480 bytes for 960 pixels
 *
 * 2bpp drive row:
 *   240 bytes for 960 pixels
 */
void gc16ConvertRow(
    const uint8_t* source4bpp,
    uint8_t* destination2bpp,
    const size_t destinationBytes,
    const uint32_t lutFrame
) {
  for (size_t outputIndex = 0;
       outputIndex < destinationBytes;
       ++outputIndex) {
    const uint8_t firstPair =
        gc16MapPixelPair(
            source4bpp[
                outputIndex * 2
            ],
            lutFrame
        );

    const uint8_t secondPair =
        gc16MapPixelPair(
            source4bpp[
                outputIndex * 2 + 1
            ],
            lutFrame
        );

    destination2bpp[outputIndex] =
        static_cast<uint8_t>(
            (firstPair << 4) |
            secondPair
        );
  }
}

}  // namespace

EPD_Painter::Gc16Result
EPD_Painter::paintGc16Full(
    const uint8_t* packed4bpp,
    const size_t packedSize,
    const bool clearFirst
) {
#if !defined(EPD_PAINTER_PRESET_M5PAPER_S3)
  (void)packed4bpp;
  (void)packedSize;
  (void)clearFirst;

  return Gc16Result::UnsupportedDevice;
#else
  if (packed4bpp == nullptr) {
    return Gc16Result::InvalidArgument;
  }

  if (_paint_active_sem == nullptr ||
      _paint_buffer_sem == nullptr ||
      dma_buffer1 == nullptr ||
      dma_buffer2 == nullptr) {
    return Gc16Result::NotInitialized;
  }

  /*
   * 第一版只支援實體 Paper S3 面板方向：
   * 960 × 540。
   */
  if (_config.width != 960 ||
      _config.height != 540 ||
      (_config.width % 4) != 0) {
    return Gc16Result::UnsupportedGeometry;
  }

  const size_t targetRowBytes =
      static_cast<size_t>(
          _config.width
      ) /
      2;

  const size_t expectedSize =
      targetRowBytes *
      static_cast<size_t>(
          _config.height
      );

  if (packedSize != expectedSize) {
    return Gc16Result::InvalidBufferSize;
  }

  /*
   * 先用既有完整清除流程，把面板建立成已知的白色狀態。
   *
   * 之後 eraser LUT 會以 gray=15（白）作為目前狀態。
   */
  if (clearFirst) {
    clear();
  }

  /*
   * 鎖定順序必須是：
   *
   *   active semaphore
   *   buffer semaphore
   *
   * 背景 paint task 也是先取得 active，再使用 buffer。
   * 不可反過來，否則可能 deadlock。
   */
  xSemaphoreTake(
      _paint_active_sem,
      portMAX_DELAY
  );

  xSemaphoreTake(
      _paint_buffer_sem,
      portMAX_DELAY
  );

  {
    PanelPowerGuard powerGuard(*this);

    /*
     * 確保第一次寫入使用 dma_buffer1。
     * sendRow() 每列會自動在兩個 buffer 間切換。
     */
    dma_buffer = dma_buffer1;

    /*
     * Stage 1：Eraser
     *
     * clear() 後目前畫面已知為白色，因此每個來源像素
     * 都使用 level 15，即 packed pair 0xFF。
     */
    for (size_t step = 0;
         step < epd_gc16::
                    kEraserStepCount;
         ++step) {
      const uint8_t pairAction =
          gc16MapPixelPair(
              0xFF,
              epd_gc16::
                  kEraserLut[step]
          );

      const uint8_t rowPattern =
          static_cast<uint8_t>(
              (pairAction << 4) |
              pairAction
          );

      /*
       * Eraser 每一列都相同，可預先填滿兩個 DMA buffer。
       */
      std::memset(
          dma_buffer1,
          rowPattern,
          packed_row_bytes
      );

      std::memset(
          dma_buffer2,
          rowPattern,
          packed_row_bytes
      );

      for (int row = 0;
           row < _config.height;
           ++row) {
        sendRow(
            row == 0,
            false,
            false
        );
      }

      /*
       * 每個 waveform frame 後讓出 CPU，
       * 同時避免長時間阻塞 watchdog。
       */
      vTaskDelay(1);
    }

    /*
     * Stage 2：GC16 quality waveform
     */
    for (size_t step = 0;
         step < epd_gc16::
                    kQualityStepCount;
         ++step) {
      const uint32_t lutFrame =
          epd_gc16::
              kQualityLut[step];

      for (int row = 0;
           row < _config.height;
           ++row) {
        const uint8_t* sourceRow =
            packed4bpp +
            static_cast<size_t>(row) *
                targetRowBytes;

        /*
         * dma_buffer 永遠指向目前可由 CPU 填寫的那一個 buffer。
         */
        gc16ConvertRow(
            sourceRow,
            dma_buffer,
            static_cast<size_t>(
                packed_row_bytes
            ),
            lutFrame
        );

        sendRow(
            row == 0,
            false,
            false
        );
      }

      vTaskDelay(1);
    }

    /*
     * 最後送出 neutral frame，關閉所有像素驅動。
     */
    std::memset(
        dma_buffer1,
        0x00,
        packed_row_bytes
    );

    std::memset(
        dma_buffer2,
        0x00,
        packed_row_bytes
    );

    for (int row = 0;
         row < _config.height;
         ++row) {
      sendRow(
          row == 0,
          row == _config.height - 1,
          false
      );
    }
  }

  xSemaphoreGive(
      _paint_buffer_sem
  );

  xSemaphoreGive(
      _paint_active_sem
  );

  /*
   * 注意：
   * 目前 existing packed_screenbuffer 是 2bpp，
   * 無法保存這張 16 階畫面。
   *
   * 第一階段測試後不要立刻切回一般 UI；
   * 應重新啟動裝置。
   */
  return Gc16Result::Success;
#endif
}

EPD_Painter::Gc16Result
EPD_Painter::paintGc16TestBars(
    const bool clearFirst
) {
#if !defined(EPD_PAINTER_PRESET_M5PAPER_S3)
  (void)clearFirst;

  return Gc16Result::UnsupportedDevice;
#else
  if (_config.width != 960 ||
      _config.height != 540) {
    return Gc16Result::UnsupportedGeometry;
  }

  const size_t targetRowBytes =
      static_cast<size_t>(
          _config.width
      ) /
      2;

  const size_t bufferSize =
      targetRowBytes *
      static_cast<size_t>(
          _config.height
      );

  uint8_t* testBuffer =
      static_cast<uint8_t*>(
          heap_caps_aligned_alloc(
              16,
              bufferSize,
              MALLOC_CAP_SPIRAM |
                  MALLOC_CAP_8BIT
          )
      );

  if (testBuffer == nullptr) {
    return Gc16Result::
        AllocationFailed;
  }

  const int barWidth =
      _config.width / 16;

  for (int y = 0;
       y < _config.height;
       ++y) {
    uint8_t* row =
        testBuffer +
        static_cast<size_t>(y) *
            targetRowBytes;

    for (int pairIndex = 0;
         pairIndex <
             _config.width / 2;
         ++pairIndex) {
      const int firstX =
          pairIndex * 2;

      const int secondX =
          firstX + 1;

      /*
       * LUT 定義：
       *   0  = 黑
       *   15 = 白
       *
       * 因此左到右顯示白 → 黑。
       */
      const uint8_t firstLevel =
          static_cast<uint8_t>(
              15 -
              firstX / barWidth
          );

      const uint8_t secondLevel =
          static_cast<uint8_t>(
              15 -
              secondX / barWidth
          );

      row[pairIndex] =
          static_cast<uint8_t>(
              (firstLevel << 4) |
              secondLevel
          );
    }
  }

  const Gc16Result result =
      paintGc16Full(
          testBuffer,
          bufferSize,
          clearFirst
      );

  heap_caps_free(
      testBuffer
  );

  return result;
#endif
}