/*
 * SPDX-License-Identifier: Apache-2.0
 * Derived from EPD_Painter by Tony Weston and contributors:
 * https://github.com/tonywestonuk/EPD_Painter
 *
 * Modified for CrossPoint/PaperPoint on 2026-06-20.
 * See lib/EPD_Painter/NOTICE and lib/EPD_Painter/LICENSE.
 */
#include "esp32-hal.h"
// Shutdown module removed for CrossPoint integration
#include <driver/periph_ctrl.h>
#include <epd_painter_powerctl.h>
#include <esp_private/gdma.h>
#include <hal/dma_types.h>
#include <hal/gpio_hal.h>
#include <soc/gdma_struct.h>
#include <soc/lcd_cam_struct.h>
#include <string.h>

#include <algorithm>
#include <cstring>

#include "EPD_Painter.h"
#include "build_opt.h"
#include "esp_heap_caps.h"
#include "esp_rom_gpio.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"

#ifdef ARDUINO
#include <Arduino.h>
#include "Wire.h"
#else
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#endif

// LCD_CAM signal indices for the 8 parallel data lines
static const uint8_t kDataSignals[8] = {
    LCD_DATA_OUT0_IDX, LCD_DATA_OUT1_IDX, LCD_DATA_OUT2_IDX, LCD_DATA_OUT3_IDX,
    LCD_DATA_OUT4_IDX, LCD_DATA_OUT5_IDX, LCD_DATA_OUT6_IDX, LCD_DATA_OUT7_IDX,
};

epd_painter_powerctl* powerctl = nullptr;

// Assembly routines — see EPD_Painter.S for full documentation
extern "C" void epd_painter_compact_pixels(const uint8_t* input, uint8_t* output, uint32_t size);

// =============================================================================
// compact_pixels_rotated_cw
//
// Combines 90° clockwise rotation and 8bpp→2bpp compaction in one pass.
//
// The portrait canvas is src_w wide × src_h tall (e.g. 540×960).
// The physical panel is src_h wide × src_w tall (e.g. 960×540).
//
// Rotation mapping (clockwise):
//   canvas pixel (col=x, row=y)  →  panel pixel (col=y, row=src_w-1-x)
//
// Processed in blocks of 16 portrait rows so the working set (16 × src_w bytes)
// stays warm in cache across all src_w column iterations within each block,
// avoiding repeated PSRAM fetches for the strided column access pattern.
//
// src_h must be a multiple of 16. src_w must be a multiple of 4.
// =============================================================================
static IRAM_ATTR void compact_pixels_rotated_cw(const uint8_t* src, uint8_t* dst, int src_w, int src_h) {
  const int out_stride = src_h / 4;  // packed bytes per output row (e.g. 240)

  for (int rb = 0; rb < src_h; rb += 16) {
    for (int cx = 0; cx < src_w; cx++) {
      // CW: canvas column cx → output row (src_w - 1 - cx)
      uint8_t* out = dst + (src_w - 1 - cx) * out_stride + rb / 4;
      const uint8_t* col = src + rb * src_w + cx;

      // 16 portrait rows → 4 packed output bytes, 4 pixels per byte.
      // Fully unrolled; all 16 loads are to addresses within the current
      // 16-row block (rb * src_w .. (rb+15) * src_w), which is warm in cache.
      out[0] = ((col[0] & 3) << 6) | ((col[src_w] & 3) << 4) | ((col[2 * src_w] & 3) << 2) | (col[3 * src_w] & 3);
      out[1] = ((col[4 * src_w] & 3) << 6) | ((col[5 * src_w] & 3) << 4) | ((col[6 * src_w] & 3) << 2) |
               (col[7 * src_w] & 3);
      out[2] = ((col[8 * src_w] & 3) << 6) | ((col[9 * src_w] & 3) << 4) | ((col[10 * src_w] & 3) << 2) |
               (col[11 * src_w] & 3);
      out[3] = ((col[12 * src_w] & 3) << 6) | ((col[13 * src_w] & 3) << 4) | ((col[14 * src_w] & 3) << 2) |
               (col[15 * src_w] & 3);
    }
  }
}

extern "C" void epd_painter_convert_packed_fb_to_ink(const uint8_t* packed_fb, uint8_t* output, uint32_t length,
                                                     const uint8_t* waveform, uint32_t chunk_flags);

extern "C" uint32_t epd_painter_ink_on(uint8_t* packed_fastbuffer, const uint8_t* packed_screenbuffer,
                                       uint32_t length_bytes);

extern "C" void epd_painter_ink_off(uint8_t* packed_fastbuffer, uint8_t* packed_screenbuffer, uint32_t length_bytes,
                                    uint32_t bitmask);

extern "C" void epd_painter_interleaved_copy(const uint8_t* input, uint8_t* output, int16_t width, int16_t height,
                                             bool interlace_period);

extern "C" uint32_t epd_painter_ink(uint8_t* packed_fastbuffer, uint8_t* packed_screenbuffer, uint32_t length,
                                    uint32_t bitmask);

static inline void epd_gpio_func_sel(int pin) { esp_rom_gpio_pad_select_gpio((gpio_num_t)pin); }

static inline void gpio_set_fast(uint8_t pin) {
  if (pin < 32) {
    REG_WRITE(GPIO_OUT_W1TS_REG, 1UL << pin);
  } else {
    REG_WRITE(GPIO_OUT1_W1TS_REG, 1UL << (pin - 32));
  }
}

static inline void gpio_clear_fast(uint8_t pin) {
  if (pin < 32) {
    REG_WRITE(GPIO_OUT_W1TC_REG, 1UL << pin);
  } else {
    REG_WRITE(GPIO_OUT1_W1TC_REG, 1UL << (pin - 32));
  }
}

#define PASS_COUNT 13


// =============================================================================
// Paper S3 page-turn transition-aware soft grayscale band-scan parameters
// =============================================================================
// This path is used only by paintRowMajor() / PAGE_TURN_REFRESH.
// It uses band-major scanning with per-pixel drive generation and a
// previous-frame-aware transition rule:
//   prev white -> curr white : optional lighter cleanup pass, then neutral
//   prev white -> curr non-white : use the current-frame schedule
//   prev black -> curr white : lighter * N, then neutral
//   prev black -> curr black : optional darker reinforcement pass, then neutral
//   all other transitions : use the current-frame schedule
//
// Stable Paper S3 defaults. Override from platformio.ini only for testing:
#ifndef EPD_PAGE_TURN_BAND_ROWS
#define EPD_PAGE_TURN_BAND_ROWS 540
#endif

#ifndef EPD_PAGE_TURN_PASS_COUNT
#define EPD_PAGE_TURN_PASS_COUNT 8
#endif

#ifndef EPD_PAGE_TURN_PASS_DELAY_MS
#define EPD_PAGE_TURN_PASS_DELAY_MS 5
#endif

