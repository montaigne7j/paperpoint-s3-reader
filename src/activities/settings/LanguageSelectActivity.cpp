#include "LanguageSelectActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <iterator>

#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "activities/util/DirectTouchSelection.h"
#include "components/UITheme.h"
#include "fontIds.h"

void LanguageSelectActivity::onEnter() {
  Activity::onEnter();

  // Set current selection based on current language
  const auto currentLang = static_cast<uint8_t>(I18N.getLanguage());
  const auto* begin = std::begin(SORTED_LANGUAGE_INDICES);
  const auto* end = std::end(SORTED_LANGUAGE_INDICES);
  const auto* it = std::find(begin, end, currentLang);
  selectedIndex = (it != end) ? std::distance(begin, it) : 0;

  requestUpdate();
}

void LanguageSelectActivity::onExit() { Activity::onExit(); }

void LanguageSelectActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onBack();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

#if CROSSPOINT_PAPERS3
  {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight = renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
    const int targetIndex = DirectTouchSelection::hitListRow(
        mappedInput, Rect{0, contentTop, renderer.getScreenWidth(), contentHeight}, totalItems, selectedIndex,
        metrics.listRowHeight);
    if (targetIndex >= 0) {
      if (targetIndex == selectedIndex) {
        handleSelection();
      } else {
        selectedIndex = targetIndex;
        requestUpdate();
      }
      return;
    }
  }
#endif

  // Footer Previous / Next page through the list. Row selection is by touch.
  const int pageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, false);
  buttonNavigator.onNextRelease([this, pageItems] {
    selectedIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectedIndex), totalItems, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, pageItems] {
    selectedIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectedIndex), totalItems, pageItems);
    requestUpdate();
  });
}

void LanguageSelectActivity::handleSelection() {
  {
    RenderLock lock(*this);
    I18N.setLanguage(static_cast<Language>(SORTED_LANGUAGE_INDICES[selectedIndex]));
  }

  // Return to previous page
  onBack();
}

void LanguageSelectActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  auto metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_LANGUAGE));

  // Current language marker
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const auto currentLang = static_cast<uint8_t>(I18N.getLanguage());
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, totalItems, selectedIndex,
      [this](int index) { return I18N.getLanguageName(static_cast<Language>(SORTED_LANGUAGE_INDICES[index])); },
      nullptr, nullptr,
      [this, currentLang](int index) { return SORTED_LANGUAGE_INDICES[index] == currentLang ? tr(STR_SELECTED) : ""; },
      true);

  // Button hints
  const int hintPageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, false);
  const char* prevPageLabel = ButtonNavigator::hasPreviousPage(static_cast<int>(selectedIndex), totalItems, hintPageItems)
                                  ? tr(STR_DIR_UP)
                                  : "";
  const char* nextPageLabel = ButtonNavigator::hasNextPage(static_cast<int>(selectedIndex), totalItems, hintPageItems)
                                  ? tr(STR_DIR_DOWN)
                                  : "";
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), prevPageLabel, nextPageLabel);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
