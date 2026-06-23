#include "EpubReaderChapterSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <limits>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/util/DirectTouchSelection.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long BACK_LONG_PRESS_MS = 1000;

constexpr int LARGE_TEXT_SCALE = 2;
constexpr int LARGE_TEXT_CORNER_RADIUS = 6;

bool isLargeTextTheme() {
  return SETTINGS.uiTheme == CrossPointSettings::UI_THEME::LARGE_TEXT;
}
}

int EpubReaderChapterSelectionActivity::getTotalItems() const {
  return static_cast<int>(visibleTocIndices.size());
}

int EpubReaderChapterSelectionActivity::getPageItems() const {
  if (isLargeTextTheme()) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int screenHeight = renderer.getScreenHeight();
    const auto orientation = renderer.getOrientation();
    const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
    const int contentY = isPortraitInverted ? 50 : 0;
    const int contentTop = contentY + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight = screenHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
    return std::max(1, contentHeight / metrics.listRowHeight);
  }

  // Layout constants used in renderScreen
#if CROSSPOINT_PAPERS3
  constexpr int lineHeight = 75;
#else
  constexpr int lineHeight = 30;
#endif

  const int screenHeight = renderer.getScreenHeight();
  const auto orientation = renderer.getOrientation();
  // In inverted portrait, the button hints are drawn near the logical top.
  // Reserve vertical space so list items do not collide with the hints.
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterHeight = isPortraitInverted ? 50 : 0;
  const int startY = 60 + hintGutterHeight;
  const int availableHeight = screenHeight - startY - lineHeight;
  // Clamp to at least one item to avoid division by zero and empty paging.
  return std::max(1, availableHeight / lineHeight);
}

bool EpubReaderChapterSelectionActivity::nodeHasChildren(const int tocIndex) const {
  return tocIndex >= 0 && tocIndex < static_cast<int>(tocNodes.size()) &&
         (tocNodes[tocIndex].flags & NODE_HAS_CHILDREN) != 0;
}

bool EpubReaderChapterSelectionActivity::nodeIsExpanded(const int tocIndex) const {
  return tocIndex >= 0 && tocIndex < static_cast<int>(tocNodes.size()) &&
         (tocNodes[tocIndex].flags & NODE_EXPANDED) != 0;
}

void EpubReaderChapterSelectionActivity::setNodeExpanded(const int tocIndex, const bool expanded) {
  if (tocIndex < 0 || tocIndex >= static_cast<int>(tocNodes.size())) return;

  if (expanded) {
    tocNodes[tocIndex].flags |= NODE_EXPANDED;
  } else {
    tocNodes[tocIndex].flags &= static_cast<uint8_t>(~NODE_EXPANDED);
  }
}

void EpubReaderChapterSelectionActivity::buildTreeState() {
  const uint32_t treeStart = millis();
  tocNodes.clear();
  visibleTocIndices.clear();
  selectorIndex = 0;
  currentTocIndex = -1;

  if (!epub) return;

  const int tocCount = epub->getTocItemsCount();
  if (tocCount <= 0) return;

  std::vector<BookMetadataCache::TocNodeInfo> nodeInfos;
  if (!epub->getTocNodeInfos(nodeInfos) || static_cast<int>(nodeInfos.size()) != tocCount) {
    LOG_ERR("TOCUI", "Could not load TOC tree metadata in batch");
    return;
  }

  tocNodes.reserve(tocCount);

  uint8_t minimumLevel = std::numeric_limits<uint8_t>::max();
  for (const auto& item : nodeInfos) {
    TocNodeState node;
    node.level = item.level == 0 ? 1 : item.level;
    node.spineIndex = item.spineIndex;
    tocNodes.push_back(node);
    minimumLevel = std::min(minimumLevel, node.level);
  }

  // Some malformed books start their first TOC level above 1. Normalising the
  // minimum to level 1 keeps indentation and ancestor detection predictable
  // without changing the actual parent/child relationships.
  if (minimumLevel > 1 && minimumLevel != std::numeric_limits<uint8_t>::max()) {
    for (auto& node : tocNodes) {
      node.level = static_cast<uint8_t>(node.level - minimumLevel + 1);
    }
  }

  // EPUB TOCs are cached in preorder. A node has children when the following
  // entry has a deeper level.
  for (int i = 0; i + 1 < tocCount; ++i) {
    if (tocNodes[i + 1].level > tocNodes[i].level) {
      tocNodes[i].flags |= NODE_HAS_CHILDREN;
    }
  }

  currentTocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);

  // A spine item is not always represented exactly in the TOC. In that case,
  // select the nearest preceding TOC destination so the user still opens the
  // tree near the chapter currently being read.
  if (currentTocIndex < 0 || currentTocIndex >= tocCount) {
    int nearestTocIndex = -1;
    int nearestSpineIndex = -1;

    for (int i = 0; i < tocCount; ++i) {
      const int spineIndex = tocNodes[i].spineIndex;
      if (spineIndex >= 0 && spineIndex <= currentSpineIndex && spineIndex >= nearestSpineIndex) {
        nearestSpineIndex = spineIndex;
        nearestTocIndex = i;
      }
    }

    currentTocIndex = nearestTocIndex;
  }

  expandCurrentBranch();
  rebuildVisibleItems(currentTocIndex);

  LOG_DBG("TOCUI", "Tree ready: total=%d visible=%d currentToc=%d time=%lu ms", tocCount, getTotalItems(),
          currentTocIndex, millis() - treeStart);
}

