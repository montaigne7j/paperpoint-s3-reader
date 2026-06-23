#include "LyraTheme.h"

#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/book.h"
#include "components/icons/book24.h"
#include "components/icons/cover.h"
#include "components/icons/file24.h"
#include "components/icons/folder.h"
#include "components/icons/folder24.h"
#include "components/icons/hotspot.h"
#include "components/icons/image24.h"
#include "components/icons/library.h"
#include "components/icons/recent.h"
#include "components/icons/settings2.h"
#include "components/icons/text24.h"
#include "components/icons/transfer.h"
#include "components/icons/wifi.h"
#include "fontIds.h"

// Internal constants
namespace {
constexpr int hPaddingInSelection = 8;
constexpr int cornerRadius = 6;
constexpr int topHintButtonY = 345;
constexpr int popupMarginX = 16;
constexpr int popupMarginY = 12;
constexpr int maxSubtitleWidth = 100;
constexpr int maxListValueWidth = 200;
constexpr int mainMenuIconSize = 32;
constexpr int listIconSize = 24;
// Lyra list subtitles can contain EPUB metadata such as Chinese author names.
// SMALL_FONT_ID only uses the built-in Latin-oriented font, while UI_10_FONT_ID
// can route CJK glyphs through the configured external UI font.
constexpr int listSubtitleFontId = UI_10_FONT_ID;
constexpr int mainMenuColumns = 2;
int coverWidth = 0;
void drawPopupProgressRing(const GfxRenderer& renderer, const int cx, const int cy, const int radius,
                           const int thickness, const int progress, const bool state) {
  const int clamped = std::max(0, std::min(100, progress));
  constexpr float pi = 3.14159265358979323846f;

  auto drawArcPixels = [&](const int startDeg, const int endDeg, const int strokeWidth, const bool pixelState) {
    for (int deg = startDeg; deg <= endDeg; ++deg) {
      const float rad = (static_cast<float>(deg) - 90.0f) * pi / 180.0f;
      const float cs = std::cos(rad);
      const float sn = std::sin(rad);
      for (int t = 0; t < strokeWidth; ++t) {
        const int r = radius - t;
        const int x = cx + static_cast<int>(std::lround(cs * static_cast<float>(r)));
        const int y = cy + static_cast<int>(std::lround(sn * static_cast<float>(r)));
        renderer.drawPixel(x, y, pixelState);
      }
    }
  };

  drawArcPixels(0, 359, 1, state);
  if (clamped <= 0) return;
  const int sweep = static_cast<int>(std::lround(clamped * 3.6f));
  drawArcPixels(0, std::min(359, sweep), thickness, state);
}

const uint8_t* iconForName(UIIcon icon, int size) {
  if (size == 24) {
    switch (icon) {
      case UIIcon::Folder:
        return Folder24Icon;
      case UIIcon::Text:
        return Text24Icon;
      case UIIcon::Image:
        return Image24Icon;
      case UIIcon::Book:
        return Book24Icon;
      case UIIcon::File:
        return File24Icon;
      default:
        return nullptr;
    }
  } else if (size == 32) {
    switch (icon) {
      case UIIcon::Folder:
        return FolderIcon;
      case UIIcon::Book:
        return BookIcon;
      case UIIcon::Recent:
        return RecentIcon;
      case UIIcon::Settings:
        return Settings2Icon;
      case UIIcon::Transfer:
        return TransferIcon;
      case UIIcon::Library:
        return LibraryIcon;
      case UIIcon::Wifi:
        return WifiIcon;
      case UIIcon::Hotspot:
        return HotspotIcon;
      default:
        return nullptr;
    }
  }
  return nullptr;
}

int lyraListRowHeight(const std::function<std::string(int index)>& rowSubtitle) {
  return rowSubtitle != nullptr ? LyraMetrics::values.listWithSubtitleRowHeight
                                : LyraMetrics::values.listRowHeight;
}

int lyraListContentWidth(const Rect rect, const int totalPages) {
  return rect.width -
         (totalPages > 1 ? (LyraMetrics::values.scrollBarWidth + LyraMetrics::values.scrollBarRightOffset) : 1);
}

void drawLyraListScrollBar(const GfxRenderer& renderer, const Rect rect, const int itemCount, const int selectedIndex,
                           const int pageItems) {
  if (pageItems <= 0) return;

  const int totalPages = (itemCount + pageItems - 1) / pageItems;
  if (totalPages <= 1) return;

  const int scrollAreaHeight = rect.height;
  const int scrollBarHeight = (scrollAreaHeight * pageItems) / itemCount;
  const int currentPage = selectedIndex / pageItems;
  const int scrollBarY = rect.y + ((scrollAreaHeight - scrollBarHeight) * currentPage) / (totalPages - 1);
  const int scrollBarX = rect.x + rect.width - LyraMetrics::values.scrollBarRightOffset;
  renderer.drawLine(scrollBarX, rect.y, scrollBarX, rect.y + scrollAreaHeight, true);
  renderer.fillRect(scrollBarX - LyraMetrics::values.scrollBarWidth, scrollBarY,
                    LyraMetrics::values.scrollBarWidth, scrollBarHeight, true);
}

void clearLyraListRow(const GfxRenderer& renderer, const Rect rect, const int rowHeight, const int pageItems,
                      const int totalPages, const int index) {
  if (pageItems <= 0 || index < 0) return;

  const int itemY = rect.y + (index % pageItems) * rowHeight;
  const int contentWidth = lyraListContentWidth(rect, totalPages);
  renderer.fillRect(rect.x, itemY, contentWidth, rowHeight, false);
}

void drawLyraListRow(const GfxRenderer& renderer, const Rect rect, const int itemCount, const int pageItems,
                     const int index, const int selectedIndex,
                     const std::function<std::string(int index)>& rowTitle,
                     const std::function<std::string(int index)>& rowSubtitle,
                     const std::function<UIIcon(int index)>& rowIcon,
                     const std::function<std::string(int index)>& rowValue, const bool highlightValue) {
  if (pageItems <= 0 || itemCount <= 0 || index < 0 || index >= itemCount) return;

  const int rowHeight = lyraListRowHeight(rowSubtitle);
  const int totalPages = (itemCount + pageItems - 1) / pageItems;
  const int contentWidth = lyraListContentWidth(rect, totalPages);
  const int itemY = rect.y + (index % pageItems) * rowHeight;

  if (index == selectedIndex) {
    renderer.fillRoundedRect(LyraMetrics::values.contentSidePadding, itemY,
                             contentWidth - LyraMetrics::values.contentSidePadding * 2, rowHeight, cornerRadius,
                             Color::LightGray);
  }

  int textX = rect.x + LyraMetrics::values.contentSidePadding + hPaddingInSelection;
  int textWidth = contentWidth - LyraMetrics::values.contentSidePadding * 2 - hPaddingInSelection * 2;
  int iconSize = 0;
  if (rowIcon != nullptr) {
    iconSize = rowSubtitle != nullptr ? mainMenuIconSize : listIconSize;
    textX += iconSize + hPaddingInSelection;
    textWidth -= iconSize + hPaddingInSelection;
  }

  const int titleLineH = renderer.getLineHeight(UI_10_FONT_ID);
  int textYOffset = 0;
  int subtitleYOffset = 0;
  if (rowSubtitle != nullptr) {
    const int subtitleLineH = renderer.getLineHeight(listSubtitleFontId);
    constexpr int subtitleGap = 4;
    const int totalTextH = titleLineH + subtitleGap + subtitleLineH;
    textYOffset = (rowHeight - totalTextH) / 2;
    subtitleYOffset = textYOffset + titleLineH + subtitleGap;
  } else {
    textYOffset = (rowHeight - titleLineH) / 2;
  }

  int rowTextWidth = textWidth;
  int valueWidth = 0;
  int valueTextWidth = 0;
  constexpr int valueGap = 12;
  constexpr int valueRightSafeInset = 8;
  std::string valueText;
  if (rowValue != nullptr) {
    const int valueColumnWidth = std::min(maxListValueWidth, std::max(70, (textWidth - valueGap) / 2));
    valueText = rowValue(index);
    valueText = renderer.truncatedText(UI_10_FONT_ID, valueText.c_str(), valueColumnWidth);
    valueTextWidth = renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str());
    valueWidth = valueTextWidth + hPaddingInSelection;
    rowTextWidth = std::max(60, rowTextWidth - valueWidth - valueGap);
  }

