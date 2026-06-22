#pragma once
#include <Epub.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class EpubReaderChapterSelectionActivity final : public Activity {
  struct TocNodeState {
    // Compact per-entry state. BookMetadataCache itself stores at most uint16_t
    // TOC entries, so keeping a separate visible-index vector is inexpensive.
    uint8_t level = 1;
    uint8_t flags = 0;
    int16_t spineIndex = -1;
  };

  static constexpr uint8_t NODE_HAS_CHILDREN = 0x01;
  static constexpr uint8_t NODE_EXPANDED = 0x02;

  std::shared_ptr<Epub> epub;
  std::string epubPath;
  ButtonNavigator buttonNavigator;
  int currentSpineIndex = 0;

  // selectorIndex is an index into visibleTocIndices, not the flattened EPUB
  // TOC. visibleTocIndices maps each visible row back to the original TOC.
  int selectorIndex = 0;
  int currentTocIndex = -1;
  std::vector<TocNodeState> tocNodes;
  std::vector<uint16_t> visibleTocIndices;

  // Number of items that fit on a page, derived from logical screen height.
  // This adapts automatically when switching between portrait and landscape.
  int getPageItems() const;

  // Number of currently visible rows in the collapsed/expanded tree.
  int getTotalItems() const;

  bool nodeHasChildren(int tocIndex) const;
  bool nodeIsExpanded(int tocIndex) const;
  void setNodeExpanded(int tocIndex, bool expanded);

  // Build a compact tree from the EPUB's flattened preorder TOC. Only the
  // current chapter's ancestor branch is expanded initially; other branches
  // remain collapsed.
  void buildTreeState();
  void expandCurrentBranch();
  void rebuildVisibleItems(int keepTocIndex);

  int getSelectedTocIndex() const;
  void activateSelectedItem();

  // Short Back moves the selection to the nearest visible parent. It returns
  // false when the current item is already at the top level.
  bool moveSelectionToParent();
  void finishToReader();

  void drawDisclosureTriangle(int x, int centerY, bool expanded, bool black, int halfSize = 7,
                              int fullSize = 12) const;

 public:
  explicit EpubReaderChapterSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                              const std::shared_ptr<Epub>& epub, const std::string& epubPath,
                                              const int currentSpineIndex)
      : Activity("EpubReaderChapterSelection", renderer, mappedInput),
        epub(epub),
        epubPath(epubPath),
        currentSpineIndex(currentSpineIndex) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
};
