#include "ReaderValueAdjustActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <string>
#include <utility>

#include "components/UITheme.h"
#include "fontIds.h"

void ReaderValueAdjustActivity::onEnter() {
  Activity::onEnter();
  value = std::min<uint8_t>(maxValue, std::max<uint8_t>(minValue, value));
  applyCurrentValue();
  requestUpdate();
}

void ReaderValueAdjustActivity::applyCurrentValue() {
  if (applyFn) {
    applyFn(value);
  }
}

void ReaderValueAdjustActivity::adjust(const int direction) {
  const int delta = direction * static_cast<int>(std::max<uint8_t>(1, stepValue));
  const int next = std::clamp<int>(static_cast<int>(value) + delta, minValue, maxValue);
  if (next == value) return;
  value = static_cast<uint8_t>(next);
  applyCurrentValue();
  requestUpdate();
}

void ReaderValueAdjustActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    setResult(FontSizeResult{value});
    finish();
    return;
  }

  buttonNavigator.onPressAndContinuous(
      {MappedInputManager::Button::Up, MappedInputManager::Button::Left},
      [this] { adjust(-1); });
  buttonNavigator.onPressAndContinuous(
      {MappedInputManager::Button::Down, MappedInputManager::Button::Right},
      [this] { adjust(+1); });
}

void ReaderValueAdjustActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, I18N.get(titleId));

  const std::string valueText = std::to_string(value) + suffix;
  const int valueY = metrics.topPadding + metrics.headerHeight + 105;
  renderer.drawCenteredText(UI_12_FONT_ID, valueY, valueText.c_str(), true, EpdFontFamily::BOLD);

  constexpr int barSidePadding = 60;
  constexpr int barHeight = 16;
  const int barX = barSidePadding;
  const int barWidth = pageWidth - barSidePadding * 2;
  const int barY = valueY + 75;
  renderer.drawRect(barX, barY, barWidth, barHeight);

  const int range = static_cast<int>(maxValue) - static_cast<int>(minValue);
  const int progress = static_cast<int>(value) - static_cast<int>(minValue);
  const int fillWidth = range > 0 ? (barWidth - 4) * progress / range : 0;
  if (fillWidth > 0) {
    renderer.fillRect(barX + 2, barY + 2, fillWidth, barHeight - 4, true);
  }

  const std::string rangeText = std::to_string(minValue) + " - " + std::to_string(maxValue) + suffix;
  renderer.drawCenteredText(UI_10_FONT_ID, barY + 38, rangeText.c_str());

  if (!helpText.empty()) {
    const int noteY = std::min(pageHeight - metrics.buttonHintsHeight - 80, barY + 105);
    renderer.drawCenteredText(UI_10_FONT_ID, noteY, helpText.c_str());
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_DONE), "-", "+");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
