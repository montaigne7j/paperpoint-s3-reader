#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "I18n.h"
#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class ReaderValueAdjustActivity final : public Activity {
 public:
  ReaderValueAdjustActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, StrId titleId,
                            uint8_t initialValue, uint8_t minValue, uint8_t maxValue, uint8_t stepValue,
                            std::string suffix, std::string helpText,
                            std::function<void(uint8_t)> applyFn)
      : Activity("ReaderValueAdjust", renderer, mappedInput),
        titleId(titleId),
        value(initialValue),
        minValue(minValue),
        maxValue(maxValue),
        stepValue(stepValue),
        suffix(std::move(suffix)),
        helpText(std::move(helpText)),
        applyFn(std::move(applyFn)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return false; }

 private:
  StrId titleId;
  uint8_t value;
  uint8_t minValue;
  uint8_t maxValue;
  uint8_t stepValue;
  std::string suffix;
  std::string helpText;
  std::function<void(uint8_t)> applyFn;
  ButtonNavigator buttonNavigator;

  void adjust(int direction);
  void applyCurrentValue();
};