void EpubReaderChapterSelectionActivity::expandCurrentBranch() {
  if (currentTocIndex < 0 || currentTocIndex >= static_cast<int>(tocNodes.size())) return;

  // Walk backwards through the preorder list. The nearest preceding entry at
  // each shallower level is the current node's parent, then grandparent, etc.
  uint8_t childLevel = tocNodes[currentTocIndex].level;
  for (int i = currentTocIndex - 1; i >= 0 && childLevel > 1; --i) {
    if (tocNodes[i].level < childLevel) {
      setNodeExpanded(i, true);
      childLevel = tocNodes[i].level;
    }
  }

  // When the current TOC entry is itself a group, reveal its immediate child
  // level too. Deeper branches remain collapsed until the user opens them.
  if (nodeHasChildren(currentTocIndex)) {
    setNodeExpanded(currentTocIndex, true);
  }
}

void EpubReaderChapterSelectionActivity::rebuildVisibleItems(const int keepTocIndex) {
  visibleTocIndices.clear();
  visibleTocIndices.reserve(tocNodes.size());

  bool insideCollapsedBranch = false;
  uint8_t collapsedBranchLevel = 0;

  for (int i = 0; i < static_cast<int>(tocNodes.size()); ++i) {
    const auto level = tocNodes[i].level;

    if (insideCollapsedBranch) {
      if (level > collapsedBranchLevel) {
        continue;
      }
      insideCollapsedBranch = false;
    }

    visibleTocIndices.push_back(static_cast<uint16_t>(i));

    if (nodeHasChildren(i) && !nodeIsExpanded(i)) {
      insideCollapsedBranch = true;
      collapsedBranchLevel = level;
    }
  }

  selectorIndex = 0;
  if (keepTocIndex >= 0) {
    for (int i = 0; i < static_cast<int>(visibleTocIndices.size()); ++i) {
      if (visibleTocIndices[i] == keepTocIndex) {
        selectorIndex = i;
        break;
      }
    }
  }

  if (!visibleTocIndices.empty()) {
    selectorIndex = std::clamp(selectorIndex, 0, static_cast<int>(visibleTocIndices.size()) - 1);
  }
}

int EpubReaderChapterSelectionActivity::getSelectedTocIndex() const {
  if (selectorIndex < 0 || selectorIndex >= static_cast<int>(visibleTocIndices.size())) return -1;
  return visibleTocIndices[selectorIndex];
}