  auto itemName = rowTitle(index);
  auto item = renderer.truncatedText(UI_10_FONT_ID, itemName.c_str(), rowTextWidth);
  renderer.drawText(UI_10_FONT_ID, textX, itemY + textYOffset, item.c_str(), true);

  if (rowIcon != nullptr) {
    UIIcon icon = rowIcon(index);
    const uint8_t* iconBitmap = iconForName(icon, iconSize);
    if (iconBitmap != nullptr) {
      const int iconYOff = (rowHeight - iconSize) / 2;
      renderer.drawIcon(iconBitmap, rect.x + LyraMetrics::values.contentSidePadding + hPaddingInSelection,
                        itemY + iconYOff, iconSize, iconSize);
    }
  }

  if (rowSubtitle != nullptr) {
    std::string subtitleText = rowSubtitle(index);
    auto subtitle = renderer.truncatedText(listSubtitleFontId, subtitleText.c_str(), rowTextWidth);
    renderer.drawText(listSubtitleFontId, textX, itemY + subtitleYOffset, subtitle.c_str(), true);
  }

  if (!valueText.empty()) {
    const int valueRight =
        rect.x + contentWidth - LyraMetrics::values.contentSidePadding - hPaddingInSelection - valueRightSafeInset;
    const int valueX = valueRight - valueTextWidth;

    if (index == selectedIndex && highlightValue) {
      renderer.fillRoundedRect(valueX - hPaddingInSelection, itemY, valueWidth + hPaddingInSelection, rowHeight,
                               cornerRadius, Color::Black);
    }

    renderer.drawText(UI_10_FONT_ID, valueX, itemY + textYOffset, valueText.c_str(),
                      !(index == selectedIndex && highlightValue));
  }
}
}  // namespace

