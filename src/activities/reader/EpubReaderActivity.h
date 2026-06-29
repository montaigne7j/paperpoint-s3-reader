#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <vector>
#include <Epub.h>
#include <Epub/FootnoteEntry.h>
#include <Epub/Page.h>
#include <Epub/Section.h>

#include "EpubReaderMenuActivity.h"
#include "activities/Activity.h"

class EpubReaderActivity final : public Activity {
  std::shared_ptr<Epub> epub;
  std::unique_ptr<Section> section = nullptr;
  int currentSpineIndex = 0;
  int nextPageNumber = 0;
  // Set when navigating to a footnote href with a fragment (e.g. #note1).
  // Cleared on the next render after the new section loads and resolves it to a page.
  std::string pendingAnchor;
  int pagesUntilFullRefresh = 0;
  int cachedSpineIndex = 0;
  int cachedChapterTotalPageCount = 0;
  unsigned long lastPageTurnTime = 0UL;
  unsigned long pageTurnDuration = 0UL;
  // Signals that the next render should reposition within the newly loaded section
  // based on a cross-book percentage jump.
  bool pendingPercentJump = false;
  // Normalized 0.0-1.0 progress within the target spine item, computed from book percentage.
  float pendingSpineProgress = 0.0f;
  bool pendingScreenshot = false;
  bool skipNextButtonCheck = false;  // Skip button processing for one frame after subactivity exit
  bool automaticPageTurnActive = false;
  bool lastPageTurnWasForward = true;
  bool pendingNextChapterPreindex = false;
  unsigned long nextChapterPreindexAt = 0UL;
  uint16_t pendingPreindexViewportWidth = 0;
  uint16_t pendingPreindexViewportHeight = 0;

#if CROSSPOINT_PAPERS3
  static constexpr int PAGE_FRAME_CACHE_SLOT_COUNT = 4;
  struct PageFrameCacheEntry {
    uint8_t* buffer = nullptr;
    bool valid = false;
    int spineIndex = -1;
    int pageNumber = -1;
    int width = 0;
    int height = 0;
    bool hasImages = false;
    std::vector<FootnoteEntry> footnotes;
  };
  std::array<PageFrameCacheEntry, PAGE_FRAME_CACHE_SLOT_COUNT> pageFrameCache{};
  int pageFrameCacheNextWrite = 0;
  unsigned long lastPageFrameCacheWorkAt = 0;
  unsigned long lastReaderInputAt = 0;
  unsigned long lastVisibleDisplayIdleAt = 0;
  bool pendingPageTurnActive = false;
  bool pendingPageTurnForward = true;
  unsigned long pendingPageTurnAt = 0;
  bool lastVisiblePageHadImages = false;
  bool restoredPageFrameHadImages = false;

  struct PageFrameCacheWarmJob {
    bool active = false;
    int spineIndex = -1;
    int pageNumber = -1;
    int visiblePageNumber = -1;
    int orientedMarginTop = 0;
    int orientedMarginRight = 0;
    int orientedMarginBottom = 0;
    int orientedMarginLeft = 0;
    size_t nextElementIndex = 0;
    unsigned long startedAt = 0;
    unsigned long lastChunkAt = 0;
    std::unique_ptr<Page> page;
    std::vector<FootnoteEntry> footnotes;
    bool hasImages = false;
  } pageFrameCacheWarmJob;

  enum class ReaderMemoryState : uint8_t { NORMAL = 0, WARNING = 1, CRITICAL = 2, EMERGENCY = 3 };
  struct ReaderMemorySnapshot {
    uint32_t internalFree = 0;
    uint32_t internalMaxAlloc = 0;
    uint32_t psramFree = 0;
    uint32_t psramMaxAlloc = 0;
  };
  ReaderMemoryState lastReaderMemoryState = ReaderMemoryState::NORMAL;
  bool readerMemoryStateInitialized = false;
  unsigned long lastReaderMemoryLogAt = 0;
  unsigned long lastReaderMemoryPauseLogAt = 0;
  bool pendingPageTurnForceVisible = false;
  unsigned long pendingPageTurnForceVisibleAt = 0;
  int pageFrameCacheLowMemoryCooldownSpine = -1;
  int pageFrameCacheLowMemoryCooldownPage = -1;
  unsigned long pageFrameCacheLowMemoryCooldownUntil = 0;
  unsigned long lastPageFrameCacheLowMemoryCooldownLogAt = 0;
  unsigned long lastPageFrameCacheLowMemorySkipLogAt = 0;
  unsigned long lastIdleGlyphPrewarmAt = 0;
  unsigned long idleGlyphPrewarmPausedUntil = 0;

