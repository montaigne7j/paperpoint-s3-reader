#include "LargeTextTheme.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalPowerManager.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int textScale = 2;
constexpr int hPadding = 8;
constexpr int cornerRadius = 6;

int lineHeight(const GfxRenderer& renderer, const int fontId = UI_10_FONT_ID) {
  return renderer.getLineHeightScaled(fontId, textScale);
}

int textWidth(const GfxRenderer& renderer, const int fontId, const std::string& text,
              const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  return renderer.getTextWidthScaled(fontId, text.c_str(), textScale, style);
}

std::string fitText(const GfxRenderer& renderer, const int fontId, const std::string& text, const int maxWidth,
                    const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  return renderer.truncatedTextScaled(fontId, text.c_str(), maxWidth, textScale, style);
}

int largeListRowHeight(const std::function<std::string(int index)>& rowSubtitle) {
  return rowSubtitle != nullptr ? LargeTextMetrics::values.listWithSubtitleRowHeight
                                : LargeTextMetrics::values.listRowHeight;
}

int largeListContentWidth(const Rect rect, const int totalPages) {
  return rect.width -
         (totalPages > 1 ? (LargeTextMetrics::values.scrollBarWidth + LargeTextMetrics::values.scrollBarRightOffset)
                         : 1);
}

void drawLargeListScrollBar(const GfxRenderer& renderer, const Rect rect, const int itemCount,
                            const int selectedIndex, const int pageItems) {
  if (pageItems <= 0) return;

  const int totalPages = (itemCount + pageItems - 1) / pageItems;
  if (totalPages <= 1) return;

  const int scrollAreaHeight = rect.height;
  const int scrollBarHeight = std::max(8, (scrollAreaHeight * pageItems) / std::max(1, itemCount));
  const int currentPage = std::max(0, selectedIndex) / pageItems;
  const int scrollBarY = rect.y + ((scrollAreaHeight - scrollBarHeight) * currentPage) / (totalPages - 1);
  const int scrollBarX = rect.x + rect.width - LargeTextMetrics::values.scrollBarRightOffset;
  renderer.drawLine(scrollBarX, rect.y, scrollBarX, rect.y + scrollAreaHeight, true);
  renderer.fillRect(scrollBarX - LargeTextMetrics::values.scrollBarWidth, scrollBarY,
                    LargeTextMetrics::values.scrollBarWidth, scrollBarHeight, true);
}

void drawLargeListRow(const GfxRenderer& renderer, const Rect rect, const int itemCount, const int pageItems,
                      const int index, const int selectedIndex,
                      const std::function<std::string(int index)>& rowTitle,
                      const std::function<std::string(int index)>& rowSubtitle,
                      const std::function<std::string(int index)>& rowValue, const bool highlightValue) {
  if (pageItems <= 0 || itemCount <= 0 || index < 0 || index >= itemCount) return;

  const int rowHeight = largeListRowHeight(rowSubtitle);
  const int totalPages = (itemCount + pageItems - 1) / pageItems;
  const int contentWidth = largeListContentWidth(rect, totalPages);
  const int itemY = rect.y + (index % pageItems) * rowHeight;
  const bool selected = index == selectedIndex;

  const int rowX = rect.x + LargeTextMetrics::values.contentSidePadding;
  const int rowW = contentWidth - LargeTextMetrics::values.contentSidePadding * 2;
  if (selected) {
    renderer.fillRoundedRect(rowX, itemY + 2, rowW, rowHeight - 4, cornerRadius, Color::LightGray);
  }

  const int textX = rowX + hPadding;
  const int textW = std::max(1, rowW - hPadding * 2);
  const int titleLineH = lineHeight(renderer);
  constexpr int lineGap = 4;

  std::string valueText;
  if (rowValue != nullptr) {
    valueText = rowValue(index);
  }
  const bool hasValue = !valueText.empty();
  const bool hasSubtitle = rowSubtitle != nullptr;
  const bool twoLines = hasValue || hasSubtitle;
  const int totalTextH = twoLines ? titleLineH * 2 + lineGap : titleLineH;
  const int titleY = itemY + std::max(0, (rowHeight - totalTextH) / 2);

  const auto title = fitText(renderer, UI_10_FONT_ID, rowTitle(index), textW);
  renderer.drawTextScaled(UI_10_FONT_ID, textX, titleY, title.c_str(), textScale, true);

  if (hasSubtitle) {
    const auto subtitle = fitText(renderer, UI_10_FONT_ID, rowSubtitle(index), textW);
    renderer.drawTextScaled(UI_10_FONT_ID, textX, titleY + titleLineH + lineGap, subtitle.c_str(), textScale, true);
  }

  if (hasValue) {
    const int valueY = titleY + titleLineH + lineGap;
    const auto value = fitText(renderer, UI_10_FONT_ID, valueText, textW);
    const int valueW = textWidth(renderer, UI_10_FONT_ID, value);
    const int valueX = rowX + rowW - hPadding - valueW;

    if (selected && highlightValue) {
      renderer.fillRoundedRect(valueX - hPadding, valueY - 2, valueW + hPadding * 2, titleLineH + 4, cornerRadius,
                               Color::Black);
    }
    renderer.drawTextScaled(UI_10_FONT_ID, valueX, valueY, value.c_str(), textScale,
                            !(selected && highlightValue));
  }
}
}  // namespace

void LargeTextTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title,
                                const char* subtitle) const {
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
  drawPowerButton(renderer, rect);

  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  const int batteryX = rect.x + rect.width - 12 - LargeTextMetrics::values.batteryWidth;
  drawBatteryRight(renderer,
                   Rect{batteryX, rect.y + 5, LargeTextMetrics::values.batteryWidth,
                        LargeTextMetrics::values.batteryHeight},
                   showBatteryPercentage);

  if (subtitle != nullptr) {
    const int maxSubtitleWidth = 130;
    auto truncatedSubtitle = renderer.truncatedText(SMALL_FONT_ID, subtitle, maxSubtitleWidth);
    const int subtitleWidth = renderer.getTextWidth(SMALL_FONT_ID, truncatedSubtitle.c_str());
    renderer.drawText(SMALL_FONT_ID, rect.x + rect.width - LargeTextMetrics::values.contentSidePadding - subtitleWidth,
                      rect.y + 42, truncatedSubtitle.c_str());
  }

  if (title != nullptr) {
    const int safeSide = 96;
    const int maxTitleWidth = std::max(1, rect.width - safeSide * 2);
    auto truncatedTitle = renderer.truncatedTextScaled(UI_12_FONT_ID, title, maxTitleWidth, textScale,
                                                       EpdFontFamily::BOLD);
    const int titleWidth = renderer.getTextWidthScaled(UI_12_FONT_ID, truncatedTitle.c_str(), textScale,
                                                       EpdFontFamily::BOLD);
    const int titleX = rect.x + (rect.width - titleWidth) / 2;
    const int titleY = rect.y + rect.height - lineHeight(renderer, UI_12_FONT_ID) - 8;
    renderer.drawTextScaled(UI_12_FONT_ID, titleX, titleY, truncatedTitle.c_str(), textScale, true,
                            EpdFontFamily::BOLD);
  }

  renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width - 1, rect.y + rect.height - 1, true);
}

void LargeTextTheme::drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label,
                                   const char* rightLabel) const {
  const int textX = rect.x + LargeTextMetrics::values.contentSidePadding;
  const int textW = rect.width - LargeTextMetrics::values.contentSidePadding * 2;
  const int mainLineH = lineHeight(renderer);
  const bool hasRight = rightLabel != nullptr && rightLabel[0] != '\0';
  const int labelY = rect.y + (hasRight ? 4 : std::max(0, (rect.height - mainLineH) / 2));

  auto truncatedLabel = fitText(renderer, UI_10_FONT_ID, label, textW);
  renderer.drawTextScaled(UI_10_FONT_ID, textX, labelY, truncatedLabel.c_str(), textScale);

  if (hasRight) {
    auto truncatedRight = fitText(renderer, UI_10_FONT_ID, rightLabel, textW);
    const int rightWidth = textWidth(renderer, UI_10_FONT_ID, truncatedRight);
    renderer.drawTextScaled(UI_10_FONT_ID, rect.x + rect.width - LargeTextMetrics::values.contentSidePadding - rightWidth,
                            labelY + mainLineH + 4, truncatedRight.c_str(), textScale);
  }

  renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width - 1, rect.y + rect.height - 1, true);
}

