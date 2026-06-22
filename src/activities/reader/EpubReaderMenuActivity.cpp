#include "EpubReaderMenuActivity.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <I18n.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
#if CROSSPOINT_PAPERS3
uint8_t orientationLabelIndex(uint8_t orientation) { return orientation == CrossPointSettings::LANDSCAPE_CCW ? 1 : 0; }
#endif
}  // namespace

EpubReaderMenuActivity::EpubReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               const std::string& title, const int currentPage, const int totalPages,
                                               const int bookProgressPercent, const uint8_t currentOrientation,
                                               const bool hasFootnotes)
    : Activity("EpubReaderMenu", renderer, mappedInput),
      menuItems(buildMenuItems(hasFootnotes)),
      title(title),
      pendingOrientation(
#if CROSSPOINT_PAPERS3
          CrossPointSettings::normalizePaperS3Orientation(currentOrientation)
#else
          currentOrientation
#endif
              ),
      currentPage(currentPage),
      totalPages(totalPages),
      bookProgressPercent(bookProgressPercent) {
}

std::vector<EpubReaderMenuActivity::MenuItem> EpubReaderMenuActivity::buildMenuItems(bool hasFootnotes) {
  std::vector<MenuItem> items;
  items.reserve(11);
  items.push_back({MenuAction::SELECT_CHAPTER, StrId::STR_SELECT_CHAPTER});
  items.push_back({MenuAction::GO_HOME, StrId::STR_GO_HOME_BUTTON});
  items.push_back({MenuAction::READER_SETTINGS, StrId::STR_CAT_READER});
  if (hasFootnotes) {
    items.push_back({MenuAction::FOOTNOTES, StrId::STR_FOOTNOTES});
  }
  items.push_back({MenuAction::ROTATE_SCREEN, StrId::STR_ORIENTATION});
  items.push_back({MenuAction::AUTO_PAGE_TURN, StrId::STR_AUTO_TURN_PAGES_PER_MIN});
  items.push_back({MenuAction::GO_TO_PERCENT, StrId::STR_GO_TO_PERCENT});
  items.push_back({MenuAction::SCREENSHOT, StrId::STR_SCREENSHOT_BUTTON});
  items.push_back({MenuAction::DISPLAY_QR, StrId::STR_DISPLAY_QR});
  items.push_back({MenuAction::SYNC, StrId::STR_SYNC_PROGRESS});
  items.push_back({MenuAction::DELETE_CACHE, StrId::STR_DELETE_CACHE});
  return items;
}

void EpubReaderMenuActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void EpubReaderMenuActivity::onExit() { Activity::onExit(); }

void EpubReaderMenuActivity::loop() {
  // Handle navigation
  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(menuItems.size()));
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(menuItems.size()));
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const auto selectedAction = menuItems[selectedIndex].action;
    if (selectedAction == MenuAction::ROTATE_SCREEN) {
      // Cycle orientation preview locally; actual rotation happens on menu exit.
#if CROSSPOINT_PAPERS3
      pendingOrientation = CrossPointSettings::nextPaperS3Orientation(pendingOrientation);
#else
      pendingOrientation = (pendingOrientation + 1) % orientationLabels.size();
#endif
      requestUpdate();
      return;
    }

    if (selectedAction == MenuAction::AUTO_PAGE_TURN) {
      selectedPageTurnOption = (selectedPageTurnOption + 1) % pageTurnLabels.size();
      requestUpdate();
      return;
    }

    setResult(MenuResult{static_cast<int>(selectedAction), pendingOrientation, selectedPageTurnOption});
    finish();
    return;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    result.data = MenuResult{-1, pendingOrientation, selectedPageTurnOption};
    setResult(std::move(result));
    finish();
    return;
  }
}

void EpubReaderMenuActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto orientation = renderer.getOrientation();
  // Landscape orientation: button hints are drawn along a vertical edge, so we
  // reserve a horizontal gutter to prevent overlap with menu content.
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  // Inverted portrait: button hints appear near the logical top, so we reserve
  // vertical space to keep the header and list clear.
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? 30 : 0;
  // Landscape CW places hints on the left edge; CCW keeps them on the right.
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = pageWidth - hintGutterWidth;
  const int hintGutterHeight = isPortraitInverted ? 50 : 0;
  const int contentY = hintGutterHeight;

  if (SETTINGS.uiTheme == CrossPointSettings::UI_THEME::LARGE_TEXT) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int headerY = contentY + metrics.topPadding;
    GUI.drawHeader(renderer, Rect{contentX, headerY, contentWidth, metrics.headerHeight}, title.c_str());

    std::string chapterLine = std::string(tr(STR_CHAPTER_PREFIX));
    if (totalPages > 0) {
      chapterLine += std::to_string(currentPage) + "/" + std::to_string(totalPages) +
                     std::string(tr(STR_PAGES_SEPARATOR));
    } else {
      chapterLine += "-";
    }
    const std::string bookLine = std::string(tr(STR_BOOK_PREFIX)) + std::to_string(bookProgressPercent) + "%";

    const int summaryRows = 2;
    const int listY = headerY + metrics.headerHeight + metrics.verticalSpacing;
    const int footerTop = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
    const int listHeight = std::max(1, footerTop - listY);

    GUI.drawList(
        renderer, Rect{contentX, listY, contentWidth, listHeight},
        static_cast<int>(menuItems.size()) + summaryRows, selectedIndex + summaryRows,
        [this, chapterLine, bookLine, summaryRows](int index) {
          if (index == 0) return chapterLine;
          if (index == 1) return bookLine;

          const int itemIndex = index - summaryRows;
          std::string itemLabel = I18N.get(menuItems[itemIndex].labelId);
          if (menuItems[itemIndex].action == MenuAction::READER_SETTINGS) {
            itemLabel += " ";
            itemLabel += tr(STR_SETTINGS_TITLE);
          }
          return itemLabel;
        },
        nullptr, nullptr,
        [this, summaryRows](int index) -> std::string {
          if (index < summaryRows) return "";

          const int itemIndex = index - summaryRows;
          if (menuItems[itemIndex].action == MenuAction::ROTATE_SCREEN) {
#if CROSSPOINT_PAPERS3
            return I18N.get(orientationLabels[orientationLabelIndex(pendingOrientation)]);
#else
            return I18N.get(orientationLabels[pendingOrientation]);
#endif
          }
          if (menuItems[itemIndex].action == MenuAction::AUTO_PAGE_TURN) {
            return pageTurnLabels[selectedPageTurnOption];
          }
          return "";
        },
        true);

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