void drawLyraBatteryIcon(const GfxRenderer& renderer, int x, int y, int battWidth, int rectHeight,
                         uint16_t percentage) {
  BaseTheme::drawBatteryOutline(renderer, x, y, battWidth, rectHeight);

  const bool charging = static_cast<bool>(Serial);  // USB CDC connected

  if (charging) {
    // Draw solid fill when charging so lightning bolt is visible
    renderer.fillRect(x + 2, y + 2, battWidth - 5, rectHeight - 4);
    BaseTheme::drawBatteryLightningBolt(renderer, x + 4, y + 2);
  } else {
    // Draw bars when not charging
    if (percentage > 10) {
      renderer.fillRect(x + 2, y + 2, 3, rectHeight - 4);
    }
    if (percentage > 40) {
      renderer.fillRect(x + 6, y + 2, 3, rectHeight - 4);
    }
    if (percentage > 70) {
      renderer.fillRect(x + 10, y + 2, 3, rectHeight - 4);
    }
  }
}

void LyraTheme::drawBatteryLeft(const GfxRenderer& renderer, Rect rect, const bool showPercentage) const {
  // Left aligned: icon on left, percentage on right (reader mode)
  const uint16_t percentage = powerManager.getBatteryPercentage();
  const int y = rect.y + 6;
  const int battWidth = LyraMetrics::values.batteryWidth;

  if (showPercentage) {
    const auto percentageText = std::to_string(percentage) + "%";
    renderer.drawText(SMALL_FONT_ID, rect.x + BaseTheme::batteryPercentSpacing + battWidth, rect.y,
                      percentageText.c_str());
  }

  drawLyraBatteryIcon(renderer, rect.x, y, battWidth, rect.height, percentage);
}

void LyraTheme::drawBatteryRight(const GfxRenderer& renderer, Rect rect, const bool showPercentage) const {
  // Right aligned: percentage on left, icon on right (UI headers)
  const uint16_t percentage = powerManager.getBatteryPercentage();
  const int y = rect.y + 6;
  const int battWidth = LyraMetrics::values.batteryWidth;

  if (showPercentage) {
    const auto percentageText = std::to_string(percentage) + "%";
    const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, percentageText.c_str());
    // Clear the area where we're going to draw the text to prevent ghosting
    const auto textHeight = renderer.getTextHeight(SMALL_FONT_ID);
    renderer.fillRect(rect.x - textWidth - BaseTheme::batteryPercentSpacing, rect.y, textWidth, textHeight, false);
    // Draw text to the left of the icon
    renderer.drawText(SMALL_FONT_ID, rect.x - textWidth - BaseTheme::batteryPercentSpacing, rect.y,
                      percentageText.c_str());
  }

  // Draw icon at rect.x
  drawLyraBatteryIcon(renderer, rect.x, y, battWidth, rect.height, percentage);
}

void LyraTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle) const {
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
  drawPowerButton(renderer, rect);

  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  // Position icon at right edge, drawBatteryRight will place text to the left
  const int batteryX = rect.x + rect.width - 12 - LyraMetrics::values.batteryWidth;
  drawBatteryRight(renderer,
                   Rect{batteryX, rect.y + 5, LyraMetrics::values.batteryWidth, LyraMetrics::values.batteryHeight},
                   showBatteryPercentage);

  int maxTitleWidth =
      rect.width - LyraMetrics::values.contentSidePadding * 2 - (subtitle != nullptr ? maxSubtitleWidth : 0);

  if (title) {
    auto truncatedTitle = renderer.truncatedText(UI_12_FONT_ID, title, maxTitleWidth, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, rect.x + LyraMetrics::values.contentSidePadding,
                      rect.y + LyraMetrics::values.batteryBarHeight + 3, truncatedTitle.c_str(), true,
                      EpdFontFamily::BOLD);
    renderer.drawLine(rect.x, rect.y + rect.height - 3, rect.x + rect.width - 1, rect.y + rect.height - 3, 3, true);
  }

  if (subtitle) {
    auto truncatedSubtitle = renderer.truncatedText(SMALL_FONT_ID, subtitle, maxSubtitleWidth, EpdFontFamily::REGULAR);
    int truncatedSubtitleWidth = renderer.getTextWidth(SMALL_FONT_ID, truncatedSubtitle.c_str());
    renderer.drawText(SMALL_FONT_ID,
                      rect.x + rect.width - LyraMetrics::values.contentSidePadding - truncatedSubtitleWidth,
                      rect.y + 50, truncatedSubtitle.c_str(), true);
  }
}

void LyraTheme::drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label, const char* rightLabel) const {
  int currentX = rect.x + LyraMetrics::values.contentSidePadding;
  int rightSpace = LyraMetrics::values.contentSidePadding;
  if (rightLabel) {
    auto truncatedRightLabel =
        renderer.truncatedText(SMALL_FONT_ID, rightLabel, maxListValueWidth, EpdFontFamily::REGULAR);
    int rightLabelWidth = renderer.getTextWidth(SMALL_FONT_ID, truncatedRightLabel.c_str());
    renderer.drawText(SMALL_FONT_ID, rect.x + rect.width - LyraMetrics::values.contentSidePadding - rightLabelWidth,
                      rect.y + 7, truncatedRightLabel.c_str());
    rightSpace += rightLabelWidth + hPaddingInSelection;
  }

  auto truncatedLabel = renderer.truncatedText(
      UI_10_FONT_ID, label, rect.width - LyraMetrics::values.contentSidePadding - rightSpace, EpdFontFamily::REGULAR);
  renderer.drawText(UI_10_FONT_ID, currentX, rect.y + 6, truncatedLabel.c_str(), true, EpdFontFamily::REGULAR);

  renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width - 1, rect.y + rect.height - 1, true);
}

void LyraTheme::drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                           bool selected) const {
  if (tabs.empty()) return;

  if (selected) {
    renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
  }

  constexpr int cellPadding = 6;
  const int tabCount = static_cast<int>(tabs.size());
  const int textLineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int textY = rect.y + std::max(0, (rect.height - textLineH) / 2);

  for (int i = 0; i < tabCount; ++i) {
    const auto& tab = tabs[i];
    const int cellX = rect.x + (rect.width * i) / tabCount;
    const int nextCellX = rect.x + (rect.width * (i + 1)) / tabCount;
    const int cellW = nextCellX - cellX;

    auto label = renderer.truncatedText(UI_10_FONT_ID, tab.label, std::max(1, cellW - cellPadding * 2),
                                        EpdFontFamily::REGULAR);
    const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, label.c_str(), EpdFontFamily::REGULAR);
    const int textX = cellX + std::max(0, (cellW - textWidth) / 2);

    if (tab.selected) {
      if (selected) {
        renderer.fillRoundedRect(cellX + 2, rect.y + 1, std::max(1, cellW - 4), rect.height - 4, cornerRadius,
                                 Color::Black);
      } else {
        renderer.fillRectDither(cellX + 2, rect.y, std::max(1, cellW - 4), rect.height - 3, Color::LightGray);
        renderer.drawLine(cellX + cellPadding, rect.y + rect.height - 3, nextCellX - cellPadding,
                          rect.y + rect.height - 3, 2, true);
      }
    }

    renderer.drawText(UI_10_FONT_ID, textX, textY, label.c_str(), !(tab.selected && selected), EpdFontFamily::REGULAR);

    if (i + 1 < tabCount) {
      renderer.drawLine(nextCellX, rect.y + 4, nextCellX, rect.y + rect.height - 6);
    }
  }

  renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width - 1, rect.y + rect.height - 1, true);
}