bool EpubReaderChapterSelectionActivity::moveSelectionToParent() {
  const int selectedTocIndex = getSelectedTocIndex();
  if (selectedTocIndex < 0 || selectedTocIndex >= static_cast<int>(tocNodes.size())) return false;

  const uint8_t selectedLevel = tocNodes[selectedTocIndex].level;
  if (selectedLevel <= 1) return false;

  // visibleTocIndices is a preorder list. For a visible child, its parent is
  // the nearest preceding visible item whose level is shallower.
  for (int visibleIndex = selectorIndex - 1; visibleIndex >= 0; --visibleIndex) {
    const int candidateTocIndex = visibleTocIndices[visibleIndex];
    if (candidateTocIndex < 0 || candidateTocIndex >= static_cast<int>(tocNodes.size())) continue;

    if (tocNodes[candidateTocIndex].level < selectedLevel) {
      selectorIndex = visibleIndex;
      LOG_DBG("TOCUI", "Back to parent: toc=%d level=%u -> toc=%d level=%u", selectedTocIndex, selectedLevel,
              candidateTocIndex, tocNodes[candidateTocIndex].level);
      requestUpdate();
      return true;
    }
  }

  return false;
}

void EpubReaderChapterSelectionActivity::finishToReader() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void EpubReaderChapterSelectionActivity::activateSelectedItem() {
  const int tocIndex = getSelectedTocIndex();
  if (tocIndex < 0) return;

  if (nodeHasChildren(tocIndex)) {
    const bool expand = !nodeIsExpanded(tocIndex);
    setNodeExpanded(tocIndex, expand);
    rebuildVisibleItems(tocIndex);

    LOG_DBG("TOCUI", "%s node %d (level=%u, visible=%d)", expand ? "Expanded" : "Collapsed", tocIndex,
            tocNodes[tocIndex].level, getTotalItems());
    requestUpdate();
    return;
  }

  const int newSpineIndex = tocNodes[tocIndex].spineIndex;
  if (newSpineIndex < 0) {
    LOG_DBG("TOCUI", "TOC leaf %d has no matching spine item", tocIndex);
    return;
  }

  setResult(ChapterResult{newSpineIndex});
  finish();
}

void EpubReaderChapterSelectionActivity::drawDisclosureTriangle(const int x, const int centerY, const bool expanded,
                                                                const bool black, const int halfSize,
                                                                const int fullSize) const {
  int xPoints[3];
  int yPoints[3];

  if (expanded) {
    // Down-pointing triangle.
    xPoints[0] = x;
    yPoints[0] = centerY - halfSize / 2;
    xPoints[1] = x + fullSize;
    yPoints[1] = centerY - halfSize / 2;
    xPoints[2] = x + fullSize / 2;
    yPoints[2] = centerY + halfSize;
  } else {
    // Right-pointing triangle.
    xPoints[0] = x;
    yPoints[0] = centerY - halfSize;
    xPoints[1] = x;
    yPoints[1] = centerY + halfSize;
    xPoints[2] = x + fullSize;
    yPoints[2] = centerY;
  }

  renderer.fillPolygon(xPoints, yPoints, 3, black);
}

void EpubReaderChapterSelectionActivity::onEnter() {
  Activity::onEnter();

#if CROSSPOINT_PAPERS3
  // Keep the reader orientation for the chapter list, but enable the four
  // footer touch zones explicitly. drawButtonHints() draws those controls
  // along the physical bottom edge in portrait coordinates. Without this,
  // taps use the reader's 3-zone layout: Back acts as Up and Up as Select.
  mappedInput.setTouchOrientation(static_cast<uint8_t>(GfxRenderer::Orientation::Portrait));
  mappedInput.setFooterHeight(UITheme::getInstance().getMetrics().buttonHintsHeight);
#endif

  buildTreeState();

  // Trigger first update
  requestUpdate();
}

void EpubReaderChapterSelectionActivity::onExit() {
  tocNodes.clear();
  visibleTocIndices.clear();
  Activity::onExit();
}