// Independent hold time after page-turn pass 0.  Pass 0 is also where the
// same-white lighter cleanup and same-black darker reinforcement run.  The
// Paper S3 V1.8.0 profile intentionally keeps this short and uses the normal
// pass delay for the remaining passes.
#ifndef EPD_PAGE_TURN_FIRST_PASS_DELAY_MS
#define EPD_PAGE_TURN_FIRST_PASS_DELAY_MS 2
#endif

#ifndef EPD_PAGE_TURN_BLACK_DRIVE
#define EPD_PAGE_TURN_BLACK_DRIVE 0x55
#endif

#ifndef EPD_PAGE_TURN_WHITE_DRIVE
#define EPD_PAGE_TURN_WHITE_DRIVE 0xAA
#endif

#ifndef EPD_PAGE_TURN_SPECIAL_DRIVE
#define EPD_PAGE_TURN_SPECIAL_DRIVE 0xFF
#endif

#ifndef EPD_PAGE_TURN_ENABLE_WHITE_STAY_LIGHTER
#define EPD_PAGE_TURN_ENABLE_WHITE_STAY_LIGHTER 1
#endif

#ifndef EPD_PAGE_TURN_ENABLE_BLACK_STAY_DARKER
#define EPD_PAGE_TURN_ENABLE_BLACK_STAY_DARKER 1
#endif

#ifndef EPD_PAGE_TURN_WHITE_STAY_LIGHTER_PASSES
#define EPD_PAGE_TURN_WHITE_STAY_LIGHTER_PASSES 1
#endif

#ifndef EPD_PAGE_TURN_BLACK_STAY_DARKER_PASSES
#define EPD_PAGE_TURN_BLACK_STAY_DARKER_PASSES 1
#endif

#ifndef EPD_PAGE_TURN_BLACK_TO_WHITE_LIGHTER_PASSES
#define EPD_PAGE_TURN_BLACK_TO_WHITE_LIGHTER_PASSES 5
#endif

// Boot-time temperature settling profile for reader page turns.
// Paper S3 can appear over-driven immediately after boot because panel/PMIC
// temperature has not stabilized yet.  The first N full page-turn refreshes
// can therefore use a slightly lighter white->black / target-black darker
// schedule, then automatically switch to the stable schedule.
// V1.8.0 boot-settle profile: the first reader page turns after boot can
// use fewer target-black darker passes than later stable turns.  This helps
// avoid over-driving black pixels while panel temperature is still settling.
#ifndef EPD_PAGE_TURN_BOOT_SETTLE_TURNS
#define EPD_PAGE_TURN_BOOT_SETTLE_TURNS 15
#endif

#ifndef EPD_PAGE_TURN_BOOT_BLACK_DARKER_PASSES
#define EPD_PAGE_TURN_BOOT_BLACK_DARKER_PASSES 4
#endif

#ifndef EPD_PAGE_TURN_STABLE_BLACK_DARKER_PASSES
#define EPD_PAGE_TURN_STABLE_BLACK_DARKER_PASSES 6
#endif

// Current-frame 8-pass soft schedule:
//   target 00 white : lighter * 5, special(3) * 1, neutral * 2
//   target 01 gray1 : lighter * 5, darker * 1, neutral * 2
//   target 10 gray2 : lighter * 5, darker * 2, neutral * 1
//   target 11 black : darker  * 5, special(3) * 1, neutral * 2
static IRAM_ATTR uint8_t epd_painter_page_turn_current_drive_for_pixel(uint8_t current_pixel,
                                                                        uint8_t pass,
                                                                        uint8_t darker_drive,
                                                                        uint8_t lighter_drive,
                                                                        uint8_t special_drive,
                                                                        uint8_t black_darker_passes) {
  switch (current_pixel & 0x03) {
    case 0x00:  // white
      if (pass < 5) return lighter_drive;
      if (pass == 5) return special_drive;
      return 0x00;

    case 0x01:  // gray 1
      if (pass < 5) return lighter_drive;
      if (pass == 5) return darker_drive;
      return 0x00;

    case 0x02:  // gray 2
      if (pass < 5) return lighter_drive;
      if (pass < 7) return darker_drive;
      return 0x00;

    default: {  // black
      const uint8_t cappedBlackDarkerPasses = std::min<uint8_t>(black_darker_passes, 7);
      if (pass < cappedBlackDarkerPasses) return darker_drive;
      if (pass == cappedBlackDarkerPasses) return special_drive;
      return 0x00;
    }
  }
}

// Per-pixel transition-aware waveform schedule, 8 passes by default.
static IRAM_ATTR uint8_t epd_painter_page_turn_drive_for_pixel(uint8_t current_pixel,
                                                               uint8_t previous_pixel,
                                                               uint8_t pass,
                                                               uint8_t darker_drive,
                                                               uint8_t lighter_drive,
                                                               uint8_t special_drive,
                                                               uint8_t black_darker_passes) {
  const uint8_t curr = current_pixel & 0x03;
  const uint8_t prev = previous_pixel & 0x03;

  if (prev == 0x00 && curr == 0x00) {
#if EPD_PAGE_TURN_ENABLE_WHITE_STAY_LIGHTER
    // White stays white: optional light cleanup pulse(s), then neutral.
    return (pass < EPD_PAGE_TURN_WHITE_STAY_LIGHTER_PASSES) ? lighter_drive : 0x00;
#else
    return 0x00;
#endif
  }

  if (prev == 0x00 && curr != 0x00) {
    // White becomes gray/black: do not suppress the new ink; use current schedule.
    return epd_painter_page_turn_current_drive_for_pixel(curr, pass, darker_drive, lighter_drive, special_drive, black_darker_passes);
  }

  if (prev == 0x03 && curr == 0x00) {
    // Black becomes white: stronger cleanup path for old text ghosting.
    return (pass < EPD_PAGE_TURN_BLACK_TO_WHITE_LIGHTER_PASSES) ? lighter_drive : 0x00;
  }

  if (prev == 0x03 && curr == 0x03) {
#if EPD_PAGE_TURN_ENABLE_BLACK_STAY_DARKER
    // Black stays black: optional dark reinforcement pulse(s), then neutral.
    return (pass < EPD_PAGE_TURN_BLACK_STAY_DARKER_PASSES) ? darker_drive : 0x00;
#else
    return 0x00;
#endif
  }

  // Gray-related transitions keep the current-frame soft schedule.
  return epd_painter_page_turn_current_drive_for_pixel(curr, pass, darker_drive, lighter_drive, special_drive, black_darker_passes);
}

