#pragma once

#include <HalGPIO.h>

class MappedInputManager {
 public:
  enum class Button { Back, Confirm, Left, Right, Up, Down, Power, PageBack, PageForward };

  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };

  explicit MappedInputManager(HalGPIO& gpio) : gpio(gpio) {}

  void update() const { gpio.update(); }
  void clearState() const { gpio.clearState(); }

  // Raw touch coordinate access for tap-to-select navigation
  int16_t getTouchX() const { return gpio.getLastTouchX(); }
  int16_t getTouchY() const { return gpio.getLastTouchY(); }
  int16_t getContentTapX() const { return gpio.getContentTapX(); }
  int16_t getContentTapY() const { return gpio.getContentTapY(); }
  bool wasContentTapReleased() const { return gpio.wasContentTapReleased(); }
  // Footer nav bar height — forwarded to HalGPIO for touch zone mapping.
  void setFooterHeight(int16_t height) { gpio.setFooterHeight(height); }
  void setTouchOrientation(uint8_t orientation) { gpio.setTouchOrientation(orientation); }

  // Returns true if any single-finger tap was released in a reader zone.
  // Only active in reader/keyboard mode (footerHeight == 0); in footer mode
  // all navigation goes through the footer buttons, so this returns false.
  bool wasTapped() const {
    if (gpio.getFooterHeight() > 0) return false;
    // On the release frame, BTN_TWO_FINGER / BTN_SWIPE_* are SET in currentState
    // while the zone button transitions from previousState, so use isPressed guards.
    if (gpio.isPressed(HalGPIO::BTN_TWO_FINGER)) return false;
    if (gpio.isPressed(HalGPIO::BTN_SWIPE_UP) || gpio.isPressed(HalGPIO::BTN_SWIPE_DOWN)) return false;
    return gpio.wasReleased(HalGPIO::BTN_LEFT) || gpio.wasReleased(HalGPIO::BTN_CONFIRM) ||
           gpio.wasReleased(HalGPIO::BTN_RIGHT);
  }
  bool wasPressed(Button button) const;
  bool wasReleased(Button button) const;
  bool isPressed(Button button) const;
  bool wasAnyPressed() const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;
  Labels mapLabels(const char* back, const char* confirm, const char* previous, const char* next) const;
  // Returns the raw front button index that was pressed this frame (or -1 if none).
  int getPressedFrontButton() const;

 private:
  HalGPIO& gpio;

  bool mapButton(Button button, bool (HalGPIO::*fn)(uint8_t) const) const;
};
