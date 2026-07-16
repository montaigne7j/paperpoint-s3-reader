#include "ComicReaderMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <utility>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/util/DirectTouchSelection.h"
#include "components/UITheme.h"
#include "fontIds.h"

ComicReaderMenuActivity::ComicReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                 std::string title, const int currentPage, const int totalPages)
    : Activity("ComicReaderMenu", renderer, mappedInput),
      title(std::move(title)),
      currentPage(currentPage),
      totalPages(totalPages) {}

namespace {
const char* comicFullRefreshLabel(const uint8_t value) {
  switch (value) {
    case 0:
      return "每頁";
    case 1:
      return "每2頁";
    case 2:
      return "每5頁";
    case 3:
      return "每10頁";
    case 4:
    default:
      return "不全刷";
  }
}
}  // namespace

void ComicReaderMenuActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

const char* ComicReaderMenuActivity::itemLabel(const int index) const {
  switch (index) {
    case ITEM_GO_HOME:
      return "返回首頁";
    case ITEM_GRAY_ENHANCE:
      return "灰階強化";
    case ITEM_FULL_REFRESH:
      return "全刷頻率";
    default:
      return "";
  }
}

std::string ComicReaderMenuActivity::itemValue(const int index) const {
  switch (index) {
    case ITEM_GRAY_ENHANCE: {
      const int value = static_cast<int>(SETTINGS.comicGrayEnhanceEncoded) - 50;
      char buf[16];
      std::snprintf(buf, sizeof(buf), "%+d%%", value);
      return buf;
    }
    case ITEM_FULL_REFRESH:
      return comicFullRefreshLabel(SETTINGS.comicFullRefreshFrequency);
    default:
      return "";
  }
}

void ComicReaderMenuActivity::finishWithAction(const Action action) {
  setResult(MenuResult{static_cast<int>(action), 0, 0});
  finish();
}

void ComicReaderMenuActivity::activateSelected() {
  switch (selectedIndex) {
    case ITEM_GO_HOME:
      finishWithAction(ACTION_GO_HOME);
      return;

    case ITEM_GRAY_ENHANCE: {
      int value = static_cast<int>(SETTINGS.comicGrayEnhanceEncoded) - 50;
      value += 10;
      if (value > 50) value = -50;
      SETTINGS.comicGrayEnhanceEncoded = static_cast<uint8_t>(value + 50);
      SETTINGS.saveToFile();
      settingsChanged = true;
      requestUpdate();
      return;
    }

    case ITEM_FULL_REFRESH:
      SETTINGS.comicFullRefreshFrequency = static_cast<uint8_t>((SETTINGS.comicFullRefreshFrequency + 1) % 5);
      SETTINGS.saveToFile();
      settingsChanged = true;
      requestUpdate();
      return;

    default:
      return;
  }
}

void ComicReaderMenuActivity::loop() {
#if CROSSPOINT_PAPERS3
  {
    const auto& metrics = UITheme::getInstance().getMetrics();
    // Must match render(): the page summary occupies 34 px below the header.
    // The old hit box started 34 px too high, so users had to tap well above
    // the visible row to select it.
    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 34;
    const int footerTop = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing;
    const int rowHeight = metrics.listRowHeight;
    const Rect listRect{0, contentTop, renderer.getScreenWidth(), std::max(1, footerTop - contentTop)};

    const int hitIndex = DirectTouchSelection::hitListRow(
        mappedInput, listRect, ITEM_COUNT, selectedIndex, rowHeight);
    if (hitIndex >= 0) {
      // Comic menu touch should behave like a large button list: one tap both
      // selects and activates.  The old select-then-second-tap behavior felt
      // like missed input on Paper S3.
      selectedIndex = hitIndex;
      activateSelected();
      return;
    }
  }
#endif

  buttonNavigator.onNextRelease([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, ITEM_COUNT);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, ITEM_COUNT);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelected();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (settingsChanged) {
      finishWithAction(ACTION_SETTINGS_CHANGED);
    } else {
      ActivityResult result;
      result.isCancelled = true;
      result.data = MenuResult{ACTION_NONE, 0, 0};
      setResult(std::move(result));
      finish();
    }
    return;
  }
}

void ComicReaderMenuActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "漫畫閱讀選單");

  char summary[48];
  std::snprintf(summary, sizeof(summary), "%d/%d", currentPage, totalPages);
  renderer.drawCenteredText(UI_10_FONT_ID, metrics.topPadding + metrics.headerHeight + 22, summary);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 34;
  const int footerTop = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const Rect listRect{0, contentTop, pageWidth, std::max(1, footerTop - contentTop)};

  GUI.drawList(
      renderer, listRect, ITEM_COUNT, selectedIndex,
      [this](int index) { return std::string(itemLabel(index)); },
      nullptr,
      nullptr,
      [this](int index) { return itemValue(index); },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
}