void EpubReaderChapterSelectionActivity::loop() {
  const int pageItems = getPageItems();
  const int totalItems = getTotalItems();

#if CROSSPOINT_PAPERS3
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelectedItem();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    const unsigned long heldTime = mappedInput.getHeldTime();

    // A long Back always returns directly to the reading page, regardless of
    // the currently selected depth in the TOC tree.
    if (heldTime >= BACK_LONG_PRESS_MS) {
      LOG_DBG("TOCUI", "Long Back (%lu ms): return to reader", heldTime);
      finishToReader();
      return;
    }

    // A short Back acts as tree navigation first. From a nested item it moves
    // the cursor to its parent; only Back from a top-level item exits.
    if (moveSelectionToParent()) {
      return;
    }

    LOG_DBG("TOCUI", "Back from top level: return to reader");
    finishToReader();
    return;
  }

  if (totalItems <= 0) return;

  {
    const auto orientation = renderer.getOrientation();
    const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
    const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
    const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
    const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? 30 : 0;
    const int contentX = isLandscapeCw ? hintGutterWidth : 0;
    const int contentWidth = renderer.getScreenWidth() - hintGutterWidth;
    const int contentY = isPortraitInverted ? 50 : 0;
    Rect listRect;
    int rowHeight = 0;
    if (isLargeTextTheme()) {
      const auto& metrics = UITheme::getInstance().getMetrics();
      const int listTop = contentY + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
      const int listHeight = renderer.getScreenHeight() - listTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
      listRect = Rect{contentX, listTop, contentWidth, std::max(1, listHeight)};
      rowHeight = metrics.listRowHeight;
    } else {
      constexpr int lineHeight = 75;
      listRect = Rect{contentX, 60 + contentY, contentWidth, pageItems * lineHeight};
      rowHeight = lineHeight;
    }

    const int targetIndex =
        DirectTouchSelection::hitListRow(mappedInput, listRect, totalItems, selectorIndex, rowHeight);
    if (targetIndex >= 0) {
      if (targetIndex == selectorIndex) {
        activateSelectedItem();
      } else {
        selectorIndex = targetIndex;
        requestUpdate();
      }
      return;
    }
  }

  buttonNavigator.onNextRelease([this, totalItems, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, totalItems, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
  });
#endif
}

void EpubReaderChapterSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto orientation = renderer.getOrientation();
  // Landscape orientation: reserve a horizontal gutter for button hints.
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  // Inverted portrait: reserve vertical space for hints at the top.
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? 30 : 0;
  // Landscape CW places hints on the left edge; CCW keeps them on the right.
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = pageWidth - hintGutterWidth;
  const int hintGutterHeight = isPortraitInverted ? 50 : 0;
  const int contentY = hintGutterHeight;
  const int pageItems = getPageItems();
  const int totalItems = getTotalItems();

  if (isLargeTextTheme()) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int pageHeight = renderer.getScreenHeight();
    const int headerTop = contentY + metrics.topPadding;
    GUI.drawHeader(renderer, Rect{contentX, headerTop, contentWidth, metrics.headerHeight}, tr(STR_SELECT_CHAPTER));

    const int listTop = headerTop + metrics.headerHeight + metrics.verticalSpacing;
    const int listHeight = std::max(1, pageHeight - listTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2);
    const int rowHeight = metrics.listRowHeight;
    const int rowX = contentX + metrics.contentSidePadding;
    const int rowW = std::max(1, contentWidth - metrics.contentSidePadding * 2 -
                                     (totalItems > pageItems ? metrics.scrollBarWidth + metrics.scrollBarRightOffset + 8
                                                             : 0));
    constexpr int leftPadding = 14;
    constexpr int rightPadding = 18;
    constexpr int indentStep = 32;
    constexpr int markerTextGap = 34;
    constexpr int maximumVisualDepth = 8;
    const int textLineH = renderer.getLineHeightScaled(UI_10_FONT_ID, LARGE_TEXT_SCALE);
    const int textYOff = (rowHeight - textLineH) / 2;
    const int pageStartIndex = selectorIndex / pageItems * pageItems;

    if (totalItems > pageItems) {
      const int totalPages = (totalItems + pageItems - 1) / pageItems;
      const int currentPage = selectorIndex / pageItems;
      const int scrollBarH = std::max(12, (listHeight * pageItems) / std::max(1, totalItems));
      const int scrollBarY = listTop + ((listHeight - scrollBarH) * currentPage) / std::max(1, totalPages - 1);
      const int scrollBarX = contentX + contentWidth - metrics.scrollBarRightOffset;
      renderer.drawLine(scrollBarX, listTop, scrollBarX, listTop + listHeight, true);
      renderer.fillRect(scrollBarX - metrics.scrollBarWidth, scrollBarY, metrics.scrollBarWidth, scrollBarH, true);
    }

    for (int i = 0; i < pageItems; i++) {
      const int visibleIndex = pageStartIndex + i;
      if (visibleIndex >= totalItems) break;

      const int tocIndex = visibleTocIndices[visibleIndex];
      const int displayY = listTop + i * rowHeight;
      const bool isSelected = visibleIndex == selectorIndex;
      const auto item = epub->getTocItem(tocIndex);
      const auto& node = tocNodes[tocIndex];

      if (isSelected) {
        renderer.fillRoundedRect(rowX, displayY + 2, rowW, rowHeight - 4, LARGE_TEXT_CORNER_RADIUS,
                                 Color::LightGray);
      }

      const int visualDepth = std::min(std::max(0, static_cast<int>(node.level) - 1), maximumVisualDepth);
      const int markerX = rowX + leftPadding + visualDepth * indentStep;
      const int textX = markerX + markerTextGap;

      if (nodeHasChildren(tocIndex)) {
        drawDisclosureTriangle(markerX, displayY + rowHeight / 2, nodeIsExpanded(tocIndex), true, 11, 20);
      }

      const int maxTextWidth = std::max(20, rowX + rowW - rightPadding - textX);
      const std::string chapterName =
          renderer.truncatedTextScaled(UI_10_FONT_ID, item.title.c_str(), maxTextWidth, LARGE_TEXT_SCALE);
      renderer.drawTextScaled(UI_10_FONT_ID, textX, displayY + textYOff, chapterName.c_str(), LARGE_TEXT_SCALE, true);
    }

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

    renderer.displayBuffer();
    return;
  }

  // Manual centering to honor content gutters.
  const int titleX =
      contentX + (contentWidth - renderer.getTextWidth(UI_12_FONT_ID, tr(STR_SELECT_CHAPTER), EpdFontFamily::BOLD)) / 2;
  renderer.drawText(UI_12_FONT_ID, titleX, 15 + contentY, tr(STR_SELECT_CHAPTER), true, EpdFontFamily::BOLD);

