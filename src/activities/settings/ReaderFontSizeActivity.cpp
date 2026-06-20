#include "ReaderFontSizeActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <string>
#include <utility>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void ReaderFontSizeActivity::onEnter() {
  Activity::onEnter();
  value = std::min<uint8_t>(CrossPointSettings::READER_FONT_SIZE_MAX,
                            std::max<uint8_t>(CrossPointSettings::READER_FONT_SIZE_MIN, value));
  requestUpdate();
}

void ReaderFontSizeActivity::onExit() { Activity::onExit(); }

void ReaderFontSizeActivity::adjust(const int delta) {
  const int next = std::clamp<int>(static_cast<int>(value) + delta,
                                   CrossPointSettings::READER_FONT_SIZE_MIN,
                                   CrossPointSettings::READER_FONT_SIZE_MAX);
  if (next == value) return;
  value = static_cast<uint8_t>(next);
  requestUpdate();
}

void ReaderFontSizeActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    setResult(FontSizeResult{value});
    finish();
    return;
  }

  // On Paper S3 the two right-hand footer zones map to Up / Down. Physical
  // front Left / Right buttons are accepted too, so either control style can
  // decrease or increase the value. Holding a key repeats the adjustment.
  buttonNavigator.onPressAndContinuous(
      {MappedInputManager::Button::Up, MappedInputManager::Button::Left},
      [this] { adjust(-1); });
  buttonNavigator.onPressAndContinuous(
      {MappedInputManager::Button::Down, MappedInputManager::Button::Right},
      [this] { adjust(+1); });
}

void ReaderFontSizeActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 tr(STR_FONT_SIZE));

  const std::string valueText = std::to_string(value) + " px";
  const int valueY = metrics.topPadding + metrics.headerHeight + 105;
  renderer.drawCenteredText(UI_12_FONT_ID, valueY, valueText.c_str(), true,
                            EpdFontFamily::BOLD);

  constexpr int barSidePadding = 60;
  constexpr int barHeight = 16;
  const int barX = barSidePadding;
  const int barWidth = pageWidth - barSidePadding * 2;
  const int barY = valueY + 75;
  renderer.drawRect(barX, barY, barWidth, barHeight);

  const int range = CrossPointSettings::READER_FONT_SIZE_MAX -
                    CrossPointSettings::READER_FONT_SIZE_MIN;
  const int progress = static_cast<int>(value) -
                       CrossPointSettings::READER_FONT_SIZE_MIN;
  const int fillWidth = range > 0 ? (barWidth - 4) * progress / range : 0;
  if (fillWidth > 0) {
    renderer.fillRect(barX + 2, barY + 2, fillWidth, barHeight - 4, true);
  }

  const std::string rangeText =
      std::to_string(CrossPointSettings::READER_FONT_SIZE_MIN) +
      " - " + std::to_string(CrossPointSettings::READER_FONT_SIZE_MAX) + " px";
  renderer.drawCenteredText(UI_10_FONT_ID, barY + 38, rangeText.c_str());

  // Keep the explanatory text clear of the footer on both portrait variants.
  const int noteY = std::min(pageHeight - metrics.buttonHintsHeight - 80, barY + 105);
  renderer.drawCenteredText(UI_10_FONT_ID, noteY, "TTF uses the exact pixel size");

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "-", "+");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