// Build one physical row of direct drive data from the current target frame and
// the previous software screen buffer.  Sources are packed 2bpp:
// 00=white, 01=gray1, 10=gray2, 11=black.
// Output is packed ink drive per pixel/column for the requested pass.
// This is per-pixel/column, not 64-pixel chunk scheduled.
static IRAM_ATTR void epd_painter_build_previous_aware_waveform_row(const uint8_t* current_row,
                                                                    const uint8_t* previous_row,
                                                                    uint8_t* output,
                                                                    uint32_t length_bytes,
                                                                    uint8_t pass,
                                                                    uint8_t darker_drive,
                                                                    uint8_t lighter_drive,
                                                                    uint8_t special_drive,
                                                                    uint8_t black_darker_passes) {
  for (uint32_t i = 0; i < length_bytes; ++i) {
    const uint8_t cur = current_row[i];
    const uint8_t prev = previous_row[i];

    const uint8_t c0 = (cur >> 6) & 0x03;
    const uint8_t c1 = (cur >> 4) & 0x03;
    const uint8_t c2 = (cur >> 2) & 0x03;
    const uint8_t c3 = cur & 0x03;

    const uint8_t p0 = (prev >> 6) & 0x03;
    const uint8_t p1 = (prev >> 4) & 0x03;
    const uint8_t p2 = (prev >> 2) & 0x03;
    const uint8_t p3 = prev & 0x03;

    const uint8_t d0 = epd_painter_page_turn_drive_for_pixel(c0, p0, pass, darker_drive, lighter_drive, special_drive, black_darker_passes);
    const uint8_t d1 = epd_painter_page_turn_drive_for_pixel(c1, p1, pass, darker_drive, lighter_drive, special_drive, black_darker_passes);
    const uint8_t d2 = epd_painter_page_turn_drive_for_pixel(c2, p2, pass, darker_drive, lighter_drive, special_drive, black_darker_passes);
    const uint8_t d3 = epd_painter_page_turn_drive_for_pixel(c3, p3, pass, darker_drive, lighter_drive, special_drive, black_darker_passes);

    output[i] = (d0 & 0xC0) | (d1 & 0x30) | (d2 & 0x0C) | (d3 & 0x03);
  }
}

EPD_Painter::EPD_Painter(const Config& config, bool portrait) {
  _config = config;
  if (portrait) _config.rotation = Rotation::ROTATION_CW;
}

void EPD_Painter::setQuality(Quality quality) { _config.quality = quality; }

// =============================================================================
// sendRow()
// =============================================================================
void EPD_Painter::sendRow(bool firstLine, bool lastLine, bool skipRow) {
  // Wait for LCD peripheral to finish consuming the previous row.
  // This also guarantees the previously-started DMA transfer is complete,
  // since the LCD FIFO cannot drain faster than DMA fills it.
  // long count=0;
  while (LCD_CAM.lcd_user.lcd_start) {
  }
  // printf("yielded %d \n",count);
  // delayMicroseconds(4);

  // dma_buffer points at the buffer the CPU just finished writing.
  // Start DMA on its matching descriptor, then swap so the next
  // convert_packed_fb_to_ink() writes into the now-idle buffer.
  // delayMicroseconds(10);
  dma_descriptor_t* desc;
  if (dma_buffer == dma_buffer1) {
    desc = &dma_desc1;
    dma_buffer = dma_buffer2;
  } else {
    desc = &dma_desc2;
    dma_buffer = dma_buffer1;
  }

  if (firstLine) {
    gpio_clear_fast(_config.pin_spv);
    gpio_clear_fast(_config.pin_ckv);
    EPD_DELAY_US(1);
    gpio_set_fast(_config.pin_ckv);
    gpio_set_fast(_config.pin_spv);
  } else {
    gpio_set_fast(_config.pin_le);
    gpio_clear_fast(_config.pin_le);

    gpio_clear_fast(_config.pin_ckv);
    EPD_DELAY_US(1);
    gpio_set_fast(_config.pin_ckv);
  }

  // Reset ownership, flush AFIFO, and restart GDMA from the correct descriptor.
  // This prevents free-running DMA from preloading the next buffer into the AFIFO
  // before the CPU has written new row data there (which caused left-side artifacts).
  desc->dw0.owner = DMA_DESCRIPTOR_BUFFER_OWNER_DMA;
  LCD_CAM.lcd_misc.lcd_afifo_reset = 1;
  gdma_start(dma_chan, (intptr_t)desc);

  LCD_CAM.lcd_user.lcd_start = 1;
  if (lastLine) {
    while (LCD_CAM.lcd_user.lcd_start) {
    }

    gpio_clear_fast(_config.pin_ckv);
    gpio_set_fast(_config.pin_le);
    EPD_DELAY_US(1);
    gpio_clear_fast(_config.pin_le);
    gpio_set_fast(_config.pin_ckv);
  }
}

// =============================================================================
// begin()
// =============================================================================
bool EPD_Painter::begin() {
  // -- Start I2C if needed.
#ifdef ARDUINO
  if (_config.i2c.scl != -1 && _config.i2c.wire == nullptr) {
    TwoWire* w = new TwoWire(0);
    w->begin(_config.i2c.sda, _config.i2c.scl, _config.i2c.freq);
    _config.i2c.wire = w;
    EPD_DELAY_MS(50);
  }
#else
  if (_config.i2c.scl != -1 && _config.i2c.i2c_bus == nullptr) {
    i2c_master_bus_config_t i2c_bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = (gpio_num_t)_config.i2c.sda,
        .scl_io_num = (gpio_num_t)_config.i2c.scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {.enable_internal_pullup = true},
    };
    i2c_master_bus_handle_t bus;
    esp_err_t err = i2c_new_master_bus(&i2c_bus_config, &bus);
    if (err != ESP_OK) {
      printf("EPD_Painter: I2C bus init failed (%d)\n", err);
      return false;
    }
    _config.i2c.i2c_bus = bus;
    EPD_DELAY_MS(50);
  }
