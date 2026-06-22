#include "ReaderFontSelectActivity.h"

#include <FontManager.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
const char* typeLabel(const FontFileType type) {
  switch (type) {
    case FontFileType::TrueType:
      return "TTF";
    case FontFileType::EpdFont:
      return "EPDF";
    case FontFileType::BitmapBin:
    default:
      return "BIN";
  }
}
}  // namespace

int ReaderFontSelectActivity::totalItems() const { return FontMgr.getFontCount() + 1; }

void ReaderFontSelectActivity::onEnter() {
  Activity::onEnter();
  FontMgr.scanFonts();
  selectedIndex = FontMgr.getSelectedIndex() + 1;
  if (selectedIndex < 0 || selectedIndex >= totalItems()) selectedIndex = 0;
  requestUpdate();
}

void ReaderFontSelectActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    applySelection();
    return;
  }

  buttonNavigator.onNextRelease([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, totalItems());
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, totalItems());
    requestUpdate();
  });
}

void ReaderFontSelectActivity::applySelection() {
  const int managerIndex = selectedIndex - 1;
  LOG_INF("FONT_UI", "Selecting reader font index %d", managerIndex);
  FontMgr.selectFont(managerIndex);
  finish();
}

std::string ReaderFontSelectActivity::itemTitle(const int index) const {
  if (index == 0) return tr(STR_BUILT_IN_FONT);
  const FontInfo* info = FontMgr.getFontInfo(index - 1);
  if (info == nullptr) return std::string();
  return info->name[0] != '\0' ? std::string(info->name) : std::string(info->filename);
}

std::string ReaderFontSelectActivity::itemSubtitle(const int index) const {
  if (index == 0) return std::string();
  const FontInfo* info = FontMgr.getFontInfo(index - 1);
  if (info == nullptr) return std::string();
  char text[96];
  if (info->type == FontFileType::TrueType) {
    std::snprintf(text, sizeof(text), "[%s %upx] %s", typeLabel(info->type), info->size, info->filename);
  } else {
    std::snprintf(text, sizeof(text), "[%s %ux%u] %s", typeLabel(info->type), info->width, info->height,
                  info->filename);
  }
  return text;
}

void ReaderFontSelectActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_READER_FONT_FILE));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int activeIndex = FontMgr.getSelectedIndex() + 1;

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, totalItems(), selectedIndex,
      [this](int index) { return itemTitle(index); }, [this](int index) { return itemSubtitle(index); }, nullptr,
      [activeIndex](int index) { return index == activeIndex ? tr(STR_SELECTED) : ""; }, true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