void LargeTextTheme::drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                                bool selected) const {
  if (selected) {
    renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
  }

  const int tabCount = static_cast<int>(tabs.size());
  const int tabLineH = lineHeight(renderer);
  const int textY = rect.y + std::max(0, (rect.height - tabLineH) / 2);
  const int leftPadding = LargeTextMetrics::values.contentSidePadding;
  const int usableWidth = rect.width - leftPadding * 2;

  for (int i = 0; i < tabCount; ++i) {
    const auto& tab = tabs[i];
    const auto style = tab.selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const int tabW = renderer.getTextWidthScaled(UI_10_FONT_ID, tab.label, textScale, style);

    int tabX = rect.x + leftPadding;
    if (tabCount == 1) {
      tabX = rect.x + (rect.width - tabW) / 2;
    } else if (i == tabCount - 1) {
      tabX = rect.x + rect.width - leftPadding - tabW;
    } else if (i > 0) {
      const int slotX = rect.x + leftPadding + (usableWidth * i) / (tabCount - 1);
      tabX = slotX - tabW / 2;
    }

    if (tab.selected) {
      if (selected) {
        renderer.fillRoundedRect(tabX - hPadding, textY - 4, tabW + hPadding * 2, tabLineH + 8, cornerRadius,
                                 Color::Black);
      } else {
        renderer.fillRectDither(tabX - hPadding, rect.y, tabW + hPadding * 2, rect.height - 3, Color::LightGray);
        renderer.drawLine(tabX - hPadding, rect.y + rect.height - 3, tabX + tabW + hPadding,
                          rect.y + rect.height - 3, 2, true);
      }
    }

    renderer.drawTextScaled(UI_10_FONT_ID, tabX, textY, tab.label, textScale, !(tab.selected && selected), style);
  }

  renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width - 1, rect.y + rect.height - 1, true);
}

void LargeTextTheme::drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                              const std::function<std::string(int index)>& rowTitle,
                              const std::function<std::string(int index)>& rowSubtitle,
                              const std::function<UIIcon(int index)>& rowIcon,
                              const std::function<std::string(int index)>& rowValue, bool highlightValue) const {
  (void)rowIcon;

  const int rowHeight = largeListRowHeight(rowSubtitle);
  const int pageItems = rowHeight > 0 ? rect.height / rowHeight : 0;
  if (pageItems <= 0) return;

  drawLargeListScrollBar(renderer, rect, itemCount, selectedIndex, pageItems);

  const int pageStartIndex = std::max(0, selectedIndex) / pageItems * pageItems;
  for (int i = pageStartIndex; i < itemCount && i < pageStartIndex + pageItems; ++i) {
    drawLargeListRow(renderer, rect, itemCount, pageItems, i, selectedIndex, rowTitle, rowSubtitle, rowValue,
                     highlightValue);
  }
}

void LargeTextTheme::redrawListSelection(const GfxRenderer& renderer, Rect rect, int itemCount, int oldSelectedIndex,
                                         int newSelectedIndex,
                                         const std::function<std::string(int index)>& rowTitle,
                                         const std::function<std::string(int index)>& rowSubtitle,
                                         const std::function<UIIcon(int index)>& rowIcon,
                                         const std::function<std::string(int index)>& rowValue,
                                         bool highlightValue) const {
  (void)oldSelectedIndex;
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
  drawList(renderer, rect, itemCount, newSelectedIndex, rowTitle, rowSubtitle, rowIcon, rowValue, highlightValue);
}

void LargeTextTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                     const char* btn4) const {
  const GfxRenderer::Orientation origOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageHeight = renderer.getScreenHeight();
  constexpr int buttonWidth = 120;
  constexpr int buttonHeight = LargeTextMetrics::values.buttonHintsHeight;
  constexpr int buttonY = LargeTextMetrics::values.buttonHintsHeight;
  constexpr int buttonPositions[] = {12, 144, 276, 408};
  const char* labels[] = {btn1, btn2, btn3, btn4};
  const char* iconLabels[] = {"<<", "o", "^", "v"};
  const int textLineH = lineHeight(renderer);

  for (int i = 0; i < 4; ++i) {
    if (labels[i] == nullptr || labels[i][0] == '\0') continue;

    const int x = buttonPositions[i];
    const int y = pageHeight - buttonY;
    renderer.fillRoundedRect(x, y, buttonWidth, buttonHeight, cornerRadius, Color::White);
    renderer.drawRoundedRect(x, y, buttonWidth, buttonHeight, 1, cornerRadius, true, true, false, false, true);

    auto label = renderer.truncatedTextScaled(UI_10_FONT_ID, iconLabels[i], buttonWidth - 12, textScale);
    const int textW = renderer.getTextWidthScaled(UI_10_FONT_ID, label.c_str(), textScale);
    const int textX = x + (buttonWidth - textW) / 2;
    const int textY = y + (buttonHeight - textLineH) / 2;
    renderer.drawTextScaled(UI_10_FONT_ID, textX, textY, label.c_str(), textScale);
  }

  renderer.setOrientation(origOrientation);
}

void LargeTextTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                          const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                          bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  (void)storeCoverBuffer;
  coverRendered = false;
  coverBufferStored = false;
  bufferRestored = false;

  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);

  const int cardX = rect.x + LargeTextMetrics::values.contentSidePadding;
  const int cardY = rect.y;
  const int cardW = rect.width - LargeTextMetrics::values.contentSidePadding * 2;
  const int cardH = rect.height;
  const bool hasRecent = !recentBooks.empty();
  const bool selected = hasRecent && selectorIndex == 0;

  if (selected) {
    renderer.fillRoundedRect(cardX, cardY, cardW, cardH, cornerRadius, Color::LightGray);
  } else {
    renderer.drawRoundedRect(cardX, cardY, cardW, cardH, 1, cornerRadius, true);
  }

  const int contentX = cardX + 18;
  const int contentW = cardW - 36;
  const int titleLineH = lineHeight(renderer, UI_12_FONT_ID);
  const int bodyLineH = lineHeight(renderer, UI_10_FONT_ID);

  if (!hasRecent) {
    const auto title = fitText(renderer, UI_12_FONT_ID, tr(STR_NO_OPEN_BOOK), contentW, EpdFontFamily::BOLD);
    const auto hint = fitText(renderer, UI_10_FONT_ID, tr(STR_START_READING), contentW);
    const int totalH = titleLineH + 8 + bodyLineH;
    int y = cardY + (cardH - totalH) / 2;
    renderer.drawTextScaled(UI_12_FONT_ID, contentX, y, title.c_str(), textScale, true, EpdFontFamily::BOLD);
    y += titleLineH + 8;
    renderer.drawTextScaled(UI_10_FONT_ID, contentX, y, hint.c_str(), textScale);
    return;
  }

  const RecentBook& book = recentBooks[0];
  const auto title = fitText(renderer, UI_12_FONT_ID, book.title, contentW, EpdFontFamily::BOLD);
  const auto author = fitText(renderer, UI_10_FONT_ID, book.author, contentW);
  const auto action = fitText(renderer, UI_10_FONT_ID, tr(STR_CONTINUE_READING), contentW);
  const int authorH = author.empty() ? 0 : bodyLineH + 8;
  const int totalH = titleLineH + authorH + bodyLineH;
  int y = cardY + std::max(0, (cardH - totalH) / 2);

  renderer.drawTextScaled(UI_12_FONT_ID, contentX, y, title.c_str(), textScale, true, EpdFontFamily::BOLD);
  y += titleLineH;

  if (!author.empty()) {
    y += 8;
    renderer.drawTextScaled(UI_10_FONT_ID, contentX, y, author.c_str(), textScale);
    y += bodyLineH;
  }

  y += 8;
  renderer.drawTextScaled(UI_10_FONT_ID, contentX, y, action.c_str(), textScale);
}

void LargeTextTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                    const std::function<std::string(int index)>& buttonLabel,
                                    const std::function<UIIcon(int index)>& rowIcon) const {
  (void)rowIcon;

  for (int i = 0; i < buttonCount; ++i) {
    const int tileWidth = rect.width - LargeTextMetrics::values.contentSidePadding * 2;
    const Rect tileRect{rect.x + LargeTextMetrics::values.contentSidePadding,
                        rect.y + i * (LargeTextMetrics::values.menuRowHeight + LargeTextMetrics::values.menuSpacing),
                        tileWidth, LargeTextMetrics::values.menuRowHeight};

    const bool selected = selectedIndex == i;
    if (selected) {
      renderer.fillRoundedRect(tileRect.x, tileRect.y, tileRect.width, tileRect.height, cornerRadius,
                               Color::LightGray);
    }

    const int textLineH = lineHeight(renderer, UI_12_FONT_ID);
    const int textY = tileRect.y + (tileRect.height - textLineH) / 2;
    const auto label = renderer.truncatedTextScaled(UI_12_FONT_ID, buttonLabel(i).c_str(), tileRect.width - 32,
                                                    textScale);
    renderer.drawTextScaled(UI_12_FONT_ID, tileRect.x + 16, textY, label.c_str(), textScale);
  }
}

