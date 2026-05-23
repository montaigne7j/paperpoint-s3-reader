#pragma once

#include <Arduino.h>

#include <lgfx/v1/touch/Touch_GT911.hpp>

// Minimal GT911 capacitive touch driver for M5PaperS3
// Uses the official M5Unified touch driver for M5PaperS3.
class HalTouch {
 public:
  HalTouch() = default;

  // Initialize the touch driver. Returns true on success.
  bool begin(int sda = 41, int scl = 42, int intPin = 48, uint8_t addr = 0x14);

  // Poll touch state. Call once per frame. Returns true if a touch is active.
  bool update();

  bool isTouched() const { return _touched; }
  uint8_t getNumPoints() const { return _numPoints; }
  int16_t getX() const { return _x; }
  int16_t getY() const { return _y; }

 private:
  lgfx::Touch_GT911 touch;
  int16_t _x = 0;
  int16_t _y = 0;
  uint8_t _numPoints = 0;
  bool _touched = false;
  unsigned long _lastTouchMs = 0;
  bool _initialized = false;
};