void LyraTheme::drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                         const std::function<std::string(int index)>& rowTitle,
                         const std::function<std::string(int index)>& rowSubtitle,
                         const std::function<UIIcon(int index)>& rowIcon,
                         const std::function<std::string(int index)>& rowValue, bool highlightValue) const {
  const int rowHeight = lyraListRowHeight(rowSubtitle);
  const int pageItems = rowHeight > 0 ? rect.height / rowHeight : 0;
  if (pageItems <= 0) return;

  drawLyraListScrollBar(renderer, rect, itemCount, selectedIndex, pageItems);

  const int pageStartIndex = selectedIndex / pageItems * pageItems;
  for (int i = pageStartIndex; i < itemCount && i < pageStartIndex + pageItems; ++i) {
    drawLyraListRow(renderer, rect, itemCount, pageItems, i, selectedIndex, rowTitle, rowSubtitle, rowIcon, rowValue,
                    highlightValue);
  }
}

void LyraTheme::redrawListSelection(const GfxRenderer& renderer, Rect rect, int itemCount, int oldSelectedIndex,
                                    int newSelectedIndex,
                                    const std::function<std::string(int index)>& rowTitle,
                                    const std::function<std::string(int index)>& rowSubtitle,
                                    const std::function<UIIcon(int index)>& rowIcon,
                                    const std::function<std::string(int index)>& rowValue,
                                    bool highlightValue) const {
  const int rowHeight = lyraListRowHeight(rowSubtitle);
  const int pageItems = rowHeight > 0 ? rect.height / rowHeight : 0;
  if (pageItems <= 0 || itemCount <= 0 || oldSelectedIndex < 0 || newSelectedIndex < 0 ||
      oldSelectedIndex >= itemCount || newSelectedIndex >= itemCount) {
    return;
  }

  if (oldSelectedIndex / pageItems != newSelectedIndex / pageItems) return;

  const int totalPages = (itemCount + pageItems - 1) / pageItems;
  clearLyraListRow(renderer, rect, rowHeight, pageItems, totalPages, oldSelectedIndex);
  if (newSelectedIndex != oldSelectedIndex) {
    clearLyraListRow(renderer, rect, rowHeight, pageItems, totalPages, newSelectedIndex);
  }

  drawLyraListRow(renderer, rect, itemCount, pageItems, oldSelectedIndex, newSelectedIndex, rowTitle, rowSubtitle,
                  rowIcon, rowValue, highlightValue);
  if (newSelectedIndex != oldSelectedIndex) {
    drawLyraListRow(renderer, rect, itemCount, pageItems, newSelectedIndex, newSelectedIndex, rowTitle, rowSubtitle,
                    rowIcon, rowValue, highlightValue);
  }
}

void LyraTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                const char* btn4) const {
  const GfxRenderer::Orientation orig_orientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageHeight = renderer.getScreenHeight();
#if CROSSPOINT_PAPERS3
  // Paper S3: 4 tappable buttons across 540px, matching footer touch zones in HalGPIO
  constexpr int buttonWidth = 120;
  constexpr int buttonHeight = LyraMetrics::values.buttonHintsHeight;
  constexpr int buttonY = LyraMetrics::values.buttonHintsHeight;
  constexpr int buttonPositions[] = {12, 144, 276, 408};
  const char* labels[] = {btn1, btn2, btn3, btn4};

  for (int i = 0; i < 4; i++) {
    if (labels[i] != nullptr && labels[i][0] != '\0') {
      const int x = buttonPositions[i];
      renderer.fillRoundedRect(x, pageHeight - buttonY, buttonWidth, buttonHeight, cornerRadius, Color::White);
      renderer.drawRoundedRect(x, pageHeight - buttonY, buttonWidth, buttonHeight, 1, cornerRadius, true, true, false,
                               false, true);
      const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, labels[i]);
      const int textX = x + (buttonWidth - textWidth) / 2;
      const int textY = pageHeight - buttonY + (buttonHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
      renderer.drawText(UI_10_FONT_ID, textX, textY, labels[i]);
    }
  }
#else
  constexpr int buttonWidth = 80;
  constexpr int smallButtonHeight = 15;
  constexpr int buttonHeight = LyraMetrics::values.buttonHintsHeight;
  constexpr int buttonY = LyraMetrics::values.buttonHintsHeight;  // Distance from bottom
  constexpr int textYOffset = 7;                                  // Distance from top of button to text baseline
  constexpr int buttonPositions[] = {58, 146, 254, 342};
  const char* labels[] = {btn1, btn2, btn3, btn4};

  for (int i = 0; i < 4; i++) {
    const int x = buttonPositions[i];
    if (labels[i] != nullptr && labels[i][0] != '\0') {
      // Draw the filled background and border for a FULL-sized button
      renderer.fillRoundedRect(x, pageHeight - buttonY, buttonWidth, buttonHeight, cornerRadius, Color::White);
      renderer.drawRoundedRect(x, pageHeight - buttonY, buttonWidth, buttonHeight, 1, cornerRadius, true, true, false,
                               false, true);
      const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, labels[i]);
      const int textX = x + (buttonWidth - 1 - textWidth) / 2;
      renderer.drawText(SMALL_FONT_ID, textX, pageHeight - buttonY + textYOffset, labels[i]);
    } else {
      // Draw the filled background and border for a SMALL-sized button
      renderer.fillRoundedRect(x, pageHeight - smallButtonHeight, buttonWidth, smallButtonHeight, cornerRadius,
                               Color::White);
      renderer.drawRoundedRect(x, pageHeight - smallButtonHeight, buttonWidth, smallButtonHeight, 1, cornerRadius, true,
                               true, false, false, true);
    }
  }