#if CROSSPOINT_PAPERS3
  constexpr int lineHeight = 75;
  constexpr int indentStep = 18;
  constexpr int maximumVisualDepth = 10;
#else
  constexpr int lineHeight = 30;
  constexpr int indentStep = 12;
  constexpr int maximumVisualDepth = 10;
#endif
  constexpr int leftPadding = 18;
  constexpr int markerTextGap = 18;
  constexpr int rightPadding = 18;

  const int textLineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int textYOff = (lineHeight - textLineH) / 2;

  const auto pageStartIndex = selectorIndex / pageItems * pageItems;
  if (totalItems > 0) {
    // Highlight only the content area, not the hint gutters.
    renderer.fillRect(contentX, 60 + contentY + (selectorIndex % pageItems) * lineHeight, contentWidth - 1, lineHeight);
  }

  for (int i = 0; i < pageItems; i++) {
    const int visibleIndex = pageStartIndex + i;
    if (visibleIndex >= totalItems) break;

    const int tocIndex = visibleTocIndices[visibleIndex];
    const int displayY = 60 + contentY + i * lineHeight;
    const bool isSelected = visibleIndex == selectorIndex;
    const auto item = epub->getTocItem(tocIndex);
    const auto& node = tocNodes[tocIndex];

    const int visualDepth = std::min(std::max(0, static_cast<int>(node.level) - 1), maximumVisualDepth);
    const int markerX = contentX + leftPadding + visualDepth * indentStep;
    const int textX = markerX + markerTextGap;

    if (nodeHasChildren(tocIndex)) {
      drawDisclosureTriangle(markerX, displayY + lineHeight / 2, nodeIsExpanded(tocIndex), !isSelected);
    }

    const int maxTextWidth = std::max(20, contentX + contentWidth - rightPadding - textX);
    const std::string chapterName = renderer.truncatedText(UI_10_FONT_ID, item.title.c_str(), maxTextWidth);

    renderer.drawText(UI_10_FONT_ID, textX, displayY + textYOff, chapterName.c_str(), !isSelected);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
