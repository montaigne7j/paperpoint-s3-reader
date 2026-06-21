#pragma once

#include "components/themes/lyra/LyraTheme.h"

namespace LargeTextMetrics {
constexpr ThemeMetrics values = {.batteryWidth = 16,
                                 .batteryHeight = 12,
                                 .topPadding = 5,
                                 .batteryBarHeight = 40,
                                 .headerHeight = 112,
                                 .verticalSpacing = 10,
                                 .contentSidePadding = 16,
                                 .listRowHeight = 132,
                                 .listWithSubtitleRowHeight = 132,
                                 .menuRowHeight = 100,
                                 .menuSpacing = 8,
                                 .tabSpacing = 0,
                                 .tabBarHeight = 76,
                                 .scrollBarWidth = 4,
                                 .scrollBarRightOffset = 5,
                                 .homeTopPadding = 56,
                                 .homeCoverHeight = 305,
                                 .homeCoverTileHeight = 327,
                                 .homeRecentBooksCount = 1,
                                 .buttonHintsHeight = 96,
                                 .sideButtonHintsWidth = 0,
                                 .progressBarHeight = 16,
                                 .progressBarMarginTop = 1,
                                 .statusBarHorizontalMargin = 5,
                                 .statusBarVerticalMargin = 64,
                                 .keyboardKeyWidth = 31,
                                 .keyboardKeyHeight = 50,
                                 .keyboardKeySpacing = 0,
                                 .keyboardBottomAligned = true,
                                 .keyboardCenteredText = true};
}  // namespace LargeTextMetrics

class LargeTextTheme : public LyraTheme {
 public:
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle) const override;
  void drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label,
                     const char* rightLabel = nullptr) const override;
  void drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                  bool selected) const override;
  void drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                const std::function<std::string(int index)>& rowTitle,
                const std::function<std::string(int index)>& rowSubtitle,
                const std::function<UIIcon(int index)>& rowIcon, const std::function<std::string(int index)>& rowValue,
                bool highlightValue) const override;
  void redrawListSelection(const GfxRenderer& renderer, Rect rect, int itemCount, int oldSelectedIndex,
                           int newSelectedIndex, const std::function<std::string(int index)>& rowTitle,
                           const std::function<std::string(int index)>& rowSubtitle,
                           const std::function<UIIcon(int index)>& rowIcon,
                           const std::function<std::string(int index)>& rowValue,
                           bool highlightValue) const override;
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                       const char* btn4) const override;
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                           bool& bufferRestored, std::function<bool()> storeCoverBuffer) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
  void drawStatusBar(GfxRenderer& renderer, const float bookProgress, const int currentPage, const int pageCount,
                     std::string title, const int paddingBottom = 0, const int textYOffset = 0) const override;
};