#endif

  renderer.setOrientation(orig_orientation);
}

void LyraTheme::drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn) const {
#if CROSSPOINT_PAPERS3
  return;
#endif
  const int screenWidth = renderer.getScreenWidth();
  constexpr int buttonWidth = LyraMetrics::values.sideButtonHintsWidth;  // Width on screen (height when rotated)
  constexpr int buttonHeight = 78;                                       // Height on screen (width when rotated)
  // Position for the button group - buttons share a border so they're adjacent

  const char* labels[] = {topBtn, bottomBtn};

  // Draw the shared border for both buttons as one unit
  const int x = screenWidth - buttonWidth;

  // Draw top button outline
  if (topBtn != nullptr && topBtn[0] != '\0') {
    renderer.drawRoundedRect(x, topHintButtonY, buttonWidth, buttonHeight, 1, cornerRadius, true, false, true, false,
                             true);
  }

  // Draw bottom button outline
  if (bottomBtn != nullptr && bottomBtn[0] != '\0') {
    renderer.drawRoundedRect(x, topHintButtonY + buttonHeight + 5, buttonWidth, buttonHeight, 1, cornerRadius, true,
                             false, true, false, true);
  }

  // Draw text for each button
  for (int i = 0; i < 2; i++) {
    if (labels[i] != nullptr && labels[i][0] != '\0') {
      const int y = topHintButtonY + (i * buttonHeight + 5);

      // Draw rotated text centered in the button
      const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, labels[i]);

      renderer.drawTextRotated90CW(SMALL_FONT_ID, x, y + (buttonHeight + textWidth) / 2, labels[i]);
    }
  }
}

void LyraTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                    const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                    bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  const int tileWidth = rect.width - 2 * LyraMetrics::values.contentSidePadding;
  const int tileHeight = rect.height;
  const int tileY = rect.y;
  const bool hasContinueReading = !recentBooks.empty();
  if (coverWidth == 0) {
    coverWidth = LyraMetrics::values.homeCoverHeight * 0.6;
  }

  // Draw book card regardless, fill with message based on `hasContinueReading`
  // Draw cover image as background if available (inside the box)
  // Only load from SD on first render, then use stored buffer
  if (hasContinueReading) {
    RecentBook book = recentBooks[0];
    if (!coverRendered) {
      std::string coverPath = book.coverBmpPath;
      bool hasCover = true;
      int tileX = LyraMetrics::values.contentSidePadding;
      if (coverPath.empty()) {
        hasCover = false;
      } else {
        const std::string coverBmpPath = UITheme::getCoverThumbPath(coverPath, LyraMetrics::values.homeCoverHeight);

        // First time: load cover from SD and render
        FsFile file;
        if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
          Bitmap bitmap(file);
          if (bitmap.parseHeaders() == BmpReaderError::Ok) {
            coverWidth = bitmap.getWidth();
            // Cap cover width so title text still fits beside it
            const int maxCoverWidth = (tileWidth - 2 * hPaddingInSelection) * 3 / 5;
            if (coverWidth > maxCoverWidth) coverWidth = maxCoverWidth;
            renderer.setRenderMode(GfxRenderer::GRAYSCALE_DIRECT);
            renderer.drawBitmap(bitmap, tileX + hPaddingInSelection, tileY + hPaddingInSelection, coverWidth,
                                LyraMetrics::values.homeCoverHeight);
            renderer.setRenderMode(GfxRenderer::BW);
          } else {
            hasCover = false;
          }
          file.close();
        }
      }

      // Draw either way
      renderer.drawRect(tileX + hPaddingInSelection, tileY + hPaddingInSelection, coverWidth,
                        LyraMetrics::values.homeCoverHeight, true);

      if (!hasCover) {
        // Render empty cover
        renderer.fillRect(tileX + hPaddingInSelection,
                          tileY + hPaddingInSelection + (LyraMetrics::values.homeCoverHeight / 3), coverWidth,
                          2 * LyraMetrics::values.homeCoverHeight / 3, true);
        renderer.drawIcon(CoverIcon, tileX + hPaddingInSelection + 24, tileY + hPaddingInSelection + 24, 32, 32);
      }

      coverBufferStored = storeCoverBuffer();
      coverRendered = coverBufferStored;  // Only consider it rendered if we successfully stored the buffer
    }

    bool bookSelected = (selectorIndex == 0);

    int tileX = LyraMetrics::values.contentSidePadding;
    int textWidth = tileWidth - 2 * hPaddingInSelection - LyraMetrics::values.verticalSpacing - coverWidth;

    if (bookSelected) {
      // Draw selection box
      renderer.fillRoundedRect(tileX, tileY, tileWidth, hPaddingInSelection, cornerRadius, true, true, false, false,
                               Color::LightGray);
      renderer.fillRectDither(tileX, tileY + hPaddingInSelection, hPaddingInSelection,
                              LyraMetrics::values.homeCoverHeight, Color::LightGray);
      renderer.fillRectDither(tileX + hPaddingInSelection + coverWidth, tileY + hPaddingInSelection,
                              tileWidth - hPaddingInSelection - coverWidth, LyraMetrics::values.homeCoverHeight,
                              Color::LightGray);
      renderer.fillRoundedRect(tileX, tileY + LyraMetrics::values.homeCoverHeight + hPaddingInSelection, tileWidth,
                               hPaddingInSelection, cornerRadius, false, false, true, true, Color::LightGray);
    }

    auto titleLines = renderer.wrappedText(UI_12_FONT_ID, book.title.c_str(), textWidth, 3, EpdFontFamily::BOLD);

    auto author = renderer.truncatedText(UI_10_FONT_ID, book.author.c_str(), textWidth);
    const int titleLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
    const int titleBlockHeight = titleLineHeight * static_cast<int>(titleLines.size());
    const int authorHeight = book.author.empty() ? 0 : (renderer.getLineHeight(UI_10_FONT_ID) * 3 / 2);
    const int totalBlockHeight = titleBlockHeight + authorHeight;
    int titleY = tileY + tileHeight / 2 - totalBlockHeight / 2;
    const int textX = tileX + hPaddingInSelection + coverWidth + LyraMetrics::values.verticalSpacing;
    for (const auto& line : titleLines) {
      renderer.drawText(UI_12_FONT_ID, textX, titleY, line.c_str(), true, EpdFontFamily::BOLD);
      titleY += titleLineHeight;
    }
    if (!book.author.empty()) {
      titleY += renderer.getLineHeight(UI_10_FONT_ID) / 2;
      renderer.drawText(UI_10_FONT_ID, textX, titleY, author.c_str(), true);
    }
  } else {
    drawEmptyRecents(renderer, rect);
  }
}

void LyraTheme::drawEmptyRecents(const GfxRenderer& renderer, const Rect rect) const {
  constexpr int padding = 48;
  renderer.drawText(UI_12_FONT_ID, rect.x + padding,
                    rect.y + rect.height / 2 - renderer.getLineHeight(UI_12_FONT_ID) - 2, tr(STR_NO_OPEN_BOOK), true,
                    EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, rect.x + padding, rect.y + rect.height / 2 + 2, tr(STR_START_READING), true);
}

void LyraTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                               const std::function<std::string(int index)>& buttonLabel,
                               const std::function<UIIcon(int index)>& rowIcon) const {
  for (int i = 0; i < buttonCount; ++i) {
    int tileWidth = rect.width - LyraMetrics::values.contentSidePadding * 2;
    Rect tileRect = Rect{rect.x + LyraMetrics::values.contentSidePadding,
                         rect.y + i * (LyraMetrics::values.menuRowHeight + LyraMetrics::values.menuSpacing), tileWidth,
                         LyraMetrics::values.menuRowHeight};

    const bool selected = selectedIndex == i;

    if (selected) {
      renderer.fillRoundedRect(tileRect.x, tileRect.y, tileRect.width, tileRect.height, cornerRadius, Color::LightGray);
    }

    std::string labelStr = buttonLabel(i);
    const char* label = labelStr.c_str();
    int textX = tileRect.x + 16;
    const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
    const int textY = tileRect.y + (LyraMetrics::values.menuRowHeight - lineHeight) / 2;

    if (rowIcon != nullptr) {
      UIIcon icon = rowIcon(i);
      const uint8_t* iconBitmap = iconForName(icon, mainMenuIconSize);
      if (iconBitmap != nullptr) {
        renderer.drawIcon(iconBitmap, textX, textY + 3, mainMenuIconSize, mainMenuIconSize);
        textX += mainMenuIconSize + hPaddingInSelection + 2;
      }
    }

    renderer.drawText(UI_12_FONT_ID, textX, textY, label, true);
  }
}

Rect LyraTheme::drawPopup(const GfxRenderer& renderer, const char* message) const {
  // Scale y position proportionally to screen height (16.5% from top)
  const int y = static_cast<int>(renderer.getScreenHeight() * 0.165f);
  constexpr int outline = 2;
  constexpr int indicatorBlockHeight = 58;
  const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, message, EpdFontFamily::REGULAR);
  const int textHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int w = textWidth + popupMarginX * 2;
  const int h = textHeight + popupMarginY * 2 + indicatorBlockHeight;
  const int x = (renderer.getScreenWidth() - w) / 2;

  renderer.fillRoundedRect(x - outline, y - outline, w + outline * 2, h + outline * 2, cornerRadius + outline,
                           Color::White);
  renderer.fillRoundedRect(x, y, w, h, cornerRadius, Color::Black);

  const int textX = x + (w - textWidth) / 2;
  const int textY = y + popupMarginY - 2;
  renderer.drawText(UI_12_FONT_ID, textX, textY, message, false, EpdFontFamily::REGULAR);
  renderer.displayBuffer();

  return Rect{x, y, w, h};
}

void LyraTheme::fillPopupProgress(const GfxRenderer& renderer, const Rect& layout, const int progress) const {
  constexpr int progressAreaHeight = 52;
  constexpr int ringRadius = 18;
  constexpr int ringThickness = 3;
  const int clamped = std::max(0, std::min(100, progress));
  const int progressAreaY = layout.y + layout.height - progressAreaHeight - popupMarginY / 2;
  const int centerX = layout.x + layout.width / 2;
  const int centerY = progressAreaY + progressAreaHeight / 2;

  renderer.fillRect(layout.x + popupMarginX, progressAreaY, layout.width - popupMarginX * 2, progressAreaHeight, true);
  drawPopupProgressRing(renderer, centerX, centerY, ringRadius, ringThickness, clamped, false);

  const std::string percentText = std::to_string(clamped) + "%";
  const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, percentText.c_str());
  const int textX = centerX - textWidth / 2;
  const int textY = centerY - std::max(renderer.getLineHeight(SMALL_FONT_ID), 16) / 2 + 1;
  renderer.drawText(SMALL_FONT_ID, textX, textY, percentText.c_str(), false);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
void LyraTheme::drawTextField(const GfxRenderer& renderer, Rect rect, const int textWidth) const {
  int lineY = rect.y + rect.height + renderer.getLineHeight(UI_12_FONT_ID) + LyraMetrics::values.verticalSpacing;
  int lineW = textWidth + hPaddingInSelection * 2;
  renderer.drawLine(rect.x + (rect.width - lineW) / 2, lineY, rect.x + (rect.width + lineW) / 2, lineY, 3);
}

void LyraTheme::drawKeyboardKey(const GfxRenderer& renderer, Rect rect, const char* label,
                                const bool isSelected) const {
  if (isSelected) {
    renderer.fillRoundedRect(rect.x, rect.y, rect.width, rect.height, cornerRadius, Color::Black);
  }

  const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, label);
  const int textX = rect.x + (rect.width - textWidth) / 2;
  const int textY = rect.y + (rect.height - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
  renderer.drawText(UI_12_FONT_ID, textX, textY, label, !isSelected);
}
