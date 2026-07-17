#include "HalTouch.h"

bool HalTouch::begin(int sda, int scl, int intPin, uint8_t addr) {
  if (_initialized) {
    return true;
  }

  auto cfg = touch.config();
  cfg.pin_sda = sda;
  cfg.pin_scl = scl;
  cfg.pin_int = intPin;
  cfg.i2c_port = 1;
  cfg.i2c_addr = addr;
  cfg.freq = 400000;
  cfg.x_min = 0;
  cfg.x_max = 539;
  cfg.y_min = 0;
  cfg.y_max = 959;
  cfg.offset_rotation = 1;
  // BMI270 shares SDA/SCL with GT911 on Paper S3.  Keep the ESP-IDF I2C
  // controller shareable so M5Unified can access the IMU without taking the
  // touch controller offline.
  cfg.bus_shared = true;
  touch.config(cfg);

  _initialized = touch.init();
  if (Serial) {
    Serial.printf("[%lu] HalTouch: GT911 touch %s\n", millis(), _initialized ? "OK" : "FAIL");
  }
  return _initialized;
}

bool HalTouch::update() {
  _touched = false;
  _numPoints = 0;

  if (!_initialized) return false;

  lgfx::touch_point_t points[5];
  _numPoints = touch.getTouchRaw(points, 5);

  if (_numPoints > 0) {
    _x = points[0].x;
    _y = points[0].y;
    _touched = true;
  }

  return _touched;
}
