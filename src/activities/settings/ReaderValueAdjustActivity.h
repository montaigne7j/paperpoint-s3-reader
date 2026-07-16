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
                            int16_t initialValue, int16_t minValue, int16_t maxValue, int16_t stepValue,
                            std::string suffix, std::string helpText,
                            std::function<void(int16_t)> applyFn)
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
  int16_t value;
  int16_t minValue;
  int16_t maxValue;
  int16_t stepValue;
  std::string suffix;
  std::string helpText;
  std::function<void(int16_t)> applyFn;
  ButtonNavigator buttonNavigator;

  void adjust(int direction);
  void applyCurrentValue();
};