void LargeTextTheme::drawStatusBar(GfxRenderer& renderer, const float bookProgress, const int currentPage,
                                   const int pageCount, std::string title, const int paddingBottom,
                                   const int textYOffset) const {
  int orientedTop = 0;
  int orientedRight = 0;
  int orientedBottom = 0;
  int orientedLeft = 0;
  renderer.getOrientedViewableTRBL(&orientedTop, &orientedRight, &orientedBottom, &orientedLeft);
  (void)orientedTop;

  const bool clockVisible = SETTINGS.statusBarClock && halClock.isAvailable();
  const bool showStatusText = SETTINGS.statusBarChapterPageCount || SETTINGS.statusBarBookProgressPercentage ||
                              SETTINGS.statusBarTitle != CrossPointSettings::STATUS_BAR_TITLE::HIDE_TITLE ||
                              SETTINGS.statusBarBattery || clockVisible;
  const bool showProgressBar =
      SETTINGS.statusBarProgressBar != CrossPointSettings::STATUS_BAR_PROGRESS_BAR::HIDE_PROGRESS;

  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int statusHeight = UITheme::getInstance().getStatusBarHeight();
  const int statusTop = screenH - statusHeight - orientedBottom - paddingBottom;
  const int leftX = LargeTextMetrics::values.statusBarHorizontalMargin + orientedLeft;
  const int rightX = screenW - LargeTextMetrics::values.statusBarHorizontalMargin - orientedRight;

  if (showProgressBar) {
    size_t progress = 0;
    if (SETTINGS.statusBarProgressBar == CrossPointSettings::STATUS_BAR_PROGRESS_BAR::BOOK_PROGRESS) {
      progress = static_cast<size_t>(std::max(0.0f, std::min(100.0f, bookProgress)));
    } else {
      progress = pageCount > 0 ? static_cast<size_t>((static_cast<float>(currentPage) / pageCount) * 100.0f) : 0;
    }
    const int barHeight = (SETTINGS.statusBarProgressBarThickness + 1) * 2;
    const int barY = screenH - orientedBottom - paddingBottom - barHeight;
    const int barWidth = std::max(0, screenW - orientedLeft - orientedRight) * static_cast<int>(progress) / 100;
    renderer.fillRect(orientedLeft, barY, barWidth, barHeight, true);
  }

  if (!showStatusText) {
    return;
  }

  std::string leftText;
  if (clockVisible) {
    char timeBuf[9];
    if (halClock.formatTime(timeBuf, sizeof(timeBuf), SETTINGS.clockUtcOffsetQ, SETTINGS.clockFormat == 1)) {
      leftText = timeBuf;
    }
  } else if (SETTINGS.statusBarTitle != CrossPointSettings::STATUS_BAR_TITLE::HIDE_TITLE && !title.empty()) {
    leftText = title;
  } else if (SETTINGS.statusBarBattery) {
    leftText = std::to_string(powerManager.getBatteryPercentage()) + "%";
  }

  char rightBuf[48] = {};
  if (SETTINGS.statusBarBookProgressPercentage && SETTINGS.statusBarChapterPageCount) {
    std::snprintf(rightBuf, sizeof(rightBuf), "%d/%d %.0f%%", currentPage, pageCount, bookProgress);
  } else if (SETTINGS.statusBarBookProgressPercentage) {
    std::snprintf(rightBuf, sizeof(rightBuf), "%.0f%%", bookProgress);
  } else if (SETTINGS.statusBarChapterPageCount) {
    std::snprintf(rightBuf, sizeof(rightBuf), "%d/%d", currentPage, pageCount);
  } else if (SETTINGS.statusBarBattery && leftText.empty()) {
    std::snprintf(rightBuf, sizeof(rightBuf), "%u%%", powerManager.getBatteryPercentage());
  }

  std::string rightText = rightBuf;
  const int statusLineH = lineHeight(renderer, UI_10_FONT_ID);
  const int textY = statusTop + std::max(0, (LargeTextMetrics::values.statusBarVerticalMargin - statusLineH) / 2) -
                    textYOffset;
  const int gap = 16;
  const int totalTextW = std::max(1, rightX - leftX);
  const int rightMaxW = totalTextW / 2;
  if (!rightText.empty()) {
    rightText = fitText(renderer, UI_10_FONT_ID, rightText, rightMaxW);
  }
  const int rightW = rightText.empty() ? 0 : textWidth(renderer, UI_10_FONT_ID, rightText);
  const int leftMaxW = std::max(1, totalTextW - rightW - gap);
  if (!leftText.empty()) {
    leftText = fitText(renderer, UI_10_FONT_ID, leftText, leftMaxW);
    renderer.drawTextScaled(UI_10_FONT_ID, leftX, textY, leftText.c_str(), textScale);
  }
  if (!rightText.empty()) {
    renderer.drawTextScaled(UI_10_FONT_ID, rightX - rightW, textY, rightText.c_str(), textScale);
  }
}