#endif

  // ---- Configure EPD control pins ----
  // pin_pwr and pin_oe are -1 when managed by powerctl (e.g. LilyGo via PCA9555/TPS65185)
  if (_config.pin_pwr >= 0) EPD_PIN_OUTPUT(_config.pin_pwr);
  EPD_PIN_OUTPUT(_config.pin_spv);
  EPD_PIN_OUTPUT(_config.pin_ckv);
  EPD_PIN_OUTPUT(_config.pin_sph);
  if (_config.pin_oe >= 0) EPD_PIN_OUTPUT(_config.pin_oe);
  EPD_PIN_OUTPUT(_config.pin_le);
  EPD_PIN_OUTPUT(_config.pin_cl);

  packed_row_bytes = _config.width / 4;

  // ---- Enable and reset LCD_CAM peripheral ----
  periph_module_enable(PERIPH_LCD_CAM_MODULE);
  periph_module_reset(PERIPH_LCD_CAM_MODULE);
  LCD_CAM.lcd_user.lcd_reset = 1;
  EPD_DELAY_US(100);

  // ---- Configure LCD_CAM pixel clock ----
  LCD_CAM.lcd_clock.clk_en = 1;
  LCD_CAM.lcd_clock.lcd_clk_sel = 2;
  LCD_CAM.lcd_clock.lcd_ck_out_edge = 0;
  LCD_CAM.lcd_clock.lcd_ck_idle_edge = 0;
  LCD_CAM.lcd_clock.lcd_clk_equ_sysclk = 0;
  LCD_CAM.lcd_clock.lcd_clkm_div_num = 1;
  LCD_CAM.lcd_clock.lcd_clkm_div_a = 0;
  LCD_CAM.lcd_clock.lcd_clkm_div_b = 0;
  LCD_CAM.lcd_clock.lcd_clkcnt_n = 1;

  // ---- Configure LCD_CAM for i8080 8-bit parallel mode ----
  LCD_CAM.lcd_ctrl.lcd_rgb_mode_en = 0;
  LCD_CAM.lcd_rgb_yuv.lcd_conv_bypass = 0;
  LCD_CAM.lcd_misc.lcd_next_frame_en = 0;
  LCD_CAM.lcd_data_dout_mode.val = 0;
  LCD_CAM.lcd_user.lcd_always_out_en = 0;
  LCD_CAM.lcd_user.lcd_8bits_order = 0;
  LCD_CAM.lcd_user.lcd_bit_order = 0;
  LCD_CAM.lcd_user.lcd_2byte_en = 0;
  LCD_CAM.lcd_user.lcd_dummy = 0;
  LCD_CAM.lcd_user.lcd_dummy_cyclelen = 0;
  LCD_CAM.lcd_user.lcd_cmd = 0;
  LCD_CAM.lcd_user.lcd_dout_cyclelen = packed_row_bytes - 1;
  LCD_CAM.lcd_user.lcd_dout = 1;
  LCD_CAM.lcd_user.lcd_update = 1;

  // ---- Route 8-bit data bus pins using config ----
  for (int i = 0; i < 8; i++) {
    int8_t pin = _config.data_pins[i];
    esp_rom_gpio_connect_out_signal(pin, kDataSignals[i], false, false);
    epd_gpio_func_sel(GPIO_PIN_MUX_REG[pin]);
    gpio_set_drive_capability((gpio_num_t)pin, (gpio_drive_cap_t)3);
  }

  // ---- Route pixel clock to CL pin ----
  esp_rom_gpio_connect_out_signal(_config.pin_cl, LCD_PCLK_IDX, false, false);
  epd_gpio_func_sel(GPIO_PIN_MUX_REG[_config.pin_cl]);
  gpio_set_drive_capability((gpio_num_t)_config.pin_cl, (gpio_drive_cap_t)3);

  // ---- Allocate GDMA channel ----
  gdma_channel_alloc_config_t dma_chan_config = {
      .sibling_chan = NULL,
      .direction = GDMA_CHANNEL_DIRECTION_TX,
      .flags = {.reserve_sibling = 0},
  };
  gdma_new_channel(&dma_chan_config, &dma_chan);
  gdma_connect(dma_chan, GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_LCD, 0));

  // Cache the GDMA channel index for direct register access in sendRow().
  // LCD_CAM is peripheral 5 in the GDMA peri_sel register.
  _dma_channel_id = 0;
  for (int i = 0; i < 5; i++) {
    if (GDMA.channel[i].out.peri_sel.sel == 5) {
      _dma_channel_id = i;
      break;
    }
  }

  gdma_strategy_config_t strategy_config = {
      .owner_check = false,
      .auto_update_desc = false,
  };
  gdma_apply_strategy(dma_chan, &strategy_config);

  // ---- Allocate DMA row buffers ----
  dma_buffer1 =
      static_cast<uint8_t*>(heap_caps_aligned_alloc(16, packed_row_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  dma_buffer2 =
      static_cast<uint8_t*>(heap_caps_aligned_alloc(16, packed_row_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));

  dma_buffer = dma_buffer1;

  // ---- Set up DMA descriptors (one per buffer, stopped after each row) ----
  dma_desc2.dw0.owner = DMA_DESCRIPTOR_BUFFER_OWNER_DMA;
  dma_desc2.dw0.suc_eof = 1;
  dma_desc2.dw0.size = packed_row_bytes;
  dma_desc2.dw0.length = packed_row_bytes;
  dma_desc2.buffer = const_cast<uint8_t*>(dma_buffer2);
  dma_desc2.next = nullptr;

  dma_desc1.dw0.owner = DMA_DESCRIPTOR_BUFFER_OWNER_DMA;
  dma_desc1.dw0.suc_eof = 1;
  dma_desc1.dw0.size = packed_row_bytes;
  dma_desc1.dw0.length = packed_row_bytes;
  dma_desc1.buffer = const_cast<uint8_t*>(dma_buffer1);
  dma_desc1.next = nullptr;

  // ---- Allocate packed 2bpp framebuffers ----
  const size_t packed_size = (_config.width * _config.height) / 4;

  packed_fastbuffer = static_cast<uint8_t*>(heap_caps_aligned_alloc(16, packed_size, MALLOC_CAP_INTERNAL));
  if (!packed_fastbuffer) {
    packed_fastbuffer = static_cast<uint8_t*>(heap_caps_aligned_alloc(16, packed_size, MALLOC_CAP_SPIRAM));
  }

  packed_screenbuffer = static_cast<uint8_t*>(heap_caps_aligned_alloc(16, packed_size, MALLOC_CAP_SPIRAM));
  packed_paintbuffer = static_cast<uint8_t*>(heap_caps_aligned_alloc(16, packed_size, MALLOC_CAP_SPIRAM));

  // Zero-init so delta updates assume "all white" as initial screen state.
  // This avoids comparing against garbage on the first paint() call.
  if (packed_fastbuffer) memset(packed_fastbuffer, 0, packed_size);
  if (packed_screenbuffer) memset(packed_screenbuffer, 0, packed_size);
  if (packed_paintbuffer) memset(packed_paintbuffer, 0, packed_size);

  bitmask = static_cast<uint32_t*>(heap_caps_aligned_alloc(4, _config.height * 4, MALLOC_CAP_INTERNAL));

  // ── If a TPS chip is present, initialise the power controller ──
  if (_config.power.tps_addr != -1) {
    printf("\n── PowerCtl Init ──\n");
    powerctl = new epd_painter_powerctl();
    if (!powerctl->begin(_config)) {
      printf("FATAL: powerctl init failed!\n");
      while (1) EPD_DELAY_MS(1000);
    }
  }

  if (!(dma_buffer && packed_fastbuffer && packed_screenbuffer)) return false;

  _paint_active_sem = xSemaphoreCreateBinary();
  xSemaphoreGive(_paint_active_sem);
  _paint_buffer_sem = xSemaphoreCreateBinary();
  xSemaphoreGive(_paint_buffer_sem);

  xTaskCreatePinnedToCore(_paint_task_entry, "epd_paint", 8000, this, 10, &_paint_task_h, 0);

  return true;
}

// =============================================================================
// waitUntilIdle()
// =============================================================================
void EPD_Painter::waitUntilIdle() {
  if (_paint_active_sem == nullptr || _paint_task_h == nullptr) {
    return;
  }

  // The paint task owns this semaphore for the whole physical waveform.
  // Taking it therefore waits for every row/pass to finish, unlike paint(),
  // which only waits until the framebuffer has been copied by the task.
  xSemaphoreTake(_paint_active_sem, portMAX_DELAY);
  xSemaphoreGive(_paint_active_sem);
}

// =============================================================================
// end()
// =============================================================================
bool EPD_Painter::end() {
  // Do not delete the worker while it is still driving the panel.
  waitUntilIdle();

  // Shut the EPD rails down in a deterministic order before the board PMIC
  // removes power. The old code left this to the delayed idle task.
  PanelPowerGuard::shutdown(*this);

  if (_paint_task_h) {
    vTaskDelete(_paint_task_h);
    _paint_task_h = nullptr;
  }

  if (_paint_active_sem) {
    vSemaphoreDelete(_paint_active_sem);
    _paint_active_sem = nullptr;
  }

  if (_paint_buffer_sem) {
    vSemaphoreDelete(_paint_buffer_sem);
    _paint_buffer_sem = nullptr;
  }

  if (dma_chan) {
    gdma_disconnect(dma_chan);
    gdma_del_channel(dma_chan);
    dma_chan = nullptr;
  }
  periph_module_disable(PERIPH_LCD_CAM_MODULE);
  return true;
}

// =============================================================================
// Power control
// =============================================================================
void EPD_Painter::powerOn() {
  EPD_PIN_LOW(_config.pin_spv);
  EPD_PIN_LOW(_config.pin_sph);

  if (powerctl) {
    powerctl->powerOn();
  } else {
    EPD_PIN_HIGH(_config.pin_oe);
    EPD_DELAY_US(100);
    EPD_PIN_HIGH(_config.pin_pwr);
    EPD_DELAY_US(100);
  }

  gpio_clear_fast(_config.pin_spv);
  gpio_clear_fast(_config.pin_ckv);
  EPD_DELAY_US(1);

  gpio_set_fast(_config.pin_ckv);
  gpio_set_fast(_config.pin_spv);
}

void EPD_Painter::powerOff() {
  if (powerctl) {
    powerctl->powerOff();
  } else {
    EPD_PIN_LOW(_config.pin_oe);
    EPD_DELAY_US(100);
    EPD_PIN_LOW(_config.pin_pwr);
    EPD_DELAY_US(100);
  }
}

// Waveform tables are defined per-device in EPD_Painter_presets.h
// and stored in _config.waveforms.

// =============================================================================
// paint()
// =============================================================================
void EPD_Painter::paint(uint8_t* framebuffer) {
  if (!_paint_buffer_sem) return;
  xSemaphoreTake(_paint_buffer_sem, portMAX_DELAY);
  row_major_remaining_stages.store(0);

  if (_config.rotation == Rotation::ROTATION_CW)
    compact_pixels_rotated_cw(framebuffer, packed_paintbuffer, _config.height, _config.width);
  else
    epd_painter_compact_pixels(framebuffer, packed_paintbuffer, _config.width * _config.height);

  paintStage = (interlace_mode ? 3 : 2);
  xSemaphoreGive(_paint_buffer_sem);

  // wait until this buffer has been picked up by the paint loop.
  while (paintStage == (interlace_mode ? 3 : 2)) {
    vTaskDelay(1);
  }
}

// =============================================================================
// paintRowMajor() — previous-aware soft grayscale band scan.
//
// This keeps the band-major timing path and per-pixel row data
// generation, but adds the requested previous-frame override:
//   previous white : lighter*1 + neutral*7
//   previous black : darker*1  + neutral*7
//   previous gray  : use the current-frame 8-pass soft schedule
//
// Band height, pass count, and pass delay are compile-time parameters near the
// top of this file: EPD_PAGE_TURN_BAND_ROWS, EPD_PAGE_TURN_PASS_COUNT,
// EPD_PAGE_TURN_PASS_DELAY_MS.
//
// Reader-page-turn-only path tuned for Paper S3.
// =============================================================================
void EPD_Painter::paintRowMajor(uint8_t* framebuffer, bool reverseBandOrder) {
  if (!_paint_buffer_sem) return;
  xSemaphoreTake(_paint_buffer_sem, portMAX_DELAY);

  if (_config.rotation == Rotation::ROTATION_CW)
    compact_pixels_rotated_cw(framebuffer, packed_paintbuffer, _config.height, _config.width);
  else
    epd_painter_compact_pixels(framebuffer, packed_paintbuffer, _config.width * _config.height);

  const uint32_t pageTurnNumber = row_major_page_turn_counter.fetch_add(1) + 1;
  const uint32_t settleTurns = EPD_PAGE_TURN_BOOT_SETTLE_TURNS;
  const bool settled = (settleTurns == 0) || (pageTurnNumber > settleTurns);
  const uint8_t blackDarkerPasses = settled ? EPD_PAGE_TURN_STABLE_BLACK_DARKER_PASSES : EPD_PAGE_TURN_BOOT_BLACK_DARKER_PASSES;
  row_major_black_darker_passes.store(blackDarkerPasses);
#ifdef ARDUINO
  Serial.printf("[EPD] Page-turn waveform profile: turn=%lu settleTurns=%lu blackDarkerPasses=%u profile=%s\n",
                static_cast<unsigned long>(pageTurnNumber),
                static_cast<unsigned long>(settleTurns),
                static_cast<unsigned>(blackDarkerPasses),
                settled ? "stable" : "boot");
#endif

  // One row/band-major stage only.  This intentionally
  // avoids the normal two paint stages so the scan test is fast enough to use.
  row_major_reverse_bands.store(reverseBandOrder);
  row_major_active_row_start.store(-1);
  row_major_active_row_end.store(-1);
  row_major_remaining_stages.store(interlace_mode ? 0 : 1);
  paintStage = (interlace_mode ? 3 : 1);
  xSemaphoreGive(_paint_buffer_sem);

  // wait until this buffer has been picked up by the paint loop.
  while (paintStage == (interlace_mode ? 3 : 1)) {
    vTaskDelay(1);
  }
}

void EPD_Painter::paintRowRange(uint8_t* framebuffer, int activeRowStart, int activeRowEnd) {
  if (!_paint_buffer_sem) return;
  if (activeRowStart > activeRowEnd) return;

  activeRowStart = std::max(0, std::min<int>(_config.height - 1, activeRowStart));
  activeRowEnd = std::max(0, std::min<int>(_config.height - 1, activeRowEnd));
  if (activeRowStart > activeRowEnd) return;

  xSemaphoreTake(_paint_buffer_sem, portMAX_DELAY);

  if (_config.rotation == Rotation::ROTATION_CW)
    compact_pixels_rotated_cw(framebuffer, packed_paintbuffer, _config.height, _config.width);
  else
    epd_painter_compact_pixels(framebuffer, packed_paintbuffer, _config.width * _config.height);

  // Same transition-aware row-major waveform as paintRowMajor(), but the
  // paint task will drive only this physical row range and leave all other
  // rows neutral.  The software screenbuffer is updated only for this range.
  row_major_reverse_bands.store(false);
  row_major_active_row_start.store(activeRowStart);
  row_major_active_row_end.store(activeRowEnd);
  row_major_remaining_stages.store(interlace_mode ? 0 : 1);
  paintStage = (interlace_mode ? 3 : 1);
  xSemaphoreGive(_paint_buffer_sem);

  // Wait until the paint task has picked up packed_paintbuffer.  The waveform
  // itself continues asynchronously, matching the existing paintRowMajor()
  // behavior used by reader page turns.
  while (paintStage == (interlace_mode ? 3 : 1)) {
    vTaskDelay(1);
  }
}

// =============================================================================
// paintPacked() — like paint() but skips compaction; buffer is already 2bpp
// =============================================================================
void EPD_Painter::paintPacked(const uint8_t* packed) {
  if (!_paint_buffer_sem) return;
  xSemaphoreTake(_paint_buffer_sem, portMAX_DELAY);
  row_major_remaining_stages.store(0);
  memcpy(packed_paintbuffer, packed, (_config.width * _config.height) / 4);
  paintStage = (interlace_mode ? 3 : 2);
  xSemaphoreGive(_paint_buffer_sem);

  while (paintStage == (interlace_mode ? 3 : 2)) {
    vTaskDelay(1);
  }
}

// =============================================================================
// unpaintPacked() — DC-balance pass: tells the driver the screen currently
// shows 'packed', then drives a blank (all-zero) frame so every pixel that
// was darkened gets a matching lightening pulse.
// =============================================================================
void EPD_Painter::unpaintPacked(const uint8_t* packed) {
  if (!_paint_buffer_sem) return;
  xSemaphoreTake(_paint_buffer_sem, portMAX_DELAY);
  row_major_remaining_stages.store(0);
  memcpy(packed_screenbuffer, packed, packed_row_bytes * _config.height);
  memset(packed_paintbuffer, 0x00, packed_row_bytes * _config.height);
  paintStage = (interlace_mode ? 3 : 2);
  xSemaphoreGive(_paint_buffer_sem);

  while (paintStage == (interlace_mode ? 3 : 2)) {
    vTaskDelay(1);
  }
}

// =============================================================================
// paintLater
// =============================================================================
void EPD_Painter::paintLater(uint8_t* framebuffer) {
  if (!_paint_buffer_sem) return;
  xSemaphoreTake(_paint_buffer_sem, portMAX_DELAY);
  row_major_remaining_stages.store(0);
  // const int64_t t0 = esp_timer_get_time();

  if (_config.rotation == Rotation::ROTATION_CW)
    compact_pixels_rotated_cw(framebuffer, packed_paintbuffer, _config.height, _config.width);
  else
    epd_painter_compact_pixels(framebuffer, packed_paintbuffer, _config.width * _config.height);

  // printf("[rotate] compact_pixels_rotated_cw: %lld us\n", esp_timer_get_time() - t0);

  paintStage = interlace_mode ? 3 : 2;
  xSemaphoreGive(_paint_buffer_sem);
}

// =============================================================================
// _paint_task_entry() / _paint_task_body()
// =============================================================================
void EPD_Painter::_paint_task_entry(void* arg) { static_cast<EPD_Painter*>(arg)->_paint_task_body(); }

void EPD_Painter::_paint_task_body() {
  for (;;) {
    if (paintStage == 0) {
      xSemaphoreGive(_paint_active_sem);
      while (paintStage == 0) {
        vTaskDelay(1);
      }
      xSemaphoreTake(_paint_active_sem, portMAX_DELAY);
    }

    xSemaphoreTake(_paint_buffer_sem, portMAX_DELAY);
    memcpy(packed_fastbuffer, packed_paintbuffer, packed_row_bytes * _config.height);
    const bool rowMajorThisStage = (row_major_remaining_stages.load() > 0) && !interlace_mode;
    const bool reverseBandOrderThisStage = rowMajorThisStage && row_major_reverse_bands.load();
    const int activeRowStartThisStage = rowMajorThisStage ? row_major_active_row_start.load() : -1;
    const int activeRowEndThisStage = rowMajorThisStage ? row_major_active_row_end.load() : -1;
    const bool rowRangeThisStage = rowMajorThisStage && activeRowStartThisStage >= 0 && activeRowEndThisStage >= activeRowStartThisStage;
    if (rowMajorThisStage) {
      row_major_remaining_stages.fetch_sub(1);
      // Row-range mode is one-shot.  Clear it as soon as this stage owns the
      // values so a later full paintRowMajor() does not inherit the range.
      if (rowRangeThisStage) {
        row_major_active_row_start.store(-1);
        row_major_active_row_end.store(-1);
      }
    }
    xSemaphoreGive(_paint_buffer_sem);

    paintStage -= 1;

    PanelPowerGuard guard(*this);

    if (!rowMajorThisStage) {
      for (int row = 0; row < _config.height; row++) {
        uint8_t* fb_row = packed_fastbuffer + row * packed_row_bytes;
        uint8_t* sb_row = packed_screenbuffer + row * packed_row_bytes;

        if (interlace_mode) {
          bitmask[row] = epd_painter_ink(fb_row, sb_row, packed_row_bytes, row % 2 ? 0xffffffff : 0x00);
        } else {
          bitmask[row] = epd_painter_ink(fb_row, sb_row, packed_row_bytes, 0xffffffff);
        }
      }
    }

    const uint8_t* lt_wf;
    const uint8_t* dk_wf;
    int wf_len;

    if (_config.quality == Quality::QUALITY_FAST) {
      lt_wf = &_config.waveforms.fast_lighter[0][0];
      dk_wf = &_config.waveforms.fast_darker[0][0];
      wf_len = 7;
    } else if (_config.quality == Quality::QUALITY_NORMAL) {
      lt_wf = &_config.waveforms.normal_lighter[0][0];
      dk_wf = &_config.waveforms.normal_darker[0][0];
      wf_len = 13;
    } else {
      lt_wf = &_config.waveforms.high_lighter[0][0];
      dk_wf = &_config.waveforms.high_darker[0][0];
      wf_len = 13;
    }

    if (rowMajorThisStage) {
      /*
       * Transition-aware soft grayscale band scan.
       *
       * This page-turn-only path bypasses the normal 64-pixel chunk
       * darker/lighter scheduling.  Each physical pixel/column is driven by
       * the previous -> current transition:
       *   prev 00 + curr 00 : optional lighter cleanup + neutral
       *   prev 00 + curr !=00: use current-frame schedule
       *   prev 11 + curr 00 : lighter*N + neutral
       *   prev 11 + curr 11 : optional darker reinforcement + neutral
       *   other transitions : use current-frame schedule
       *
       * Current-frame schedule:
       *   current 00 white : lighter*5 + special*1 + neutral*2
       *   current 01 gray1 : lighter*5 + darker*1 + neutral*2
       *   current 10 gray2 : lighter*5 + darker*2 + neutral*1
       *   current 11 black : darker*5  + special*1 + neutral*2
       *
       * The scan remains band-major:
       *   band rows 0..N-1 receive pass 0..P-1, then next band, ...
       *
       * Tunables are at the top of this file:
       *   EPD_PAGE_TURN_BAND_ROWS
       *   EPD_PAGE_TURN_PASS_COUNT
       *   EPD_PAGE_TURN_PASS_DELAY_MS
       *   EPD_PAGE_TURN_FIRST_PASS_DELAY_MS
       *   EPD_PAGE_TURN_ENABLE_WHITE_STAY_LIGHTER
       *   EPD_PAGE_TURN_ENABLE_BLACK_STAY_DARKER
       */
      static constexpr int kRowsPerBand = EPD_PAGE_TURN_BAND_ROWS;
      static constexpr int kBandPassCount = EPD_PAGE_TURN_PASS_COUNT;
      static constexpr int kBandPassDelayMs = EPD_PAGE_TURN_PASS_DELAY_MS;
      static constexpr int kBandFirstPassDelayMs = EPD_PAGE_TURN_FIRST_PASS_DELAY_MS;
      static constexpr uint8_t kBlackDrive = EPD_PAGE_TURN_BLACK_DRIVE;
      static constexpr uint8_t kWhiteDrive = EPD_PAGE_TURN_WHITE_DRIVE;
      static constexpr uint8_t kSpecialDrive = EPD_PAGE_TURN_SPECIAL_DRIVE;
      const uint8_t kBlackDarkerPasses = row_major_black_darker_passes.load();

      memset(dma_buffer1, 0x00, packed_row_bytes);
      memset(dma_buffer2, 0x00, packed_row_bytes);

      const int bandCount = rowRangeThisStage ? 1 : ((_config.height + kRowsPerBand - 1) / kRowsPerBand);
      for (int bandIndex = 0; bandIndex < bandCount; ++bandIndex) {
        int band_start = 0;
        int band_end = _config.height - 1;
        if (rowRangeThisStage) {
          band_start = std::max(0, std::min<int>(_config.height - 1, activeRowStartThisStage));
          band_end = std::max(0, std::min<int>(_config.height - 1, activeRowEndThisStage));
        } else {
          const int logicalBandIndex = reverseBandOrderThisStage ? (bandCount - 1 - bandIndex) : bandIndex;
          band_start = logicalBandIndex * kRowsPerBand;
          band_end = std::min(_config.height - 1, band_start + kRowsPerBand - 1);
        }

        for (uint8_t pass = 0; pass < kBandPassCount; pass++) {
          // Hardware gate scan is still physical row 0→N.  To avoid mirrored rows,
          // reverse mode changes only the active band order; each pass still sends
          // rows in physical order and uses neutral data before/after the active band.
          for (int row = 0; row < _config.height; row++) {
            if (row >= band_start && row <= band_end) {
              const uint8_t* fb_row = packed_fastbuffer + row * packed_row_bytes;
              const uint8_t* prev_row = packed_screenbuffer + row * packed_row_bytes;
              epd_painter_build_previous_aware_waveform_row(fb_row, prev_row, dma_buffer, packed_row_bytes, pass, kBlackDrive, kWhiteDrive, kSpecialDrive, kBlackDarkerPasses);
            } else {
              memset(dma_buffer, 0x00, packed_row_bytes);
            }
            sendRow(row == 0, row == _config.height - 1, false);
          }

          const int passDelayMs = (pass == 0) ? kBandFirstPassDelayMs : kBandPassDelayMs;
          if (passDelayMs > 0) {
            EPD_DELAY_MS(passDelayMs);
          }
        }

        if ((bandIndex & 0x07) == 0) {
          vTaskDelay(1);  // keep the WDT fed during long page-turn scans
        }
      }

      // Keep the software screenbuffer coherent for the next normal UI refresh.
      // Full band-scan declares the entire target frame current; row-range mode
      // updates only the rows that actually received drive pulses.
      if (rowRangeThisStage) {
        const int copy_start = std::max(0, std::min<int>(_config.height - 1, activeRowStartThisStage));
        const int copy_end = std::max(0, std::min<int>(_config.height - 1, activeRowEndThisStage));
        if (copy_start <= copy_end) {
          memcpy(packed_screenbuffer + copy_start * packed_row_bytes,
                 packed_fastbuffer + copy_start * packed_row_bytes,
                 static_cast<size_t>(copy_end - copy_start + 1) * packed_row_bytes);
        }
      } else {
        memcpy(packed_screenbuffer, packed_fastbuffer, packed_row_bytes * _config.height);
      }
    } else {
      for (uint8_t pass = 0; pass < wf_len; pass++) {
        uint8_t lighter_wf[3] = {(uint8_t)(lt_wf[2 * wf_len + pass] * 0x55),
                                 (uint8_t)(lt_wf[1 * wf_len + pass] * 0x55),
                                 (uint8_t)(lt_wf[0 * wf_len + pass] * 0x55)};
        uint8_t darker_wf[3] = {(uint8_t)(dk_wf[2 * wf_len + pass] * 0x55),
                                (uint8_t)(dk_wf[1 * wf_len + pass] * 0x55),
                                (uint8_t)(dk_wf[0 * wf_len + pass] * 0x55)};

        for (int row = 0; row < _config.height; row++) {
          uint8_t* fb_row = packed_fastbuffer + row * packed_row_bytes;
          epd_painter_convert_packed_fb_to_ink(fb_row, dma_buffer, packed_row_bytes, darker_wf, bitmask[row]);
          epd_painter_convert_packed_fb_to_ink(fb_row, dma_buffer, packed_row_bytes, lighter_wf, ~bitmask[row]);
          sendRow(row == 0, false, false);
        }

        if (_config.quality == Quality::QUALITY_HIGH) {
          EPD_DELAY_MS(8);
        } else if (_config.quality == Quality::QUALITY_NORMAL) {
          EPD_DELAY_MS(2);
        }
      }
    }

    vTaskDelay(1);  // yield once per frame: feeds WDT and lets application task run

    memset(dma_buffer1, 0x00, packed_row_bytes);
    memset(dma_buffer2, 0x00, packed_row_bytes);
    for (int row = 0; row < _config.height; ++row) {
      sendRow(row == 0, row == _config.height - 1);
    }
  }
}

// =============================================================================
// clear()
// =============================================================================
void EPD_Painter::clear() {
  if (!_paint_buffer_sem) return;

  const int packed_row_bytes = _config.width / 4;

  // First paint it white.
  xSemaphoreTake(_paint_buffer_sem, portMAX_DELAY);
  memset(packed_paintbuffer, 0x00, packed_row_bytes * _config.height);
  paintStage = 1;  /// only needs 1 pass.
  xSemaphoreGive(_paint_buffer_sem);

  while (paintStage == 1) {
    vTaskDelay(1);  // Wait until paintloop starts up again
  }

  // Wait until paintloop is idle.. By taking it, prevents paint loop from starting again.
  xSemaphoreTake(_paint_active_sem, portMAX_DELAY);

  PanelPowerGuard guard(*this);
  const uint8_t* lt_wf;
  int wf_len;

  // Send clear
  for (int phase = 0; phase < 4; phase++) {
    uint8_t pattern = (phase % 2 == 0) ? 0b01010101 : 0b10101010;
    memset(dma_buffer1, pattern, packed_row_bytes);
    memset(dma_buffer2, pattern, packed_row_bytes);

    int totpass[] = {6, 2, 4, 8};  // 6 blacks, 2 whites, 4 blacks 8 whites.

    for (int passes = 0; passes < totpass[phase]; passes++) {
      for (int row = 0; row < _config.height; ++row) {
        sendRow(row == 0);
      }
      EPD_DELAY_MS(5);
    }
  }

  // Send neutral..
  memset(dma_buffer1, 0x00, packed_row_bytes);
  memset(dma_buffer2, 0x00, packed_row_bytes);
  for (int row = 0; row < _config.height; ++row) {
    sendRow(row == 0, row == _config.height - 1);
  }

  xSemaphoreGive(_paint_active_sem);
}
// =============================================================================
// fxClear() — sweeping bar clear effect
//
// A thick bar travels left-to-right across the panel. Within the bar, each
// pixel is independently and randomly driven black or white on every pass,
// so adjacent pixels may be heading in opposite directions — the "fuzz".
// Left of the bar, pixels are settled white; right of the bar they are driven
// black and wait for the bar to sweep over them.
//
// Tuning constants (pixels, dimensionless):
//   BAR_W   — width of the active bar
//   STEP    — columns advanced per step
//   NPASSES — render passes per step (more = slower but better clearing)
// =============================================================================
void EPD_Painter::fxClear() {
  if (!_paint_active_sem) return;
  const int prb = _config.width / 4;  // packed row bytes

  // Wait for any in-progress paint to finish, then take exclusive control.
  // Unlike clear(), we do NOT issue a white frame first — the screenbuffer
  // must still reflect the current display state for the pixel mask to work.
  xSemaphoreTake(_paint_active_sem, portMAX_DELAY);

  PanelPowerGuard guard(*this);

  const int H = _config.height;

  dma_buffer = dma_buffer1;

  // All white (0b00) screenbuffer pixels are driven fully black in the dark
  // zone and fully white in the light zone — no partial selection. This gives
  // maximum contrast: a solid black leading edge and a solid white trailing
  // edge. DC balance is maintained because dark and light zones are equal size.
  const int BAR_DARK = 180;   // rows of black voltage (leading edge)
  const int BAR_LIGHT = 180;  // rows of white voltage (trailing edge)
  const int BAR_H = BAR_DARK + BAR_LIGHT;
  const int STEP = 15;  // 1 row per step → 60 dark + 60 light pulses per pixel

  for (int bar_top = -BAR_H; bar_top <= H; bar_top += STEP) {
    const int dark_top = bar_top + BAR_LIGHT;  // dark zone: lower (leading) half
    const int bar_bot = bar_top + BAR_H;

    for (int row = 0; row < H; row++) {
      uint32_t* buf32 = reinterpret_cast<uint32_t*>(dma_buffer);

      if (row >= bar_top && row < dark_top) {
        // Light phase (upper/trailing): all white pixels → white voltage
        for (int i = 0; i < prb / 4; i++) {
          const uint32_t* sb32 = reinterpret_cast<const uint32_t*>(packed_screenbuffer + row * prb) + i;
          uint32_t either = (*sb32 | (*sb32 >> 1)) & 0x55555555u;
          uint32_t pixel_mask = ~(either | (either << 1));
          buf32[i] = 0xAAAAAAAAu & pixel_mask;
        }
      } else if (row >= dark_top && row < bar_bot) {
        // Dark phase (lower/leading): all white pixels → black voltage
        for (int i = 0; i < prb / 4; i++) {
          const uint32_t* sb32 = reinterpret_cast<const uint32_t*>(packed_screenbuffer + row * prb) + i;
          uint32_t either = (*sb32 | (*sb32 >> 1)) & 0x55555555u;
          uint32_t pixel_mask = ~(either | (either << 1));
          buf32[i] = 0x55555555u & pixel_mask;
        }
      } else {
        memset(dma_buffer, 0x00, prb);
      }
      sendRow(row == 0, false);
    }
  }

  // Final neutral flush — de-energise all pixels
  memset(dma_buffer1, 0x00, prb);
  memset(dma_buffer2, 0x00, prb);
  for (int row = 0; row < H; ++row) {
    sendRow(row == 0, row == H - 1);
  }

  // Screenbuffer is unchanged — non-white pixels were never driven, white pixels
  // were already 0x00. No update needed.

  xSemaphoreGive(_paint_active_sem);
}

// =============================================================================
// dither()
//
// Converts an 8bpp greyscale framebuffer (0=black … 255=white) in-place to
// the driver's 4-level encoding:  0=white  1=light-grey  2=dark-grey  3=black
//
// Algorithm: Floyd-Steinberg error diffusion.
// A single row of int16 scratch (next_err[width]) carries the below-row error
// forward so the main buffer never needs to be widened beyond uint8.
//
// Error distribution:
//          [x]  →  right:        7/16
//   below-left:  3/16   below:  5/16   below-right:  1/16
// =============================================================================
void EPD_Painter::dither(uint8_t* fb, uint16_t width, uint16_t height) {
  // Representative 8bpp values for each 2bpp level (0=white … 3=black)
  static const uint8_t kLevel8[4] = {255, 170, 85, 0};

  int16_t* next_err =
      (int16_t*)heap_caps_malloc((size_t)width * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!next_err) return;
  memset(next_err, 0, (size_t)width * sizeof(int16_t));

  for (uint16_t y = 0; y < height; y++) {
    uint8_t* row = fb + (size_t)y * width;
    int16_t carry = 0;

    for (uint16_t x = 0; x < width; x++) {
      int32_t val = (int32_t)row[x] + carry + next_err[x];
      next_err[x] = 0;
      if (val < 0) val = 0;
      if (val > 255) val = 255;

      // Quantise to nearest level (0=white … 3=black)
      uint8_t q;
      if (val >= 213)
        q = 0;  // white
      else if (val >= 128)
        q = 1;  // light grey
      else if (val >= 43)
        q = 2;  // dark grey
      else
        q = 3;  // black

      row[x] = q;

      int32_t err = val - (int32_t)kLevel8[q];

      carry = (err * 7) >> 4;
      if (y + 1 < height) {
        if (x > 0) next_err[x - 1] += (int16_t)((err * 3) >> 4);
        next_err[x] += (int16_t)((err * 5) >> 4);
        if (x + 1 < width) next_err[x + 1] += (int16_t)((err * 1) >> 4);
      }
    }
  }

  heap_caps_free(next_err);
}