#if CROSSPOINT_PAPERS3
  // EpubReaderMenu is a non-reader activity, so Activity::onEnter() enables
  // footer mode and the existing top-left 64x64 power hotspot. Draw the same
  // visible button used by Home/Settings so the shutdown target is discoverable.
  GUI.drawPowerButton(renderer, Rect{contentX, contentY, HalGPIO::POWER_HOTSPOT_SIZE,
                                     HalGPIO::POWER_HOTSPOT_SIZE});

  // Keep the centered book title clear of the power button. Reserve the same
  // amount on the right so the title remains centered on the physical screen.
  const int titleSideReserve = HalGPIO::POWER_HOTSPOT_SIZE;
#else
  const int titleSideReserve = 20;
#endif

  // Title
  const int titleMaxWidth =
      (contentWidth > titleSideReserve * 2) ? contentWidth - titleSideReserve * 2 : 0;
  const std::string truncTitle =
      renderer.truncatedText(UI_12_FONT_ID, title.c_str(), titleMaxWidth, EpdFontFamily::BOLD);
  // Manual centering inside the safe title area.
  const int titleWidth = renderer.getTextWidth(UI_12_FONT_ID, truncTitle.c_str(), EpdFontFamily::BOLD);
  const int titleX = contentX + titleSideReserve + (titleMaxWidth - titleWidth) / 2;
  renderer.drawText(UI_12_FONT_ID, titleX, 15 + contentY, truncTitle.c_str(), true, EpdFontFamily::BOLD);

  // Progress summary
  std::string progressLine;
  if (totalPages > 0) {
    progressLine = std::string(tr(STR_CHAPTER_PREFIX)) + std::to_string(currentPage) + "/" +
                   std::to_string(totalPages) + std::string(tr(STR_PAGES_SEPARATOR));
  }
  progressLine += std::string(tr(STR_BOOK_PREFIX)) + std::to_string(bookProgressPercent) + "%";
  renderer.drawCenteredText(UI_10_FONT_ID, 45, progressLine.c_str());

  // Menu Items. Reader Settings adds one more row, so derive the row height
  // from the space above the footer instead of assuming ten fixed 75px rows.
  const int startY = 85 + contentY;
  const int textLineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int footerTop = pageHeight - UITheme::getInstance().getMetrics().buttonHintsHeight;
  const int availableHeight = std::max(1, footerTop - startY);
  const int idealLineHeight = availableHeight / std::max(1, static_cast<int>(menuItems.size()));
#if CROSSPOINT_PAPERS3
  const int lineHeight = std::max(textLineH + 4, std::min(75, idealLineHeight));
#else
  const int lineHeight = std::max(textLineH + 2, std::min(30, idealLineHeight));
#endif
  const int textYOff = std::max(0, (lineHeight - textLineH) / 2);

  for (size_t i = 0; i < menuItems.size(); ++i) {
    const int displayY = startY + (i * lineHeight);
    const bool isSelected = (static_cast<int>(i) == selectedIndex);

    if (isSelected) {
      // Highlight only the content area so we don't paint over hint gutters.
      renderer.fillRect(contentX, displayY, contentWidth - 1, lineHeight, true);
    }

    std::string itemLabel = I18N.get(menuItems[i].labelId);
    if (menuItems[i].action == MenuAction::READER_SETTINGS) {
      itemLabel += " ";
      itemLabel += tr(STR_SETTINGS_TITLE);
    }
    renderer.drawText(UI_10_FONT_ID, contentX + 20, displayY + textYOff,
                      itemLabel.c_str(), !isSelected);

    if (menuItems[i].action == MenuAction::ROTATE_SCREEN) {
      // Render current orientation value on the right edge of the content area.
      const char* value =
#if CROSSPOINT_PAPERS3
          I18N.get(orientationLabels[orientationLabelIndex(pendingOrientation)]);
#else
          I18N.get(orientationLabels[pendingOrientation]);
#endif
      const auto width = renderer.getTextWidth(UI_10_FONT_ID, value);
      renderer.drawText(UI_10_FONT_ID, contentX + contentWidth - 20 - width, displayY + textYOff, value, !isSelected);
    }

    if (menuItems[i].action == MenuAction::AUTO_PAGE_TURN) {
      // Render current page turn value on the right edge of the content area.
      const auto value = pageTurnLabels[selectedPageTurnOption];
      const auto width = renderer.getTextWidth(UI_10_FONT_ID, value);
      renderer.drawText(UI_10_FONT_ID, contentX + contentWidth - 20 - width, displayY + textYOff, value, !isSelected);
    }
  }

  // Footer / Hints
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
