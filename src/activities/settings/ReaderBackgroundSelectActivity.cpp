#include "ReaderBackgroundSelectActivity.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/util/DirectTouchSelection.h"
#include "components/UITheme.h"

namespace {
using BackgroundItem = ReaderBackgroundSelectActivity::BackgroundItem;

void collectPngFilesFromDirectory(const char* directory, std::vector<BackgroundItem>& items, bool includeOneNestedLevel) {
  auto dir = Storage.open(directory);
  if (!dir || !dir.isDirectory()) return;

  char name[500];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    file.getName(name, sizeof(name));
    std::string filename(name);
    if (filename.empty() || filename[0] == '.') continue;

    const std::string fullPath = std::string(directory) + "/" + filename;
    if (file.isDirectory()) {
      if (includeOneNestedLevel) collectPngFilesFromDirectory(fullPath.c_str(), items, false);
      continue;
    }
    if (!FsHelpers::hasPngExtension(filename)) continue;

    BackgroundItem item;
    item.path = fullPath;
    item.title = filename;
    item.subtitle = fullPath;
    items.push_back(std::move(item));
  }
  dir.close();
}
}  // namespace

int ReaderBackgroundSelectActivity::totalItems() const { return static_cast<int>(items.size()) + 1; }

void ReaderBackgroundSelectActivity::scanBackgroundFiles() {
  items.clear();
  collectPngFilesFromDirectory("/bg", items, true);
  std::sort(items.begin(), items.end(), [](const BackgroundItem& a, const BackgroundItem& b) { return a.path < b.path; });
}

void ReaderBackgroundSelectActivity::onEnter() {
  Activity::onEnter();
  scanBackgroundFiles();
  selectedIndex = 0;
  if (SETTINGS.readerBackgroundPngPath[0] != '\0') {
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
      if (items[i].path == SETTINGS.readerBackgroundPngPath) {
        selectedIndex = i + 1;
        break;
      }
    }
  }
  requestUpdate();
}

void ReaderBackgroundSelectActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    applySelection();
    return;
  }

#if CROSSPOINT_PAPERS3
  {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight = renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
    const int targetIndex = DirectTouchSelection::hitListRow(
        mappedInput, Rect{0, contentTop, renderer.getScreenWidth(), contentHeight}, totalItems(), selectedIndex,
        metrics.listWithSubtitleRowHeight);
    if (targetIndex >= 0) {
      if (targetIndex == selectedIndex) applySelection();
      else {
        selectedIndex = targetIndex;
        requestUpdate();
      }
      return;
    }
  }
#endif

  const int pageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, true);
  buttonNavigator.onNextRelease([this, pageItems] {
    selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, totalItems(), pageItems);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, pageItems] {
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, totalItems(), pageItems);
    requestUpdate();
  });
}

void ReaderBackgroundSelectActivity::applySelection() {
  if (selectedIndex == 0) {
    SETTINGS.readerBackgroundPng = CrossPointSettings::READER_BG_OFF;
    SETTINGS.readerBackgroundPngPath[0] = '\0';
    LOG_INF("ERS", "Reader background PNG cleared");
    finish();
    return;
  }
  const int itemIndex = selectedIndex - 1;
  if (itemIndex < 0 || itemIndex >= static_cast<int>(items.size())) return;
  SETTINGS.readerBackgroundPng = CrossPointSettings::READER_BG_SELECTED_FILE;
  std::strncpy(SETTINGS.readerBackgroundPngPath, items[itemIndex].path.c_str(), sizeof(SETTINGS.readerBackgroundPngPath) - 1);
  SETTINGS.readerBackgroundPngPath[sizeof(SETTINGS.readerBackgroundPngPath) - 1] = '\0';
  LOG_INF("ERS", "Reader background PNG selected: %s", SETTINGS.readerBackgroundPngPath);
  finish();
}

void ReaderBackgroundSelectActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_READER_BACKGROUND_PNG));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const std::string activePath = SETTINGS.readerBackgroundPngPath;

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, totalItems(), selectedIndex,
      [this](int index) { return index == 0 ? std::string(tr(STR_NONE_OPT)) : items[index - 1].title; },
      [this](int index) { return index == 0 ? std::string("/bg") : items[index - 1].subtitle; }, nullptr,
      [this, activePath](int index) {
        if (index == 0) return activePath.empty() ? std::string(tr(STR_SELECTED)) : std::string();
        return items[index - 1].path == activePath ? std::string(tr(STR_SELECTED)) : std::string();
      }, true);

  const int hintPageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, true);
  const char* prevPageLabel = ButtonNavigator::hasPreviousPage(selectedIndex, totalItems(), hintPageItems) ? tr(STR_DIR_UP) : "";
  const char* nextPageLabel = ButtonNavigator::hasNextPage(selectedIndex, totalItems(), hintPageItems) ? tr(STR_DIR_DOWN) : "";
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), prevPageLabel, nextPageLabel);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
