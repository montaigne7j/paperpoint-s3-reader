#pragma once

#include <cstdint>

#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class ReaderFontSizeActivity final : public Activity {
 public:
  ReaderFontSizeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                         uint8_t initialSize)
      : Activity("ReaderFontSize", renderer, mappedInput), value(initialSize) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return false; }

 private:
  uint8_t value = 36;
  ButtonNavigator buttonNavigator;

  void adjust(int delta);
};