  bool ensurePageFrameCacheAllocated();
  bool ensurePageFrameCacheEntryBuffer(PageFrameCacheEntry& entry);
  void clearPageFrameCache(bool freeBuffers = false);
  void invalidatePageFrameCacheEntry(PageFrameCacheEntry& entry, bool freeBuffer, const char* reason);
  PageFrameCacheEntry* findPageFrameCacheEntry(int spineIndex, int pageNumber);
  PageFrameCacheEntry* acquirePageFrameCacheEntry(int spineIndex, int pageNumber);
  ReaderMemorySnapshot getReaderMemorySnapshot() const;
  ReaderMemoryState classifyReaderMemory(const ReaderMemorySnapshot& snapshot) const;
  const char* readerMemoryStateName(ReaderMemoryState state) const;
  ReaderMemoryState updateReaderMemoryState(const char* context, bool forceLog = false);
  bool isFrameCacheTargetAllowedForMemoryState(int pageNumber, ReaderMemoryState state) const;
  bool readerMemoryAllowsSilentIndexing(const char* phase);
  bool readerMemoryAllowsVisibleFrameStore(const char* phase);
  bool readerMemoryAllowsFrameCacheStart(const ReaderMemorySnapshot& snapshot, bool pendingTurnTarget, const char* phase);
  bool isPageFrameCacheLowMemoryCooldownActive(int spineIndex, int pageNumber) const;
  void markPageFrameCacheLowMemoryCooldown(int spineIndex, int pageNumber, const char* reason);
  bool shouldSkipPageFrameCacheForCooldown(int spineIndex, int pageNumber);
  void prunePageFrameCacheForMemoryState(ReaderMemoryState state, const char* reason);
  bool copyCurrentFrameToPageFrameCache(int spineIndex, int pageNumber, const std::vector<FootnoteEntry>& footnotes,
                                       bool hasImages);
  bool restorePageFrameCacheToRenderer(int spineIndex, int pageNumber, bool restoreFootnotes);
  bool renderPageToFrameCache(int pageNumber, int orientedMarginTop, int orientedMarginRight,
                              int orientedMarginBottom, int orientedMarginLeft);
  bool collectPageTtfPrewarmCodepoints(int pageNumber, std::vector<uint32_t>& out, size_t maxCodepoints);
  bool idleGlyphPrewarmIfReady();
  bool hasReaderInputPending() const;
  bool capturePageTurnInput(bool& isForwardTurn) const;
  bool queuePendingPageTurn(bool isForwardTurn, const char* source);
  void clearPendingPageTurn();
  bool executePendingPageTurnIfReady(const char* source);
  bool sameSectionPageTurnTarget(bool isForwardTurn, int& targetPage) const;
  bool pageFrameCacheReadyForTurn(bool isForwardTurn);
  bool adjacentPageFrameCachesReady();
  void abortPageFrameCacheWarmJob();
  bool startPageFrameCacheWarmJob(int pageNumber, int orientedMarginTop, int orientedMarginRight,
                                  int orientedMarginBottom, int orientedMarginLeft);
  bool continuePageFrameCacheWarmJobChunk();
  void waitForVisibleDisplayIdle(const char* source);
  void warmPageFrameCacheIfIdle();
#endif

  // Footnote support
  std::vector<FootnoteEntry> currentPageFootnotes;
  struct SavedPosition {
    int spineIndex;
    int pageNumber;
  };
  static constexpr int MAX_FOOTNOTE_DEPTH = 3;
  SavedPosition savedPositions[MAX_FOOTNOTE_DEPTH] = {};
  int footnoteDepth = 0;

  void renderContents(std::unique_ptr<Page> page, int orientedMarginTop, int orientedMarginRight,
                      int orientedMarginBottom, int orientedMarginLeft);
#if CROSSPOINT_PAPERS3
  bool renderContentsProgressive(std::unique_ptr<Page> page, int orientedMarginTop, int orientedMarginRight,
                                 int orientedMarginBottom, int orientedMarginLeft);
#endif
  void renderStatusBar() const;
  void scheduleSilentIndexNextChapter(uint16_t viewportWidth, uint16_t viewportHeight);
  void silentIndexNextChapterIfNeeded(uint16_t viewportWidth, uint16_t viewportHeight);
  void saveProgress(int spineIndex, int currentPage, int pageCount);
  // Jump to a percentage of the book (0-100), mapping it to spine and page.
  void jumpToPercent(int percent);
  void onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action);
  void applyOrientation(uint8_t orientation);
  void toggleAutoPageTurn(uint8_t selectedPageTurnOption);
  void openReaderMenu();
  void openReaderSettings();
  void pageTurn(bool isForwardTurn);

  // Footnote navigation
  void navigateToHref(const std::string& href, bool savePosition = false);
  void restoreSavedPosition();

 public:
  explicit EpubReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Epub> epub)
      : Activity("EpubReader", renderer, mappedInput), epub(std::move(epub)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;
  bool isReaderActivity() const override { return true; }
};
