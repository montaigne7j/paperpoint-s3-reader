#include "EpubReaderActivity.h"

#include <Epub/Page.h>
#include <Epub/PageRenderProfiler.h>
#include <ExternalFont.h>
#include <FontManager.h>
#include <Utf8.h>
#include <Epub/blocks/TextBlock.h>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <esp_system.h>
#if CROSSPOINT_PAPERS3
#include <esp_heap_caps.h>
#if __has_include(<esp_memory_utils.h>)
#include <esp_memory_utils.h>
#define CROSSPOINT_HAS_ESP_MEMORY_UTILS 1
#else
#define CROSSPOINT_HAS_ESP_MEMORY_UTILS 0
#endif
#endif

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "EpubReaderChapterSelectionActivity.h"
#include "activities/settings/SettingsActivity.h"
#include "EpubReaderFootnotesActivity.h"
#include "EpubReaderPercentSelectionActivity.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderSyncActivity.h"
#include "MappedInputManager.h"
#include "QrDisplayActivity.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ScreenshotUtil.h"

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <vector>
#include <string>
#include <utility>
#include <cstdio>


namespace {
// pagesPerRefresh now comes from SETTINGS.getRefreshFrequency()
constexpr unsigned long skipChapterMs = 700;
#if CROSSPOINT_PAPERS3
constexpr unsigned long pageFrameCacheIdleDelayMs = 120;
constexpr unsigned long pageFrameCacheWorkCooldownMs = 80;
constexpr unsigned long pageFrameCacheLowMemoryAbortCooldownMs = 8000;
constexpr unsigned long pageFrameCacheLowMemorySkipLogIntervalMs = 2000;
constexpr uint32_t pageFrameCacheStartInternalFreeThreshold = 30000;
constexpr uint32_t pageFrameCacheStartInternalMaxAllocThreshold = 12000;
constexpr uint32_t visibleTtfRasterizeLowMemoryFreeThreshold = 12000;
constexpr uint32_t visibleTtfRasterizeLowMemoryMaxAllocThreshold = 4096;
constexpr unsigned long idleGlyphPrewarmDelayMs = 500;
constexpr unsigned long idleGlyphPrewarmCooldownMs = 120;
constexpr uint8_t idleGlyphPrewarmMaxGlyphsPerPass = 1;
constexpr uint32_t idleGlyphPrewarmInternalFreeThreshold = 100000;
constexpr uint32_t idleGlyphPrewarmInternalMaxAllocThreshold = 50000;
constexpr uint32_t idleGlyphPrewarmPsramFreeThreshold = 3UL * 1024UL * 1024UL;
constexpr int32_t idleGlyphPrewarmStopFreeDrop = -6000;
constexpr int32_t idleGlyphPrewarmStopMaxAllocDrop = -4096;
constexpr uint8_t pageFrameCacheCooperativeChunks = 20;
constexpr unsigned long pageFrameCacheChunkBudgetMs = 45;
constexpr uint32_t readerMemNormalFreeThreshold = 24000;
constexpr uint32_t readerMemNormalMaxAllocThreshold = 8192;
constexpr uint32_t readerMemWarningFreeThreshold = 8000;
constexpr uint32_t readerMemWarningMaxAllocThreshold = 1000;
constexpr uint32_t readerMemEmergencyFreeThreshold = 3000;
constexpr uint32_t readerMemEmergencyMaxAllocThreshold = 768;
constexpr unsigned long readerMemoryLogIntervalMs = 30000;
constexpr unsigned long readerMemoryPauseLogIntervalMs = 10000;
constexpr uint8_t foregroundProgressiveStripeCount = 10;
constexpr uint8_t foregroundProgressiveElementsPerRefresh = 2;

struct ReaderHeapTrace {
  uint32_t internalFree = 0;
  uint32_t internalMaxAlloc = 0;
  uint32_t psramFree = 0;
  uint32_t psramMaxAlloc = 0;
};

class ScopedTtfRasterizePolicy {
 public:
  ScopedTtfRasterizePolicy(const bool allowed, const char* reason)
      : previous_(ExternalFont::setRuntimeTtfRasterizeAllowed(allowed, reason)) {
    ExternalFont::resetRuntimeTtfMissSuppressed();
  }
  ~ScopedTtfRasterizePolicy() {
    ExternalFont::setRuntimeTtfRasterizeAllowed(previous_, previous_ ? nullptr : "restore");
  }

  ScopedTtfRasterizePolicy(const ScopedTtfRasterizePolicy&) = delete;
  ScopedTtfRasterizePolicy& operator=(const ScopedTtfRasterizePolicy&) = delete;

 private:
  bool previous_ = true;
};

ReaderHeapTrace captureReaderHeapTrace() {
  ReaderHeapTrace trace{};
  trace.internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  trace.internalMaxAlloc = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  trace.psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  trace.psramMaxAlloc = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return trace;
}

const char* readerBufferLocation(const void* ptr) {
  if (!ptr) {
    return "NULL";
  }
#if CROSSPOINT_HAS_ESP_MEMORY_UTILS
  if (esp_ptr_external_ram(ptr)) {
    return "PSRAM";
  }
  if (esp_ptr_internal(ptr)) {
    return "INTERNAL";
  }
#endif
  return "UNKNOWN";
}

void logReaderLargeBufferAlloc(const char* tag, const void* ptr, const size_t size) {
  const ReaderHeapTrace trace = captureReaderHeapTrace();
  LOG_INF(
      "MEMD",
      "Large buffer alloc: tag=%s ptr=%p size=%u loc=%s internalFree=%lu internalMaxAlloc=%lu psramFree=%lu psramMaxAlloc=%lu",
      tag ? tag : "?",
      ptr,
      static_cast<unsigned>(size),
      readerBufferLocation(ptr),
      static_cast<unsigned long>(trace.internalFree),
      static_cast<unsigned long>(trace.internalMaxAlloc),
      static_cast<unsigned long>(trace.psramFree),
      static_cast<unsigned long>(trace.psramMaxAlloc)
  );
}

void logReaderHeapDelta(
    const char* phase,
    const ReaderHeapTrace& before,
    const ReaderHeapTrace& after,
    const unsigned long elapsedMs
) {
  LOG_INF(
      "MEMD",
      "Internal heap delta: phase=%s elapsed=%lums freeBefore=%lu freeAfter=%lu freeDelta=%ld maxBefore=%lu maxAfter=%lu maxDelta=%ld psramFreeBefore=%lu psramFreeAfter=%lu psramFreeDelta=%ld",
      phase ? phase : "?",
      elapsedMs,
      static_cast<unsigned long>(before.internalFree),
      static_cast<unsigned long>(after.internalFree),
      static_cast<long>(after.internalFree) - static_cast<long>(before.internalFree),
      static_cast<unsigned long>(before.internalMaxAlloc),
      static_cast<unsigned long>(after.internalMaxAlloc),
      static_cast<long>(after.internalMaxAlloc) - static_cast<long>(before.internalMaxAlloc),
      static_cast<unsigned long>(before.psramFree),
      static_cast<unsigned long>(after.psramFree),
      static_cast<long>(after.psramFree) - static_cast<long>(before.psramFree)
  );
}

void logReaderHeapCheckpoint(const char* phase) {
  const ReaderHeapTrace trace = captureReaderHeapTrace();
  LOG_INF(
      "MEMD",
      "Internal heap checkpoint: phase=%s internalFree=%lu internalMaxAlloc=%lu psramFree=%lu psramMaxAlloc=%lu",
      phase ? phase : "?",
      static_cast<unsigned long>(trace.internalFree),
      static_cast<unsigned long>(trace.internalMaxAlloc),
      static_cast<unsigned long>(trace.psramFree),
      static_cast<unsigned long>(trace.psramMaxAlloc)
  );
}
#endif
// pages per minute, first item is 1 to prevent division by zero if accessed
const std::vector<int> PAGE_TURN_LABELS = {1, 1, 3, 6, 12};

int clampPercent(int percent) {
  if (percent < 0) {
    return 0;
  }
  if (percent > 100) {
    return 100;
  }
  return percent;
}

int firstReadableSpineIndex(Epub* epub) {
  if (!epub || epub->getSpineItemsCount() <= 0) {
    return 0;
  }
  const int textSpineIndex = epub->getSpineIndexForTextReference();
  if (textSpineIndex >= 0 && textSpineIndex < epub->getSpineItemsCount()) {
    return textSpineIndex;
  }
  return epub->getSpineItemsCount() > 1 ? 1 : 0;
}

void prepareReaderContentBackground(
    GfxRenderer& renderer,
    const int orientedMarginTop,
    const int orientedMarginRight,
    const int orientedMarginBottom,
    const int orientedMarginLeft
) {
  renderer.setInvertDrawing(false);
  if (!SETTINGS.readerContentInvert) {
    return;
  }

  // Invert Reader Content means the entire reader page surface is black,
  // including margins and the status bar region.  The previous v24 build only
  // filled the text viewport, which left a white frame around all four edges.
  renderer.fillRect(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight(), true);
  (void)orientedMarginTop;
  (void)orientedMarginRight;
  (void)orientedMarginBottom;
  (void)orientedMarginLeft;
}

void beginReaderContentRender(GfxRenderer& renderer) {
  renderer.setInvertDrawing(SETTINGS.readerContentInvert != 0);
}

void endReaderContentRender(GfxRenderer& renderer) {
  renderer.setInvertDrawing(false);
}

#if CROSSPOINT_PAPERS3
struct ProgressiveElementInfo {
  size_t index = 0;
  int rowStart = 0;
  int rowEnd = 0;
  int rowCenter = 0;
  uint8_t stripe = 0;
};

bool progressiveRowsAscending(const GfxRenderer& renderer, const bool isForwardTurn) {
  // Foreground progressive rendering is a visual reveal effect, not the same as
  // the final page-turn waveform direction.  On Paper S3 portrait orientation,
  // lower physical rows correspond to the right side of the portrait reader
  // surface, while higher physical rows correspond to the left side.
  //
  // V32 request for vertical reading:
  //   previous page (swipe left)  : reveal from right to left
  //   next page     (swipe right) : reveal from left to right
  //
  // Therefore vertical previous turns use ascending physical rows, while
  // vertical next turns use descending physical rows.  Keep the previous
  // horizontal layout behaviour for now.
  if (SETTINGS.readingLayout == CrossPointSettings::VERTICAL_LAYOUT) {
    const bool portraitLike =
        renderer.getOrientation() == GfxRenderer::Orientation::Portrait ||
        renderer.getOrientation() == GfxRenderer::Orientation::PortraitInverted;
    if (portraitLike) {
      return !isForwardTurn;
    }
  }

  const bool logicalRightToLeft =
      (SETTINGS.readingLayout == CrossPointSettings::VERTICAL_LAYOUT) ? isForwardTurn : !isForwardTurn;
  bool reversePhysicalBands = ReaderUtils::physicalBandReverseForLogicalRightToLeft(renderer);
  if (!logicalRightToLeft) {
    reversePhysicalBands = !reversePhysicalBands;
  }
  return !reversePhysicalBands;
}

uint8_t stripeForPhysicalRow(const int row) {
  const int clamped = std::max(0, std::min<int>(HalDisplay::DISPLAY_HEIGHT - 1, row));
  int stripe = (clamped * foregroundProgressiveStripeCount) / HalDisplay::DISPLAY_HEIGHT;
  if (stripe < 0) stripe = 0;
  if (stripe >= foregroundProgressiveStripeCount) stripe = foregroundProgressiveStripeCount - 1;
  return static_cast<uint8_t>(stripe);
}

void physicalRowsForStripe(const uint8_t stripe, int& rowStart, int& rowEnd) {
  rowStart = (HalDisplay::DISPLAY_HEIGHT * stripe) / foregroundProgressiveStripeCount;
  rowEnd = (HalDisplay::DISPLAY_HEIGHT * (stripe + 1)) / foregroundProgressiveStripeCount - 1;
  rowStart = std::max(0, std::min<int>(HalDisplay::DISPLAY_HEIGHT - 1, rowStart));
  rowEnd = std::max(0, std::min<int>(HalDisplay::DISPLAY_HEIGHT - 1, rowEnd));
}

void logProgressiveInternalHeap(const char* phase) {
  const uint32_t freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const uint32_t minInternal = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const uint32_t maxAllocInternal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  LOG_DBG(
      "PRG",
      "internal heap %s: Free=%lu Min=%lu MaxAlloc=%lu",
      phase ? phase : "?",
      static_cast<unsigned long>(freeInternal),
      static_cast<unsigned long>(minInternal),
      static_cast<unsigned long>(maxAllocInternal)
  );
}

ProgressiveElementInfo makeProgressiveElementInfo(
    const GfxRenderer& renderer,
    const PageElement& element,
    const int fontId,
    const int xOffset,
    const int yOffset
) {
  const int lineHeight = std::max(1, renderer.getLineHeight(fontId));
  int logicalX = element.xPos + xOffset;
  int logicalY = element.yPos + yOffset;
  int logicalW = lineHeight + SETTINGS.getReaderCharacterSpacing() + 8;
  int logicalH = lineHeight + 8;

  if (element.getTag() == TAG_PageLine) {
    const auto& line = static_cast<const PageLine&>(element);
    const auto& block = line.getBlock();
    if (block && block->isVertical()) {
      logicalY = 0;
      logicalH = renderer.getScreenHeight();
      logicalW = std::max(logicalW, lineHeight + 12);
    } else {
      logicalX = 0;
      logicalW = renderer.getScreenWidth();
      logicalH = std::max(logicalH, lineHeight + 8);
    }
  } else if (element.getTag() == TAG_PageImage) {
    const auto& image = static_cast<const PageImage&>(element);
    logicalW = std::max(1, static_cast<int>(image.getImageBlock().getWidth()));
    logicalH = std::max(1, static_cast<int>(image.getImageBlock().getHeight()));
  }

  int rowStart = 0;
  int rowEnd = 0;
  renderer.logicalRectToPhysicalRows(logicalX, logicalY, logicalW, logicalH, &rowStart, &rowEnd);
  if (rowStart > rowEnd) std::swap(rowStart, rowEnd);
  // Expand slightly so anti-aliased glyph edges and punctuation offsets do not
  // get left behind outside the refreshed physical row range.
  rowStart = std::max(0, rowStart - 4);
  rowEnd = std::min<int>(HalDisplay::DISPLAY_HEIGHT - 1, rowEnd + 4);

  ProgressiveElementInfo info{};
  info.rowStart = rowStart;
  info.rowEnd = rowEnd;
  info.rowCenter = (rowStart + rowEnd) / 2;
  info.stripe = stripeForPhysicalRow(info.rowCenter);
  return info;
}
#endif

}  // namespace

void EpubReaderActivity::onEnter() {
  Activity::onEnter();

  if (!epub) {
    return;
  }

  // Configure screen orientation based on settings
  // NOTE: This affects layout math and must be applied before any render calls.
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
  mappedInput.setTouchOrientation(SETTINGS.orientation);

  epub->setupCacheDir();

  bool isFirstOpen = true;
  FsFile f;
  if (Storage.openFileForRead("ERS", epub->getCachePath() + "/progress.bin", f)) {
    isFirstOpen = false;
    uint8_t data[6];
    int dataSize = f.read(data, 6);
    if (dataSize == 4 || dataSize == 6) {
      currentSpineIndex = data[0] + (data[1] << 8);
      nextPageNumber = data[2] + (data[3] << 8);
      // UINT16_MAX is an in-memory sentinel used when moving to the previous
      // chapter's last page.  It should never be treated as a persisted page
      // number; if a stale/corrupt progress file contains it, reset to a safe
      // first-page fallback instead of jumping to the end of the chapter.
      if (nextPageNumber == UINT16_MAX) {
        LOG_DBG("ERS", "Ignoring invalid saved progress sentinel for spine %d", currentSpineIndex);
        nextPageNumber = 0;
      }
      cachedSpineIndex = currentSpineIndex;
      LOG_DBG("ERS", "Loaded cache: %d, %d", currentSpineIndex, nextPageNumber);
    }
    if (dataSize == 6) {
      cachedChapterTotalPageCount = data[4] + (data[5] << 8);
    }
  }
  const int spineCount = epub->getSpineItemsCount();
  if (isFirstOpen) {
    currentSpineIndex = firstReadableSpineIndex(epub.get());
    if (currentSpineIndex > 0) {
      LOG_DBG("ERS", "First open: navigating to first readable spine %d", currentSpineIndex);
    }
  } else if (spineCount <= 0 || currentSpineIndex < 0 || currentSpineIndex >= spineCount) {
    const int fallbackSpine = firstReadableSpineIndex(epub.get());
    LOG_DBG(
        "ERS",
        "Ignoring invalid saved spine index: %d (spineCount=%d), resetting to spine %d page 0",
        currentSpineIndex,
        spineCount,
        fallbackSpine
    );
    currentSpineIndex = fallbackSpine;
    nextPageNumber = 0;
    cachedSpineIndex = currentSpineIndex;
    cachedChapterTotalPageCount = 0;
  }

  // Save current epub as last opened epub and add to recent books
  APP_STATE.openEpubPath = epub->getPath();
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(epub->getPath(), epub->getTitle(), epub->getAuthor(), epub->getThumbBmpPath());

  // Trigger first update
#if CROSSPOINT_PAPERS3
  clearPageFrameCache(false);
  lastReaderInputAt = millis();
  lastVisibleDisplayIdleAt = lastReaderInputAt;
  lastPageFrameCacheWorkAt = 0;
#endif
  requestUpdate();
}

void EpubReaderActivity::onExit() {
  Activity::onExit();
#if CROSSPOINT_PAPERS3
  clearPageFrameCache(true);
#endif

  // Reset orientation back to portrait for the rest of the UI
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  mappedInput.setTouchOrientation(CrossPointSettings::PORTRAIT);

  APP_STATE.readerActivityLoadCount = 0;
  APP_STATE.saveToFile();
  section.reset();
#if CROSSPOINT_PAPERS3
  clearPageFrameCache(false);
#endif
  epub.reset();
}


#if CROSSPOINT_PAPERS3
bool EpubReaderActivity::ensurePageFrameCacheEntryBuffer(PageFrameCacheEntry& entry) {
  if (entry.buffer) {
    logReaderLargeBufferAlloc("frame-cache-slot-reuse", entry.buffer, GfxRenderer::getBufferSize());
    return true;
  }

  const size_t bufferSize = GfxRenderer::getBufferSize();
  const ReaderHeapTrace before = captureReaderHeapTrace();
  const unsigned long start = millis();
  entry.buffer = static_cast<uint8_t*>(heap_caps_malloc(bufferSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  const ReaderHeapTrace afterPsramAttempt = captureReaderHeapTrace();
  logReaderHeapDelta("frame-cache-slot-alloc-psram-attempt", before, afterPsramAttempt, millis() - start);
  logReaderLargeBufferAlloc("frame-cache-slot-psram-attempt", entry.buffer, bufferSize);

  if (!entry.buffer) {
    const ReaderHeapTrace fallbackBefore = captureReaderHeapTrace();
    const unsigned long fallbackStart = millis();
    entry.buffer = static_cast<uint8_t*>(std::malloc(bufferSize));
    const ReaderHeapTrace fallbackAfter = captureReaderHeapTrace();
    logReaderHeapDelta("frame-cache-slot-alloc-fallback-malloc", fallbackBefore, fallbackAfter, millis() - fallbackStart);
    logReaderLargeBufferAlloc("frame-cache-slot-fallback-malloc", entry.buffer, bufferSize);
  }
  if (!entry.buffer) {
    LOG_ERR("ERS", "Frame cache slot alloc failed (%u bytes)", static_cast<unsigned>(bufferSize));
    return false;
  }
  return true;
}

bool EpubReaderActivity::ensurePageFrameCacheAllocated() {
  for (auto& entry : pageFrameCache) {
    if (!ensurePageFrameCacheEntryBuffer(entry)) {
      clearPageFrameCache(true);
      return false;
    }
  }
  return true;
}

void EpubReaderActivity::invalidatePageFrameCacheEntry(PageFrameCacheEntry& entry, const bool freeBuffer, const char* reason) {
  const int oldSpine = entry.spineIndex;
  const int oldPage = entry.pageNumber;
  const bool wasValid = entry.valid;
  entry.valid = false;
  entry.spineIndex = -1;
  entry.pageNumber = -1;
  entry.width = 0;
  entry.height = 0;
  entry.hasImages = false;
  entry.footnotes.clear();
  if (freeBuffer && entry.buffer) {
    logReaderLargeBufferAlloc("frame-cache-slot-free", entry.buffer, GfxRenderer::getBufferSize());
    const ReaderHeapTrace beforeFree = captureReaderHeapTrace();
    const unsigned long freeStart = millis();
    std::free(entry.buffer);
    const ReaderHeapTrace afterFree = captureReaderHeapTrace();
    logReaderHeapDelta("frame-cache-slot-free", beforeFree, afterFree, millis() - freeStart);
    entry.buffer = nullptr;
  }
  if (wasValid) {
    LOG_DBG(
        "ERS",
        "Frame cache slot released: slot=%d spine=%d page=%d freeBuffer=%d reason=%s",
        static_cast<int>(&entry - pageFrameCache.data()),
        oldSpine,
        oldPage,
        freeBuffer ? 1 : 0,
        reason ? reason : "?"
    );
  }
}

void EpubReaderActivity::clearPageFrameCache(const bool freeBuffers) {
  for (auto& entry : pageFrameCache) {
    invalidatePageFrameCacheEntry(entry, freeBuffers, freeBuffers ? "clear-free" : "clear");
  }
  pageFrameCacheNextWrite = 0;
  abortPageFrameCacheWarmJob();
  clearPendingPageTurn();
}

EpubReaderActivity::ReaderMemorySnapshot EpubReaderActivity::getReaderMemorySnapshot() const {
  ReaderMemorySnapshot snapshot{};
  snapshot.internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  snapshot.internalMaxAlloc = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  snapshot.psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  snapshot.psramMaxAlloc = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return snapshot;
}

EpubReaderActivity::ReaderMemoryState EpubReaderActivity::classifyReaderMemory(
    const ReaderMemorySnapshot& snapshot
) const {
  if (snapshot.internalFree < readerMemEmergencyFreeThreshold ||
      snapshot.internalMaxAlloc < readerMemEmergencyMaxAllocThreshold) {
    return ReaderMemoryState::EMERGENCY;
  }
  if (snapshot.internalFree < readerMemWarningFreeThreshold ||
      snapshot.internalMaxAlloc < readerMemWarningMaxAllocThreshold) {
    return ReaderMemoryState::CRITICAL;
  }
  if (snapshot.internalFree < readerMemNormalFreeThreshold ||
      snapshot.internalMaxAlloc < readerMemNormalMaxAllocThreshold) {
    return ReaderMemoryState::WARNING;
  }
  return ReaderMemoryState::NORMAL;
}

const char* EpubReaderActivity::readerMemoryStateName(const ReaderMemoryState state) const {
  switch (state) {
    case ReaderMemoryState::NORMAL:
      return "NORMAL";
    case ReaderMemoryState::WARNING:
      return "WARNING";
    case ReaderMemoryState::CRITICAL:
      return "CRITICAL";
    case ReaderMemoryState::EMERGENCY:
      return "EMERGENCY";
  }
  return "UNKNOWN";
}

EpubReaderActivity::ReaderMemoryState EpubReaderActivity::updateReaderMemoryState(
    const char* context,
    const bool forceLog
) {
  const ReaderMemorySnapshot snapshot = getReaderMemorySnapshot();
  const ReaderMemoryState state = classifyReaderMemory(snapshot);
  const unsigned long now = millis();
  const bool changed = !readerMemoryStateInitialized || state != lastReaderMemoryState;
  const bool periodic = (now - lastReaderMemoryLogAt) >= readerMemoryLogIntervalMs;
  if (forceLog || changed || periodic) {
    LOG_INF(
        "MEM",
        "Reader memory state=%s context=%s internalFree=%lu internalMaxAlloc=%lu psramFree=%lu psramMaxAlloc=%lu",
        readerMemoryStateName(state),
        context ? context : "?",
        static_cast<unsigned long>(snapshot.internalFree),
        static_cast<unsigned long>(snapshot.internalMaxAlloc),
        static_cast<unsigned long>(snapshot.psramFree),
        static_cast<unsigned long>(snapshot.psramMaxAlloc)
    );
    lastReaderMemoryLogAt = now;
  }
  readerMemoryStateInitialized = true;
  lastReaderMemoryState = state;
  return state;
}

bool EpubReaderActivity::isFrameCacheTargetAllowedForMemoryState(
    const int pageNumber,
    const ReaderMemoryState state
) const {
  if (!section || pageNumber < 0 || pageNumber >= section->pageCount) {
    return false;
  }
  if (state == ReaderMemoryState::NORMAL) {
    return true;
  }
  if (state == ReaderMemoryState::CRITICAL || state == ReaderMemoryState::EMERGENCY) {
    return false;
  }

  // WARNING: never run ordinary background frame-cache warming.  Only a
  // same-section pending page-turn target may be considered, and the hard
  // start gate in warmPageFrameCacheIfIdle() can still force visible render.
  int pendingTarget = -1;
  if (pendingPageTurnActive && sameSectionPageTurnTarget(pendingPageTurnForward, pendingTarget)) {
    return pageNumber == pendingTarget;
  }
  return false;
}

bool EpubReaderActivity::readerMemoryAllowsSilentIndexing(const char* phase) {
  const ReaderMemoryState state = updateReaderMemoryState(phase ? phase : "silent-index");
  if (state == ReaderMemoryState::NORMAL) {
    return true;
  }
  const unsigned long now = millis();
  if ((now - lastReaderMemoryPauseLogAt) >= readerMemoryPauseLogIntervalMs) {
    const ReaderMemorySnapshot snapshot = getReaderMemorySnapshot();
    LOG_DBG(
        "ERS",
        "Background work paused: feature=%s state=%s internalFree=%lu internalMaxAlloc=%lu psramFree=%lu psramMaxAlloc=%lu",
        phase ? phase : "silent-index",
        readerMemoryStateName(state),
        static_cast<unsigned long>(snapshot.internalFree),
        static_cast<unsigned long>(snapshot.internalMaxAlloc),
        static_cast<unsigned long>(snapshot.psramFree),
        static_cast<unsigned long>(snapshot.psramMaxAlloc)
    );
    lastReaderMemoryPauseLogAt = now;
  }
  return false;
}

bool EpubReaderActivity::readerMemoryAllowsVisibleFrameStore(const char* phase) {
  const ReaderMemoryState state = updateReaderMemoryState(phase ? phase : "visible-frame-store");
  if (state == ReaderMemoryState::CRITICAL || state == ReaderMemoryState::EMERGENCY) {
    const ReaderMemorySnapshot snapshot = getReaderMemorySnapshot();
    LOG_DBG(
        "ERS",
        "Frame cache store skipped: feature=%s state=%s internalFree=%lu internalMaxAlloc=%lu psramFree=%lu psramMaxAlloc=%lu",
        phase ? phase : "visible-frame-store",
        readerMemoryStateName(state),
        static_cast<unsigned long>(snapshot.internalFree),
        static_cast<unsigned long>(snapshot.internalMaxAlloc),
        static_cast<unsigned long>(snapshot.psramFree),
        static_cast<unsigned long>(snapshot.psramMaxAlloc)
    );
    return false;
  }
  return true;
}

bool EpubReaderActivity::readerMemoryAllowsFrameCacheStart(
    const ReaderMemorySnapshot& snapshot,
    const bool pendingTurnTarget,
    const char* phase
) {
  if (snapshot.internalFree >= pageFrameCacheStartInternalFreeThreshold &&
      snapshot.internalMaxAlloc >= pageFrameCacheStartInternalMaxAllocThreshold) {
    return true;
  }

  const unsigned long now = millis();
  if ((now - lastPageFrameCacheLowMemorySkipLogAt) >= pageFrameCacheLowMemorySkipLogIntervalMs) {
    LOG_DBG(
        "ERS",
        "Frame cache warm skipped: reason=start-gate feature=%s pending=%d internalFree=%lu internalMaxAlloc=%lu requiredFree=%lu requiredMaxAlloc=%lu",
        phase ? phase : "frame-cache-start",
        pendingTurnTarget ? 1 : 0,
        static_cast<unsigned long>(snapshot.internalFree),
        static_cast<unsigned long>(snapshot.internalMaxAlloc),
        static_cast<unsigned long>(pageFrameCacheStartInternalFreeThreshold),
        static_cast<unsigned long>(pageFrameCacheStartInternalMaxAllocThreshold)
    );
    lastPageFrameCacheLowMemorySkipLogAt = now;
  }
  return false;
}

bool EpubReaderActivity::isPageFrameCacheLowMemoryCooldownActive(
    const int spineIndex,
    const int pageNumber
) const {
  if (pageFrameCacheLowMemoryCooldownSpine != spineIndex ||
      pageFrameCacheLowMemoryCooldownPage != pageNumber ||
      pageFrameCacheLowMemoryCooldownUntil == 0) {
    return false;
  }
  return static_cast<long>(pageFrameCacheLowMemoryCooldownUntil - millis()) > 0;
}

void EpubReaderActivity::markPageFrameCacheLowMemoryCooldown(
    const int spineIndex,
    const int pageNumber,
    const char* reason
) {
  pageFrameCacheLowMemoryCooldownSpine = spineIndex;
  pageFrameCacheLowMemoryCooldownPage = pageNumber;
  pageFrameCacheLowMemoryCooldownUntil = millis() + pageFrameCacheLowMemoryAbortCooldownMs;
  LOG_DBG(
      "ERS",
      "Frame cache low-memory cooldown armed: spine=%d page=%d duration=%lums reason=%s",
      spineIndex,
      pageNumber,
      static_cast<unsigned long>(pageFrameCacheLowMemoryAbortCooldownMs),
      reason ? reason : "low-memory"
  );
}

bool EpubReaderActivity::shouldSkipPageFrameCacheForCooldown(
    const int spineIndex,
    const int pageNumber
) {
  if (!isPageFrameCacheLowMemoryCooldownActive(spineIndex, pageNumber)) {
    return false;
  }
  const unsigned long now = millis();
  if ((now - lastPageFrameCacheLowMemoryCooldownLogAt) >= pageFrameCacheLowMemorySkipLogIntervalMs) {
    LOG_DBG(
        "ERS",
        "Frame cache warm skipped: reason=low-memory-cooldown spine=%d page=%d remaining=%ldms",
        spineIndex,
        pageNumber,
        static_cast<long>(pageFrameCacheLowMemoryCooldownUntil - now)
    );
    lastPageFrameCacheLowMemoryCooldownLogAt = now;
  }
  return true;
}

void EpubReaderActivity::prunePageFrameCacheForMemoryState(
    const ReaderMemoryState state,
    const char* reason
) {
  if (state != ReaderMemoryState::CRITICAL && state != ReaderMemoryState::EMERGENCY) {
    return;
  }
  const int cur = section ? section->currentPage : -1;
  const int next = (section && section->currentPage < section->pageCount - 1) ? section->currentPage + 1 : -1;
  const bool freeBuffers = state == ReaderMemoryState::EMERGENCY;
  for (auto& entry : pageFrameCache) {
    if (!entry.valid) {
      if (freeBuffers && entry.buffer) {
        invalidatePageFrameCacheEntry(entry, true, reason ? reason : "memory-emergency");
      }
      continue;
    }
    const bool keepCurrent = entry.spineIndex == currentSpineIndex && entry.pageNumber == cur;
    const bool keepNext = next >= 0 && entry.spineIndex == currentSpineIndex && entry.pageNumber == next;
    if (!keepCurrent && !keepNext) {
      invalidatePageFrameCacheEntry(entry, freeBuffers, reason ? reason : "memory-prune");
    }
  }
}

EpubReaderActivity::PageFrameCacheEntry* EpubReaderActivity::findPageFrameCacheEntry(
    const int spineIndex,
    const int pageNumber
) {
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  for (auto& entry : pageFrameCache) {
    if (entry.valid && entry.spineIndex == spineIndex && entry.pageNumber == pageNumber &&
        entry.width == width && entry.height == height && entry.buffer) {
      return &entry;
    }
  }
  return nullptr;
}

EpubReaderActivity::PageFrameCacheEntry* EpubReaderActivity::acquirePageFrameCacheEntry(
    const int spineIndex,
    const int pageNumber
) {
  if (auto* existing = findPageFrameCacheEntry(spineIndex, pageNumber)) {
    return existing;
  }

  PageFrameCacheEntry* selected = nullptr;
  for (auto& entry : pageFrameCache) {
    if (!entry.valid) {
      selected = &entry;
      break;
    }
  }

  if (!selected) {
    // Ring-buffer replacement.  No page framebuffer is shifted or moved; only
    // this slot's metadata is retargeted to the new page.  Prefer not to evict
    // the currently visible page if another slot can be used.
    for (int i = 0; i < PAGE_FRAME_CACHE_SLOT_COUNT; i++) {
      auto& candidate = pageFrameCache[pageFrameCacheNextWrite];
      pageFrameCacheNextWrite = (pageFrameCacheNextWrite + 1) % PAGE_FRAME_CACHE_SLOT_COUNT;
      if (!(candidate.spineIndex == currentSpineIndex && section && candidate.pageNumber == section->currentPage)) {
        selected = &candidate;
        break;
      }
    }
  }
  if (!selected) {
    selected = &pageFrameCache[pageFrameCacheNextWrite];
    pageFrameCacheNextWrite = (pageFrameCacheNextWrite + 1) % PAGE_FRAME_CACHE_SLOT_COUNT;
  }

  if (!ensurePageFrameCacheEntryBuffer(*selected)) {
    return nullptr;
  }
  selected->valid = false;
  selected->footnotes.clear();
  return selected;
}

bool EpubReaderActivity::copyCurrentFrameToPageFrameCache(
    const int spineIndex,
    const int pageNumber,
    const std::vector<FootnoteEntry>& footnotes,
    const bool hasImages
) {
  if (!readerMemoryAllowsVisibleFrameStore("frame-cache-store")) {
    return false;
  }

  const ReaderHeapTrace acquireBefore = captureReaderHeapTrace();
  const unsigned long acquireStart = millis();
  auto* entry = acquirePageFrameCacheEntry(spineIndex, pageNumber);
  const ReaderHeapTrace acquireAfter = captureReaderHeapTrace();
  logReaderHeapDelta("cache-store-acquire-slot", acquireBefore, acquireAfter, millis() - acquireStart);
  if (!entry || !entry->buffer) {
    return false;
  }

  logReaderLargeBufferAlloc("frame-cache-store-destination", entry->buffer, GfxRenderer::getBufferSize());
  const ReaderHeapTrace copyBefore = captureReaderHeapTrace();
  const unsigned long copyStart = millis();
  std::memcpy(entry->buffer, renderer.getFrameBuffer(), GfxRenderer::getBufferSize());
  const ReaderHeapTrace copyAfter = captureReaderHeapTrace();
  logReaderHeapDelta("cache-store-memcpy", copyBefore, copyAfter, millis() - copyStart);
  entry->valid = true;
  entry->spineIndex = spineIndex;
  entry->pageNumber = pageNumber;
  entry->width = renderer.getScreenWidth();
  entry->height = renderer.getScreenHeight();
  entry->hasImages = hasImages;
  const ReaderHeapTrace footnotesBefore = captureReaderHeapTrace();
  const unsigned long footnotesStart = millis();
  entry->footnotes = footnotes;
  const ReaderHeapTrace footnotesAfter = captureReaderHeapTrace();
  logReaderHeapDelta("cache-store-footnotes-copy", footnotesBefore, footnotesAfter, millis() - footnotesStart);
  LOG_DBG("ERS", "Frame cache stored: slot=%d spine=%d page=%d", static_cast<int>(entry - pageFrameCache.data()), spineIndex, pageNumber);
  return true;
}

bool EpubReaderActivity::restorePageFrameCacheToRenderer(
    const int spineIndex,
    const int pageNumber,
    const bool restoreFootnotes
) {
  auto* entry = findPageFrameCacheEntry(spineIndex, pageNumber);
  if (!entry || !entry->buffer) {
    return false;
  }
  std::memcpy(renderer.getFrameBuffer(), entry->buffer, GfxRenderer::getBufferSize());
  restoredPageFrameHadImages = entry->hasImages;
  if (restoreFootnotes) {
    currentPageFootnotes = entry->footnotes;
  }
  renderer.beginFrame();
  LOG_DBG("ERS", "Frame cache hit: slot=%d spine=%d page=%d", static_cast<int>(entry - pageFrameCache.data()), spineIndex, pageNumber);
  return true;
}

bool EpubReaderActivity::hasReaderInputPending() const {
  return mappedInput.wasAnyPressed() || mappedInput.wasAnyReleased() || mappedInput.wasTapped();
}

bool EpubReaderActivity::capturePageTurnInput(bool& isForwardTurn) const {
  const bool reverseHorizontalZones =
      SETTINGS.readingLayout ==
      CrossPointSettings::VERTICAL_LAYOUT;

  auto [prevTriggered, nextTriggered] =
      ReaderUtils::detectPageTurn(
          mappedInput,
          reverseHorizontalZones
      );

  if (prevTriggered == nextTriggered) {
    return false;
  }

  isForwardTurn = nextTriggered;
  return true;
}

bool EpubReaderActivity::queuePendingPageTurn(const bool isForwardTurn, const char* source) {
  lastReaderInputAt = millis();
  if (pendingPageTurnActive) {
    LOG_DBG(
        "ERS",
        "Page turn input ignored: pending already active old=%s new=%s source=%s",
        pendingPageTurnForward ? "next" : "prev",
        isForwardTurn ? "next" : "prev",
        source ? source : "?"
    );
    return false;
  }

  pendingPageTurnActive = true;
  pendingPageTurnForward = isForwardTurn;
  pendingPageTurnAt = millis();
  pendingPageTurnForceVisible = false;
  pendingPageTurnForceVisibleAt = 0;
  LOG_DBG(
      "ERS",
      "Page turn queued: dir=%s source=%s curSpine=%d curPage=%d",
      pendingPageTurnForward ? "next" : "prev",
      source ? source : "?",
      currentSpineIndex,
      section ? section->currentPage : -1
  );
  return true;
}

void EpubReaderActivity::clearPendingPageTurn() {
  pendingPageTurnActive = false;
  pendingPageTurnForward = true;
  pendingPageTurnAt = 0;
  pendingPageTurnForceVisible = false;
  pendingPageTurnForceVisibleAt = 0;
}

void EpubReaderActivity::waitForVisibleDisplayIdle(const char* source) {
  const auto idleStart = millis();
  renderer.waitDisplayIdle();
  lastVisibleDisplayIdleAt = millis();
  lastReaderInputAt = lastVisibleDisplayIdleAt;
  LOG_DBG(
      "ERS",
      "Visible display idle: source=%s wait=%lums",
      source ? source : "?",
      lastVisibleDisplayIdleAt - idleStart
  );
}

bool EpubReaderActivity::executePendingPageTurnIfReady(const char* source) {
  if (!pendingPageTurnActive) {
    return false;
  }

  if (!section || RenderLock::peek()) {
    return false;
  }

  // A queued turn is a single, explicit user command.  Once its own target
  // page frame is ready, execute it immediately; do not wait for both adjacent
  // pages.  This keeps the pending command responsive while still preventing
  // stacked/swallowed swipes from skipping pages.
  if (!pageFrameCacheReadyForTurn(pendingPageTurnForward)) {
    return false;
  }

  const bool isForwardTurn = pendingPageTurnForward;
  const unsigned long queuedFor = millis() - pendingPageTurnAt;
  clearPendingPageTurn();
  mappedInput.clearState();
  LOG_DBG(
      "ERS",
      "Executing queued page turn: dir=%s source=%s queuedFor=%lums",
      isForwardTurn ? "next" : "prev",
      source ? source : "?",
      queuedFor
  );
  pageTurn(isForwardTurn);
  return true;
}

bool EpubReaderActivity::sameSectionPageTurnTarget(const bool isForwardTurn, int& targetPage) const {
  if (!section || section->pageCount <= 0) {
    return false;
  }

  if (isForwardTurn) {
    if (section->currentPage < section->pageCount - 1) {
      targetPage = section->currentPage + 1;
      return true;
    }
    return false;
  }

  if (section->currentPage > 0) {
    targetPage = section->currentPage - 1;
    return true;
  }
  return false;
}

bool EpubReaderActivity::pageFrameCacheReadyForTurn(const bool isForwardTurn) {
  if (pendingPageTurnForceVisible) {
    LOG_DBG(
        "ERS",
        "Page turn cache gate bypassed after low-memory warm abort: dir=%s queuedFor=%lums",
        isForwardTurn ? "next" : "prev",
        static_cast<unsigned long>(millis() - pendingPageTurnForceVisibleAt)
    );
    return true;
  }

  const ReaderMemoryState state = updateReaderMemoryState("page-turn-cache-gate");
  if (state == ReaderMemoryState::CRITICAL || state == ReaderMemoryState::EMERGENCY) {
    prunePageFrameCacheForMemoryState(state, "page-turn-cache-gate");
    return true;
  }

  if (pageFrameCacheWarmJob.active) {
    return false;
  }

  int targetPage = -1;
  if (!sameSectionPageTurnTarget(isForwardTurn, targetPage)) {
    // Chapter boundary turns can still need a section load/index.  Do not block
    // them forever here; the normal render path will handle the cross-spine page.
    return true;
  }

  return findPageFrameCacheEntry(currentSpineIndex, targetPage) != nullptr;
}

bool EpubReaderActivity::adjacentPageFrameCachesReady() {
  const ReaderMemoryState state = updateReaderMemoryState("adjacent-cache-gate");
  if (state != ReaderMemoryState::NORMAL) {
    return true;
  }

  if (!section || section->pageCount <= 0 || pageFrameCacheWarmJob.active) {
    return false;
  }

  int targetPage = -1;
  if (sameSectionPageTurnTarget(true, targetPage) &&
      !findPageFrameCacheEntry(currentSpineIndex, targetPage)) {
    return false;
  }

  if (sameSectionPageTurnTarget(false, targetPage) &&
      !findPageFrameCacheEntry(currentSpineIndex, targetPage)) {
    return false;
  }

  return true;
}

void EpubReaderActivity::abortPageFrameCacheWarmJob() {
  renderer.setInvertDrawing(false);
  if (!pageFrameCacheWarmJob.active && !pageFrameCacheWarmJob.page) {
    return;
  }
  LOG_DBG(
      "ERS",
      "Frame cache job aborted: spine=%d page=%d nextElement=%u",
      pageFrameCacheWarmJob.spineIndex,
      pageFrameCacheWarmJob.pageNumber,
      static_cast<unsigned>(pageFrameCacheWarmJob.nextElementIndex)
  );
  const ReaderHeapTrace abortBefore = captureReaderHeapTrace();
  const unsigned long abortStart = millis();
  pageFrameCacheWarmJob.page.reset();
  pageFrameCacheWarmJob.footnotes.clear();
  pageFrameCacheWarmJob = PageFrameCacheWarmJob{};
  const ReaderHeapTrace abortAfter = captureReaderHeapTrace();
  logReaderHeapDelta("frame-cache-job-abort", abortBefore, abortAfter, millis() - abortStart);
}

bool EpubReaderActivity::startPageFrameCacheWarmJob(
    const int pageNumber,
    const int orientedMarginTop,
    const int orientedMarginRight,
    const int orientedMarginBottom,
    const int orientedMarginLeft
) {
  if (!section || pageNumber < 0 || pageNumber >= section->pageCount) {
    return false;
  }
  if (findPageFrameCacheEntry(currentSpineIndex, pageNumber)) {
    return true;
  }
  logReaderHeapCheckpoint("frame-cache-warm-start");
  const int visiblePage = section->currentPage;
  auto savedFootnotes = currentPageFootnotes;
  const ReaderHeapTrace pageLoadBefore = captureReaderHeapTrace();
  const unsigned long pageLoadStart = millis();
  section->currentPage = pageNumber;
  auto page = section->loadPageFromSectionFile();
  section->currentPage = visiblePage;
  currentPageFootnotes = std::move(savedFootnotes);
  const ReaderHeapTrace pageLoadAfter = captureReaderHeapTrace();
  logReaderHeapDelta("frame-cache-page-object-load", pageLoadBefore, pageLoadAfter, millis() - pageLoadStart);

  if (!page) {
    return false;
  }

  const ReaderHeapTrace jobSetupBefore = captureReaderHeapTrace();
  const unsigned long jobSetupStart = millis();
  pageFrameCacheWarmJob = PageFrameCacheWarmJob{};
  pageFrameCacheWarmJob.active = true;
  pageFrameCacheWarmJob.spineIndex = currentSpineIndex;
  pageFrameCacheWarmJob.pageNumber = pageNumber;
  pageFrameCacheWarmJob.visiblePageNumber = visiblePage;
  pageFrameCacheWarmJob.orientedMarginTop = orientedMarginTop;
  pageFrameCacheWarmJob.orientedMarginRight = orientedMarginRight;
  pageFrameCacheWarmJob.orientedMarginBottom = orientedMarginBottom;
  pageFrameCacheWarmJob.orientedMarginLeft = orientedMarginLeft;
  pageFrameCacheWarmJob.startedAt = millis();
  pageFrameCacheWarmJob.lastChunkAt = pageFrameCacheWarmJob.startedAt;
  pageFrameCacheWarmJob.footnotes = page->footnotes;
  pageFrameCacheWarmJob.hasImages = page->hasImages();
  pageFrameCacheWarmJob.page = std::move(page);
  const ReaderHeapTrace jobSetupAfter = captureReaderHeapTrace();
  logReaderHeapDelta("frame-cache-job-object-setup", jobSetupBefore, jobSetupAfter, millis() - jobSetupStart);

  renderer.clearScreen();
  prepareReaderContentBackground(renderer, orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
  beginReaderContentRender(renderer);
  if (ReaderUtils::shouldUseTextAntiAliasingForReader()) {
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_DIRECT);
  }

  LOG_DBG(
      "ERS",
      "Frame cache job start: spine=%d page=%d elements=%u",
      pageFrameCacheWarmJob.spineIndex,
      pageFrameCacheWarmJob.pageNumber,
      static_cast<unsigned>(pageFrameCacheWarmJob.page->elements.size())
  );
  return true;
}

bool EpubReaderActivity::continuePageFrameCacheWarmJobChunk() {
  if (!pageFrameCacheWarmJob.active || !pageFrameCacheWarmJob.page) {
    return false;
  }

  logReaderHeapCheckpoint("frame-cache-job-before-state-check");
  const ReaderMemoryState state = updateReaderMemoryState("frame-cache-job");
  if (!isFrameCacheTargetAllowedForMemoryState(pageFrameCacheWarmJob.pageNumber, state)) {
    LOG_DBG(
        "ERS",
        "Frame cache job paused by memory state: page=%d state=%s",
        pageFrameCacheWarmJob.pageNumber,
        readerMemoryStateName(state)
    );
    int pendingTargetPage = -1;
    if (pendingPageTurnActive && sameSectionPageTurnTarget(pendingPageTurnForward, pendingTargetPage) &&
        pendingTargetPage == pageFrameCacheWarmJob.pageNumber) {
      pendingPageTurnForceVisible = true;
      pendingPageTurnForceVisibleAt = millis();
      LOG_DBG(
          "ERS",
          "Pending page turn will use visible render after low-memory cache abort: target=%d state=%s",
          pendingTargetPage,
          readerMemoryStateName(state)
      );
    }
    markPageFrameCacheLowMemoryCooldown(
        pageFrameCacheWarmJob.spineIndex,
        pageFrameCacheWarmJob.pageNumber,
        readerMemoryStateName(state)
    );
    prunePageFrameCacheForMemoryState(state, "frame-cache-job");
    abortPageFrameCacheWarmJob();
    return false;
  }

  if (hasReaderInputPending()) {
    // Background page analysis/cache owns the reader until it finishes.
    // Keep exactly one page-turn command pending; all later page-turn inputs are
    // consumed until that pending command is actually executed.
    bool pendingForward = true;
    if (capturePageTurnInput(pendingForward)) {
      queuePendingPageTurn(pendingForward, "cache-job");
    } else {
      lastReaderInputAt = millis();
      LOG_DBG("ERS", "Reader input event ignored while frame cache job is active");
    }
    mappedInput.clearState();
  }
  int pendingTargetPage = -1;
  if (pendingPageTurnActive && sameSectionPageTurnTarget(pendingPageTurnForward, pendingTargetPage) &&
      pendingTargetPage != pageFrameCacheWarmJob.pageNumber) {
    LOG_DBG(
        "ERS",
        "Frame cache job retargeted for pending turn: oldPage=%d targetPage=%d dir=%s",
        pageFrameCacheWarmJob.pageNumber,
        pendingTargetPage,
        pendingPageTurnForward ? "next" : "prev"
    );
    abortPageFrameCacheWarmJob();
    return false;
  }

  if (!section || pageFrameCacheWarmJob.spineIndex != currentSpineIndex) {
    abortPageFrameCacheWarmJob();
    return false;
  }
  if (findPageFrameCacheEntry(pageFrameCacheWarmJob.spineIndex, pageFrameCacheWarmJob.pageNumber)) {
    abortPageFrameCacheWarmJob();
    return true;
  }

  auto* page = pageFrameCacheWarmJob.page.get();
  const size_t totalElements = page->elements.size();
  const size_t elementsPerChunk = std::max<size_t>(
      1,
      (totalElements + pageFrameCacheCooperativeChunks - 1) / pageFrameCacheCooperativeChunks
  );
  const size_t startElement = pageFrameCacheWarmJob.nextElementIndex;
  const unsigned long chunkStart = millis();
  size_t renderedElements = 0;

  while (pageFrameCacheWarmJob.nextElementIndex < totalElements) {
    const size_t elementIndex = pageFrameCacheWarmJob.nextElementIndex;
    const PageElementTag elementTag = page->elements[elementIndex]->getTag();
    const ReaderHeapTrace glyphBefore = captureReaderHeapTrace();
    const unsigned long glyphStart = millis();
    {
      ScopedTtfRasterizePolicy ttfPolicy(false, "frame-cache-background");
      page->elements[elementIndex]->render(
          renderer,
          SETTINGS.getReaderFontId(),
          pageFrameCacheWarmJob.orientedMarginLeft,
          pageFrameCacheWarmJob.orientedMarginTop
      );
    }
    const bool ttfMissSuppressed = ExternalFont::consumeRuntimeTtfMissSuppressed();
    const ReaderHeapTrace glyphAfter = captureReaderHeapTrace();
    char phaseName[80];
    std::snprintf(phaseName, sizeof(phaseName), "frame-cache-glyph-render[%u tag=%u]", static_cast<unsigned>(elementIndex), static_cast<unsigned>(elementTag));
    logReaderHeapDelta(phaseName, glyphBefore, glyphAfter, millis() - glyphStart);
    if (ttfMissSuppressed) {
      int pendingTargetPage = -1;
      if (pendingPageTurnActive && sameSectionPageTurnTarget(pendingPageTurnForward, pendingTargetPage) &&
          pendingTargetPage == pageFrameCacheWarmJob.pageNumber) {
        pendingPageTurnForceVisible = true;
        pendingPageTurnForceVisibleAt = millis();
        LOG_DBG(
            "ERS",
            "Pending page turn will use visible render after background TTF miss suppression: target=%d",
            pendingTargetPage
        );
      }
      markPageFrameCacheLowMemoryCooldown(
          pageFrameCacheWarmJob.spineIndex,
          pageFrameCacheWarmJob.pageNumber,
          "ttf-miss-suppressed"
      );
      LOG_DBG(
          "ERS",
          "Frame cache job aborted: reason=ttf-miss-suppressed spine=%d page=%d element=%u dirty=1 store=0",
          pageFrameCacheWarmJob.spineIndex,
          pageFrameCacheWarmJob.pageNumber,
          static_cast<unsigned>(elementIndex)
      );
      renderer.setRenderMode(GfxRenderer::BW);
      endReaderContentRender(renderer);
      abortPageFrameCacheWarmJob();
      if (section) {
        restorePageFrameCacheToRenderer(currentSpineIndex, section->currentPage, false);
      }
      return false;
    }
    pageFrameCacheWarmJob.nextElementIndex++;
    renderedElements++;

    if (renderedElements >= elementsPerChunk) {
      break;
    }
    if ((millis() - chunkStart) >= pageFrameCacheChunkBudgetMs) {
      break;
    }
  }

  pageFrameCacheWarmJob.lastChunkAt = millis();
  LOG_DBG(
      "ERS",
      "Frame cache chunk: page=%d elements=%u..%u/%u time=%lums",
      pageFrameCacheWarmJob.pageNumber,
      static_cast<unsigned>(startElement),
      static_cast<unsigned>(pageFrameCacheWarmJob.nextElementIndex),
      static_cast<unsigned>(totalElements),
      pageFrameCacheWarmJob.lastChunkAt - chunkStart
  );

  if (pageFrameCacheWarmJob.nextElementIndex < totalElements) {
    return false;
  }

  renderer.setRenderMode(GfxRenderer::BW);
  endReaderContentRender(renderer);

  const int visiblePage = section->currentPage;
  auto savedFootnotes = currentPageFootnotes;
  section->currentPage = pageFrameCacheWarmJob.pageNumber;
  renderStatusBar();
  const ReaderHeapTrace cacheStoreBefore = captureReaderHeapTrace();
  const unsigned long cacheStoreStart = millis();
  const bool stored = copyCurrentFrameToPageFrameCache(
      pageFrameCacheWarmJob.spineIndex,
      pageFrameCacheWarmJob.pageNumber,
      pageFrameCacheWarmJob.footnotes,
      pageFrameCacheWarmJob.hasImages
  );
  const ReaderHeapTrace cacheStoreAfter = captureReaderHeapTrace();
  logReaderHeapDelta("frame-cache-cache-store-total", cacheStoreBefore, cacheStoreAfter, millis() - cacheStoreStart);
  section->currentPage = visiblePage;
  currentPageFootnotes = std::move(savedFootnotes);

  const int finishedPage = pageFrameCacheWarmJob.pageNumber;
  const unsigned long totalMs = millis() - pageFrameCacheWarmJob.startedAt;
  pageFrameCacheWarmJob.page.reset();
  pageFrameCacheWarmJob.footnotes.clear();
  pageFrameCacheWarmJob = PageFrameCacheWarmJob{};
  lastPageFrameCacheWorkAt = millis();

  // Restore the visible frame if it is cached.  This keeps the renderer buffer
  // sane after cooperative cache drawing, but it never drives the e-paper.
  if (section) {
    restorePageFrameCacheToRenderer(currentSpineIndex, section->currentPage, false);
  }

  LOG_DBG(
      "ERS",
      "Frame cache render cooperative: page=%d stored=%d total=%lums",
      finishedPage,
      stored ? 1 : 0,
      totalMs
  );
  return stored;
}

bool EpubReaderActivity::renderPageToFrameCache(
    const int pageNumber,
    const int orientedMarginTop,
    const int orientedMarginRight,
    const int orientedMarginBottom,
    const int orientedMarginLeft
) {
  if (!section || pageNumber < 0 || pageNumber >= section->pageCount) {
    return false;
  }
  if (findPageFrameCacheEntry(currentSpineIndex, pageNumber)) {
    return true;
  }
  const int visiblePage = section->currentPage;
  auto savedFootnotes = currentPageFootnotes;
  const ReaderHeapTrace pageLoadBefore = captureReaderHeapTrace();
  const unsigned long pageLoadStart = millis();
  section->currentPage = pageNumber;
  auto page = section->loadPageFromSectionFile();
  const ReaderHeapTrace pageLoadAfter = captureReaderHeapTrace();
  logReaderHeapDelta("frame-cache-direct-page-object-load", pageLoadBefore, pageLoadAfter, millis() - pageLoadStart);
  if (!page) {
    section->currentPage = visiblePage;
    currentPageFootnotes = std::move(savedFootnotes);
    return false;
  }

  const auto start = millis();
  auto footnotes = page->footnotes;
  renderer.clearScreen();
  prepareReaderContentBackground(renderer, orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
  beginReaderContentRender(renderer);

  if (ReaderUtils::shouldUseTextAntiAliasingForReader()) {
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_DIRECT);
  }

  {
    ScopedTtfRasterizePolicy ttfPolicy(false, "frame-cache-direct");
    const ReaderHeapTrace glyphBefore = captureReaderHeapTrace();
    const unsigned long glyphStart = millis();
    page->render(
        renderer,
        SETTINGS.getReaderFontId(),
        orientedMarginLeft,
        orientedMarginTop
    );
    const ReaderHeapTrace glyphAfter = captureReaderHeapTrace();
    logReaderHeapDelta("frame-cache-direct-glyph-render", glyphBefore, glyphAfter, millis() - glyphStart);
    if (ExternalFont::consumeRuntimeTtfMissSuppressed()) {
      markPageFrameCacheLowMemoryCooldown(currentSpineIndex, pageNumber, "ttf-miss-suppressed");
      renderer.setRenderMode(GfxRenderer::BW);
      endReaderContentRender(renderer);
      section->currentPage = visiblePage;
      currentPageFootnotes = std::move(savedFootnotes);
      LOG_DBG("ERS", "Frame cache direct render aborted: reason=ttf-miss-suppressed page=%d dirty=1 store=0", pageNumber);
      if (section) {
        restorePageFrameCacheToRenderer(currentSpineIndex, section->currentPage, false);
      }
      return false;
    }
  }

  renderer.setRenderMode(GfxRenderer::BW);
  endReaderContentRender(renderer);
  renderStatusBar();
  const bool stored = copyCurrentFrameToPageFrameCache(currentSpineIndex, pageNumber, footnotes, page->hasImages());

  section->currentPage = visiblePage;
  currentPageFootnotes = std::move(savedFootnotes);
  LOG_DBG("ERS", "Frame cache render: page=%d stored=%d time=%lums", pageNumber, stored ? 1 : 0, millis() - start);
  (void)orientedMarginBottom;
  return stored;
}

bool EpubReaderActivity::collectPageTtfPrewarmCodepoints(
    const int pageNumber,
    std::vector<uint32_t>& out,
    const size_t maxCodepoints
) {
  out.clear();
  if (!section || pageNumber < 0 || pageNumber >= section->pageCount || maxCodepoints == 0) {
    return false;
  }

  const int visiblePage = section->currentPage;
  auto savedFootnotes = currentPageFootnotes;
  const ReaderHeapTrace pageLoadBefore = captureReaderHeapTrace();
  const unsigned long pageLoadStart = millis();
  section->currentPage = pageNumber;
  auto page = section->loadPageFromSectionFile();
  section->currentPage = visiblePage;
  currentPageFootnotes = std::move(savedFootnotes);
  const ReaderHeapTrace pageLoadAfter = captureReaderHeapTrace();
  logReaderHeapDelta("idle-glyph-prewarm-page-object-load", pageLoadBefore, pageLoadAfter, millis() - pageLoadStart);
  if (!page) {
    return false;
  }

  auto appendCodepoint = [&](const uint32_t cp) {
    if (cp == 0 || cp == REPLACEMENT_GLYPH || cp == 0x00AD || utf8IsCombiningMark(cp)) {
      return;
    }
    // Whitespace and ASCII control characters do not need TTF prewarming.
    if (cp <= 0x20 || cp == 0x00A0 || cp == 0x2002 || cp == 0x2003 || cp == 0x3000) {
      return;
    }
    if (std::find(out.begin(), out.end(), cp) != out.end()) {
      return;
    }
    out.push_back(cp);
  };

  for (const auto& element : page->elements) {
    if (!element || element->getTag() != TAG_PageLine) {
      continue;
    }
    const auto& line = static_cast<const PageLine&>(*element);
    const auto& block = line.getBlock();
    if (!block) {
      continue;
    }
    for (const std::string& word : block->getWords()) {
      const unsigned char* cursor = reinterpret_cast<const unsigned char*>(word.c_str());
      while (*cursor != 0) {
        const uint32_t cp = utf8NextCodepoint(&cursor);
        appendCodepoint(cp);
        if (out.size() >= maxCodepoints) {
          return true;
        }
      }
    }
  }
  return !out.empty();
}

bool EpubReaderActivity::idleGlyphPrewarmIfReady() {
  if (!section || section->pageCount <= 0 || pendingPageTurnActive || pageFrameCacheWarmJob.active) {
    return false;
  }
  if (hasReaderInputPending()) {
    return false;
  }

  ExternalFont* font = FontManager::getInstance().getActiveFont();
  if (font == nullptr || !font->isLoaded() || !font->isTtfFormat()) {
    return false;
  }

  const unsigned long now = millis();
  if ((now - lastReaderInputAt) < idleGlyphPrewarmDelayMs) {
    return false;
  }
  if (lastIdleGlyphPrewarmAt != 0 && (now - lastIdleGlyphPrewarmAt) < idleGlyphPrewarmCooldownMs) {
    return false;
  }
  if (idleGlyphPrewarmPausedUntil != 0 && now < idleGlyphPrewarmPausedUntil) {
    return false;
  }

  ReaderMemorySnapshot snapshot = getReaderMemorySnapshot();
  if (snapshot.internalFree < idleGlyphPrewarmInternalFreeThreshold ||
      snapshot.internalMaxAlloc < idleGlyphPrewarmInternalMaxAllocThreshold ||
      snapshot.psramFree < idleGlyphPrewarmPsramFreeThreshold) {
    return false;
  }

  const int cur = section->currentPage;
  const std::array<int, 2> candidates = {cur + 1, cur - 1};
  std::vector<uint32_t> codepoints;
  codepoints.reserve(96);

  for (const int pageNumber : candidates) {
    if (pageNumber < 0 || pageNumber >= section->pageCount) {
      continue;
    }
    if (findPageFrameCacheEntry(currentSpineIndex, pageNumber)) {
      continue;
    }

    // Loading the page object touches Section state, so keep the same render lock
    // discipline as frame-cache warming.  Each pass is intentionally small.
    RenderLock lock(*this);
    if (!collectPageTtfPrewarmCodepoints(pageNumber, codepoints, 128)) {
      continue;
    }

    uint8_t warmed = 0;
    uint8_t checked = 0;
    for (const uint32_t cp : codepoints) {
      if (font->isGlyphAvailableWithoutRasterize(cp)) {
        continue;
      }

      snapshot = getReaderMemorySnapshot();
      if (snapshot.internalFree < idleGlyphPrewarmInternalFreeThreshold ||
          snapshot.internalMaxAlloc < idleGlyphPrewarmInternalMaxAllocThreshold ||
          snapshot.psramFree < idleGlyphPrewarmPsramFreeThreshold) {
        break;
      }

      const ReaderHeapTrace glyphBefore = captureReaderHeapTrace();
      const unsigned long glyphStart = millis();
      const uint8_t* glyph = nullptr;
      {
        ScopedTtfRasterizePolicy ttfPolicy(true, "idle-glyph-prewarm");
        glyph = font->getGlyph(cp);
      }
      const bool unexpectedSuppressed = ExternalFont::consumeRuntimeTtfMissSuppressed();
      const ReaderHeapTrace glyphAfter = captureReaderHeapTrace();
      char phase[96];
      std::snprintf(phase, sizeof(phase), "idle-glyph-prewarm U+%04lx", static_cast<unsigned long>(cp));
      logReaderHeapDelta(phase, glyphBefore, glyphAfter, millis() - glyphStart);
      ++checked;
      if (glyph != nullptr) {
        ++warmed;
      }
      if (unexpectedSuppressed) {
        LOG_DBG("ERS", "Idle glyph prewarm saw unexpected TTF suppression: U+%04lx", static_cast<unsigned long>(cp));
      }

      const int32_t freeDelta = static_cast<int32_t>(glyphAfter.internalFree) - static_cast<int32_t>(glyphBefore.internalFree);
      const int32_t maxDelta = static_cast<int32_t>(glyphAfter.internalMaxAlloc) - static_cast<int32_t>(glyphBefore.internalMaxAlloc);
      if (freeDelta < idleGlyphPrewarmStopFreeDrop || maxDelta < idleGlyphPrewarmStopMaxAllocDrop) {
        idleGlyphPrewarmPausedUntil = millis() + 2000UL;
        LOG_DBG(
            "ERS",
            "Idle glyph prewarm paused: page=%d U+%04lx freeDelta=%ld maxDelta=%ld",
            pageNumber,
            static_cast<unsigned long>(cp),
            static_cast<long>(freeDelta),
            static_cast<long>(maxDelta)
        );
        break;
      }

      if (warmed >= idleGlyphPrewarmMaxGlyphsPerPass || checked >= idleGlyphPrewarmMaxGlyphsPerPass) {
        break;
      }
    }

    if (warmed > 0 || checked > 0) {
      font->flushPersistentCache();
      lastIdleGlyphPrewarmAt = millis();
      if (warmed > 0 && pageFrameCacheLowMemoryCooldownSpine == currentSpineIndex &&
          pageFrameCacheLowMemoryCooldownPage == pageNumber) {
        pageFrameCacheLowMemoryCooldownUntil = 0;
        lastPageFrameCacheLowMemorySkipLogAt = 0;
        LOG_DBG("ERS", "Frame cache cooldown cleared after idle glyph prewarm: spine=%d page=%d", currentSpineIndex, pageNumber);
      }
      LOG_DBG(
          "ERS",
          "Idle glyph prewarm: page=%d warmed=%u checked=%u candidates=%u",
          pageNumber,
          static_cast<unsigned>(warmed),
          static_cast<unsigned>(checked),
          static_cast<unsigned>(codepoints.size())
      );
      return true;
    }
  }

  return false;
}


void EpubReaderActivity::warmPageFrameCacheIfIdle() {
  if (!epub || !section || section->pageCount <= 0) {
    abortPageFrameCacheWarmJob();
    return;
  }

  if (RenderLock::peek()) {
    return;
  }

  if (pendingPageTurnForceVisible) {
    return;
  }

  const ReaderMemoryState state = updateReaderMemoryState("frame-cache-warm");
  prunePageFrameCacheForMemoryState(state, "frame-cache-warm");

  if (pageFrameCacheWarmJob.active) {
    if (!isFrameCacheTargetAllowedForMemoryState(pageFrameCacheWarmJob.pageNumber, state)) {
      markPageFrameCacheLowMemoryCooldown(
          pageFrameCacheWarmJob.spineIndex,
          pageFrameCacheWarmJob.pageNumber,
          readerMemoryStateName(state)
      );
      abortPageFrameCacheWarmJob();
      return;
    }
    RenderLock lock(*this);
    continuePageFrameCacheWarmJobChunk();
    return;
  }

  if (state == ReaderMemoryState::CRITICAL || state == ReaderMemoryState::EMERGENCY) {
    return;
  }

  const unsigned long now = millis();
  const bool pendingTurn = pendingPageTurnActive;
  const unsigned long idleFor = now - lastReaderInputAt;
  if (!pendingTurn && idleFor < pageFrameCacheIdleDelayMs) {
    return;
  }

  if (!pendingTurn && lastPageFrameCacheWorkAt != 0 &&
      (now - lastPageFrameCacheWorkAt) < pageFrameCacheWorkCooldownMs) {
    return;
  }

  // r14: before attempting a background frame cache, spend a tiny idle slice
  // prewarming missing TTF glyphs for adjacent pages.  This keeps r13a's rule
  // (background cache must abort on true TTF miss) while gradually making the
  // next/previous page eligible for a full cache render.
  if (!pendingTurn && idleGlyphPrewarmIfReady()) {
    return;
  }

  int top = 0;
  int right = 0;
  int bottom = 0;
  int left = 0;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  top += SETTINGS.screenMargin;
  left += SETTINGS.screenMargin;
  right += SETTINGS.screenMargin;

  const uint8_t statusBarHeight = UITheme::getInstance().getStatusBarHeight();
  if (SETTINGS.statusBarFollowsPageMargin) {
    bottom += SETTINGS.screenMargin + statusBarHeight;
  } else if (automaticPageTurnActive &&
             (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight())) {
    bottom += std::max(SETTINGS.screenMargin,
                       static_cast<uint8_t>(statusBarHeight + UITheme::getInstance().getMetrics().statusBarVerticalMargin));
  } else {
    bottom += std::max(SETTINGS.screenMargin, statusBarHeight);
  }

  const int cur = section->currentPage;
  int pendingTarget = -1;
  const bool hasSameSectionPendingTarget = pendingTurn && sameSectionPageTurnTarget(pendingPageTurnForward, pendingTarget);
  const int oppositeTarget = hasSameSectionPendingTarget ? (pendingPageTurnForward ? cur - 1 : cur + 1) : -1;
  std::array<int, 3> candidates = hasSameSectionPendingTarget
      ? std::array<int, 3>{pendingTarget, cur, oppositeTarget}
      : std::array<int, 3>{cur, cur + 1, cur - 1};
  if (state == ReaderMemoryState::WARNING) {
    // Warning state is not safe for ordinary background warming.  Only a
    // same-section pending page-turn target may be attempted, and the hard
    // start gate below can still force a visible render fallback.
    candidates = hasSameSectionPendingTarget
        ? std::array<int, 3>{pendingTarget, -1, -1}
        : std::array<int, 3>{-1, -1, -1};
  }
  if (pendingTurn) {
    LOG_DBG(
        "ERS",
        "Frame cache warm pending-priority: dir=%s cur=%d target=%d idleFor=%lums cooldownBypassed=1",
        pendingPageTurnForward ? "next" : "prev",
        cur,
        hasSameSectionPendingTarget ? pendingTarget : -1,
        idleFor
    );
  }
  for (const int pageNumber : candidates) {
    if (pageNumber < 0 || pageNumber >= section->pageCount) {
      continue;
    }
    if (findPageFrameCacheEntry(currentSpineIndex, pageNumber)) {
      continue;
    }
    if (!isFrameCacheTargetAllowedForMemoryState(pageNumber, state)) {
      continue;
    }
    if (shouldSkipPageFrameCacheForCooldown(currentSpineIndex, pageNumber)) {
      if (pendingTurn && hasSameSectionPendingTarget && pageNumber == pendingTarget) {
        pendingPageTurnForceVisible = true;
        pendingPageTurnForceVisibleAt = millis();
      }
      continue;
    }

    const bool pendingTargetCandidate = pendingTurn && hasSameSectionPendingTarget && pageNumber == pendingTarget;
    const ReaderMemorySnapshot startSnapshot = getReaderMemorySnapshot();
    if (!readerMemoryAllowsFrameCacheStart(startSnapshot, pendingTargetCandidate, "frame-cache-warm-start-gate")) {
      if (pendingTargetCandidate) {
        pendingPageTurnForceVisible = true;
        pendingPageTurnForceVisibleAt = millis();
        LOG_DBG(
            "ERS",
            "Pending page turn will use visible render after frame-cache start gate: target=%d internalFree=%lu internalMaxAlloc=%lu",
            pageNumber,
            static_cast<unsigned long>(startSnapshot.internalFree),
            static_cast<unsigned long>(startSnapshot.internalMaxAlloc)
        );
      }
      continue;
    }

    RenderLock lock(*this);
    logReaderHeapCheckpoint("frame-cache-warm-before-start-job");
    const ReaderHeapTrace warmStartBefore = captureReaderHeapTrace();
    const unsigned long warmStartMs = millis();
    const bool started = startPageFrameCacheWarmJob(pageNumber, top, right, bottom, left);
    const ReaderHeapTrace warmStartAfter = captureReaderHeapTrace();
    logReaderHeapDelta("frame-cache-warm-start-job-total", warmStartBefore, warmStartAfter, millis() - warmStartMs);
    if (started) {
      continuePageFrameCacheWarmJobChunk();
    }
    return;
  }
}
#endif

void EpubReaderActivity::openReaderSettings() {
#if CROSSPOINT_PAPERS3
  abortPageFrameCacheWarmJob();
#endif
  onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::READER_SETTINGS);
}

void EpubReaderActivity::openReaderMenu() {
#if CROSSPOINT_PAPERS3
  abortPageFrameCacheWarmJob();
#endif
  if (!epub) {
    return;
  }
  const int currentPage = section ? section->currentPage + 1 : 0;
  const int totalPages = section ? section->pageCount : 0;
  float bookProgress = 0.0f;
  if (epub->getBookSize() > 0 && section && section->pageCount > 0) {
    const float chapterProgress = static_cast<float>(section->currentPage) / static_cast<float>(section->pageCount);
    bookProgress = epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f;
  }
  const int bookProgressPercent = clampPercent(static_cast<int>(bookProgress + 0.5f));
  startActivityForResult(std::make_unique<EpubReaderMenuActivity>(
                             renderer, mappedInput, epub->getTitle(), currentPage, totalPages, bookProgressPercent,
                             SETTINGS.orientation, !currentPageFootnotes.empty()),
                         [this](const ActivityResult& result) {
                           // Always apply orientation change even if the menu was cancelled.
                           const auto& menu = std::get<MenuResult>(result.data);
                           applyOrientation(menu.orientation);
                           mappedInput.setTouchOrientation(menu.orientation);
                           toggleAutoPageTurn(menu.pageTurnOption);
                           if (!result.isCancelled) {
                             onReaderMenuConfirm(static_cast<EpubReaderMenuActivity::MenuAction>(menu.action));
                           }
                         });
}

void EpubReaderActivity::loop() {
  if (!epub) {
    // Should never happen
    finish();
    return;
  }

#if CROSSPOINT_PAPERS3
  // The visible renderer and the background frame-cache renderer both hold
  // RenderLock while they touch the shared Section/renderer state.  While it is
  // busy, keep exactly one page-turn command pending and consume any later
  // page-turn inputs until that command is executed.
  if (RenderLock::peek()) {
    if (mappedInput.wasAnyPressed() || mappedInput.wasAnyReleased() || mappedInput.wasTapped()) {
      bool pendingForward = true;
      if (capturePageTurnInput(pendingForward)) {
        queuePendingPageTurn(pendingForward, "render-busy");
      } else {
        lastReaderInputAt = millis();
        LOG_DBG("ERS", "Reader input event ignored while render/cache busy");
      }
      mappedInput.clearState();
    }
    return;
  }

  if (pendingPageTurnActive) {
    if (executePendingPageTurnIfReady("reader-loop")) {
      return;
    }
    if (mappedInput.wasAnyPressed() || mappedInput.wasAnyReleased() || mappedInput.wasTapped()) {
      bool ignoredForward = true;
      if (capturePageTurnInput(ignoredForward)) {
        queuePendingPageTurn(ignoredForward, "pending-active");
      } else {
        lastReaderInputAt = millis();
        LOG_DBG("ERS", "Reader input event ignored while page turn pending");
      }
      mappedInput.clearState();
    }
    warmPageFrameCacheIfIdle();
    return;
  }
#endif

  if (automaticPageTurnActive) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
        mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      automaticPageTurnActive = false;
      // updates chapter title space to indicate page turn disabled
      requestUpdate();
      return;
    }

    if (!section) {
      requestUpdate();
      return;
    }

    // Skips page turn if renderingMutex is busy
    if (RenderLock::peek()) {
      lastPageTurnTime = millis();
      return;
    }

    if ((millis() - lastPageTurnTime) >= pageTurnDuration) {
      pageTurn(true);
      return;
    }
  }

#if CROSSPOINT_PAPERS3
  // On the 2-finger lift frame, BTN_BACK is set in currentState while the zone
  // button transitions out of previousState, causing wasReleased(zone) to fire
  // simultaneously.  Skip all zone-based actions when BTN_BACK is active so the
  // Back handler processes correctly on the next frame (wasReleased).
  if (mappedInput.isPressed(MappedInputManager::Button::Back)) {
    return;
  }
#endif

#if CROSSPOINT_PAPERS3
  // Reader quick touch zones:
  // - middle upper area: open Settings > Reader directly
  // - middle lower area: open the reader page menu
  if (mappedInput.wasTapped()) {
    const int16_t tx = mappedInput.getTouchX();
    const int16_t ty = mappedInput.getTouchY();
    const int screenW = renderer.getScreenWidth();
    const int screenH = renderer.getScreenHeight();
    if (tx >= screenW / 3 && tx <= (screenW * 2) / 3) {
      if (ty < screenH / 2) {
        openReaderSettings();
      } else {
        openReaderMenu();
      }
      return;
    }
  }
#endif

  // Enter reader menu activity.
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openReaderMenu();
    return;
  }

  // Long press BACK (1s+) goes to file selection
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= ReaderUtils::GO_HOME_MS) {
    activityManager.goToFileBrowser(epub ? epub->getPath() : "");
    return;
  }

  // Short press BACK goes directly to home (or restores position if viewing footnote)
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() < ReaderUtils::GO_HOME_MS) {
    if (footnoteDepth > 0) {
      restoreSavedPosition();
      return;
    }
    onGoHome();
    return;
  }

  const bool reverseHorizontalZones =
      SETTINGS.readingLayout ==
      CrossPointSettings::VERTICAL_LAYOUT;

  auto [prevTriggered, nextTriggered] =
      ReaderUtils::detectPageTurn(
          mappedInput,
          reverseHorizontalZones
      );
      
  if (!prevTriggered && !nextTriggered) {
    if (pendingNextChapterPreindex &&
        (millis() - nextChapterPreindexAt) >= 3500UL &&
        !RenderLock::peek()) {
      pendingNextChapterPreindex = false;
      if (readerMemoryAllowsSilentIndexing("silent-index")) {
        RenderLock lock(*this);
        silentIndexNextChapterIfNeeded(pendingPreindexViewportWidth, pendingPreindexViewportHeight);
      }
    }
#if CROSSPOINT_PAPERS3
    warmPageFrameCacheIfIdle();
#endif
    return;
  }

  pendingNextChapterPreindex = false;

  // At end of the book, forward button goes home and back button returns to last page
  if (currentSpineIndex > 0 && currentSpineIndex >= epub->getSpineItemsCount()) {
    if (nextTriggered) {
      onGoHome();
    } else {
      currentSpineIndex = epub->getSpineItemsCount() - 1;
      nextPageNumber = UINT16_MAX;
      requestUpdate();
    }
    return;
  }

  const bool skipChapter = SETTINGS.longPressChapterSkip && mappedInput.getHeldTime() > skipChapterMs;

  // Don't skip chapter after screenshot (power + down released together)
  if (mappedInput.wasReleased(MappedInputManager::Button::Power) &&
      mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    return;
  }

  if (skipChapter) {
    lastPageTurnTime = millis();
    // We don't want to delete the section mid-render, so grab the semaphore
    {
      RenderLock lock(*this);
      lastPageTurnWasForward = nextTriggered;
      nextPageNumber = 0;
      currentSpineIndex = nextTriggered ? currentSpineIndex + 1 : currentSpineIndex - 1;
      section.reset();
#if CROSSPOINT_PAPERS3
  clearPageFrameCache(false);
#endif
    }
    requestUpdate();
    return;
  }

  // No current section, attempt to rerender the book
  if (!section) {
    requestUpdate();
    return;
  }

#if CROSSPOINT_PAPERS3
  // Deterministic cache-gated page turns: while the current page's adjacent
  // same-section frame caches are being prepared, keep one requested turn as a
  // pending command.  Later page-turn inputs are ignored until this command is
  // executed, so multiple swipes cannot stack up and skip pages.
  const bool requestedForwardTurn = !prevTriggered;
  const ReaderMemoryState turnMemoryState = updateReaderMemoryState("page-turn-input");
  const bool requireAdjacentCaches = turnMemoryState == ReaderMemoryState::NORMAL;
  const bool turnCacheReady = pageFrameCacheReadyForTurn(requestedForwardTurn);
  const bool adjacentCachesReady = requireAdjacentCaches ? adjacentPageFrameCachesReady() : true;
  if (!adjacentCachesReady || !turnCacheReady) {
    int targetPage = -1;
    const bool sameSectionTarget = sameSectionPageTurnTarget(requestedForwardTurn, targetPage);
    queuePendingPageTurn(requestedForwardTurn, "cache-not-ready");
    LOG_DBG(
        "ERS",
        "Page turn pending until frame cache ready: dir=%s cur=%d target=%d sameSection=%d warmActive=%d memState=%s requireAdjacent=%d",
        requestedForwardTurn ? "next" : "prev",
        section ? section->currentPage : -1,
        sameSectionTarget ? targetPage : -1,
        sameSectionTarget ? 1 : 0,
        pageFrameCacheWarmJob.active ? 1 : 0,
        readerMemoryStateName(turnMemoryState),
        requireAdjacentCaches ? 1 : 0
    );
    mappedInput.clearState();
    warmPageFrameCacheIfIdle();
    return;
  }
#endif

  if (prevTriggered) {
    pageTurn(false);
  } else {
    pageTurn(true);
  }
}

// Translate an absolute percent into a spine index plus a normalized position
// within that spine so we can jump after the section is loaded.
void EpubReaderActivity::jumpToPercent(int percent) {
  if (!epub) {
    return;
  }

  const size_t bookSize = epub->getBookSize();
  if (bookSize == 0) {
    return;
  }

  // Normalize input to 0-100 to avoid invalid jumps.
  percent = clampPercent(percent);

  // Convert percent into a byte-like absolute position across the spine sizes.
  // Use an overflow-safe computation: (bookSize / 100) * percent + (bookSize % 100) * percent / 100
  size_t targetSize =
      (bookSize / 100) * static_cast<size_t>(percent) + (bookSize % 100) * static_cast<size_t>(percent) / 100;
  if (percent >= 100) {
    // Ensure the final percent lands inside the last spine item.
    targetSize = bookSize - 1;
  }

  const int spineCount = epub->getSpineItemsCount();
  if (spineCount == 0) {
    return;
  }

  int targetSpineIndex = spineCount - 1;
  size_t prevCumulative = 0;

  for (int i = 0; i < spineCount; i++) {
    const size_t cumulative = epub->getCumulativeSpineItemSize(i);
    if (targetSize <= cumulative) {
      // Found the spine item containing the absolute position.
      targetSpineIndex = i;
      prevCumulative = (i > 0) ? epub->getCumulativeSpineItemSize(i - 1) : 0;
      break;
    }
  }

  const size_t cumulative = epub->getCumulativeSpineItemSize(targetSpineIndex);
  const size_t spineSize = (cumulative > prevCumulative) ? (cumulative - prevCumulative) : 0;
  // Store a normalized position within the spine so it can be applied once loaded.
  pendingSpineProgress =
      (spineSize == 0) ? 0.0f : static_cast<float>(targetSize - prevCumulative) / static_cast<float>(spineSize);
  if (pendingSpineProgress < 0.0f) {
    pendingSpineProgress = 0.0f;
  } else if (pendingSpineProgress > 1.0f) {
    pendingSpineProgress = 1.0f;
  }

  // Reset state so render() reloads and repositions on the target spine.
  {
    RenderLock lock(*this);
    currentSpineIndex = targetSpineIndex;
    nextPageNumber = 0;
    pendingPercentJump = true;
    section.reset();
#if CROSSPOINT_PAPERS3
  clearPageFrameCache(false);
#endif
  }
}

void EpubReaderActivity::onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action) {
  switch (action) {
    case EpubReaderMenuActivity::MenuAction::SELECT_CHAPTER: {
      const int spineIdx = currentSpineIndex;
      const std::string path = epub->getPath();
      startActivityForResult(
          std::make_unique<EpubReaderChapterSelectionActivity>(renderer, mappedInput, epub, path, spineIdx),
          [this](const ActivityResult& result) {
            if (!result.isCancelled && currentSpineIndex != std::get<ChapterResult>(result.data).spineIndex) {
              RenderLock lock(*this);
              currentSpineIndex = std::get<ChapterResult>(result.data).spineIndex;
              nextPageNumber = 0;
              section.reset();
#if CROSSPOINT_PAPERS3
  clearPageFrameCache(false);
#endif
            }
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::READER_SETTINGS: {
      // Open the normal Settings UI directly on the Reader tab. Returning from
      // it keeps the current book active and forces the current section to be
      // revalidated/reflowed using the updated settings.
      if (section) {
        cachedSpineIndex = currentSpineIndex;
        cachedChapterTotalPageCount = section->pageCount;
        nextPageNumber = section->currentPage;
      }
      startActivityForResult(
          std::make_unique<SettingsActivity>(renderer, mappedInput, 1, true),
          [this](const ActivityResult&) {
            RenderLock lock(*this);
            if (section) {
              cachedSpineIndex = currentSpineIndex;
              cachedChapterTotalPageCount = section->pageCount;
              nextPageNumber = section->currentPage;
            }
            section.reset();
#if CROSSPOINT_PAPERS3
  clearPageFrameCache(false);
#endif
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::FOOTNOTES: {
      startActivityForResult(std::make_unique<EpubReaderFootnotesActivity>(renderer, mappedInput, currentPageFootnotes),
                             [this](const ActivityResult& result) {
                               if (!result.isCancelled) {
                                 const auto& footnoteResult = std::get<FootnoteResult>(result.data);
                                 navigateToHref(footnoteResult.href, true);
                               }
                               requestUpdate();
                             });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::GO_TO_PERCENT: {
      float bookProgress = 0.0f;
      if (epub && epub->getBookSize() > 0 && section && section->pageCount > 0) {
        const float chapterProgress = static_cast<float>(section->currentPage) / static_cast<float>(section->pageCount);
        bookProgress = epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f;
      }
      const int initialPercent = clampPercent(static_cast<int>(bookProgress + 0.5f));
      startActivityForResult(
          std::make_unique<EpubReaderPercentSelectionActivity>(renderer, mappedInput, initialPercent),
          [this](const ActivityResult& result) {
            if (!result.isCancelled) {
              jumpToPercent(std::get<PercentResult>(result.data).percent);
            }
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::DISPLAY_QR: {
      if (section && section->currentPage >= 0 && section->currentPage < section->pageCount) {
        auto p = section->loadPageFromSectionFile();
        if (p) {
          std::string fullText;
          for (const auto& el : p->elements) {
            if (el->getTag() == TAG_PageLine) {
              const auto& line = static_cast<const PageLine&>(*el);
              if (line.getBlock()) {
                const auto& words = line.getBlock()->getWords();
                for (const auto& w : words) {
                  if (!fullText.empty()) fullText += " ";
                  fullText += w;
                }
              }
            }
          }
          if (!fullText.empty()) {
            startActivityForResult(std::make_unique<QrDisplayActivity>(renderer, mappedInput, fullText),
                                   [this](const ActivityResult& result) {});
            break;
          }
        }
      }
      // If no text or page loading failed, just close menu
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::GO_HOME: {
      onGoHome();
      return;
    }
    case EpubReaderMenuActivity::MenuAction::DELETE_CACHE: {
      {
        RenderLock lock(*this);
        if (epub && section) {
          uint16_t backupSpine = currentSpineIndex;
          uint16_t backupPage = section->currentPage;
          uint16_t backupPageCount = section->pageCount;
          section.reset();
#if CROSSPOINT_PAPERS3
  clearPageFrameCache(false);
#endif
          epub->clearCache();
          epub->setupCacheDir();
          saveProgress(backupSpine, backupPage, backupPageCount);
        }
      }
      onGoHome();
      return;
    }
    case EpubReaderMenuActivity::MenuAction::SCREENSHOT: {
      {
        RenderLock lock(*this);
        pendingScreenshot = true;
      }
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::SYNC: {
      if (KOREADER_STORE.hasCredentials()) {
        const int currentPage = section ? section->currentPage : 0;
        const int totalPages = section ? section->pageCount : 0;
        startActivityForResult(
            std::make_unique<KOReaderSyncActivity>(renderer, mappedInput, epub, epub->getPath(), currentSpineIndex,
                                                   currentPage, totalPages),
            [this](const ActivityResult& result) {
              if (!result.isCancelled) {
                const auto& sync = std::get<SyncResult>(result.data);
                if (currentSpineIndex != sync.spineIndex || (section && section->currentPage != sync.page)) {
                  RenderLock lock(*this);
                  currentSpineIndex = sync.spineIndex;
                  nextPageNumber = sync.page;
                  section.reset();
#if CROSSPOINT_PAPERS3
  clearPageFrameCache(false);
#endif
                }
              }
            });
      }
      break;
    }
  }
}

void EpubReaderActivity::applyOrientation(const uint8_t orientation) {
  // No-op if the selected orientation matches current settings.
  if (SETTINGS.orientation == orientation) {
    return;
  }

  // Preserve current reading position so we can restore after reflow.
  {
    RenderLock lock(*this);
    if (section) {
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->pageCount;
      nextPageNumber = section->currentPage;
    }

    // Persist the selection so the reader keeps the new orientation on next launch.
    SETTINGS.orientation = orientation;
    SETTINGS.saveToFile();

    // Update renderer orientation to match the new logical coordinate system.
    ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);

    // Reset section to force re-layout in the new orientation.
    section.reset();
#if CROSSPOINT_PAPERS3
  clearPageFrameCache(false);
#endif
  }
}

void EpubReaderActivity::toggleAutoPageTurn(const uint8_t selectedPageTurnOption) {
  if (selectedPageTurnOption == 0 || selectedPageTurnOption >= PAGE_TURN_LABELS.size()) {
    automaticPageTurnActive = false;
    return;
  }

  lastPageTurnTime = millis();
  // calculates page turn duration by dividing by number of pages
  pageTurnDuration = (1UL * 60 * 1000) / PAGE_TURN_LABELS[selectedPageTurnOption];
  automaticPageTurnActive = true;

  const uint8_t statusBarHeight = UITheme::getInstance().getStatusBarHeight();
  // resets cached section so that space is reserved for auto page turn indicator when None or progress bar only
  if (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight()) {
    // Preserve current reading position so we can restore after reflow.
    RenderLock lock(*this);
    if (section) {
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->pageCount;
      nextPageNumber = section->currentPage;
    }
    section.reset();
#if CROSSPOINT_PAPERS3
  clearPageFrameCache(false);
#endif
  }
}

void EpubReaderActivity::pageTurn(bool isForwardTurn) {
  lastPageTurnWasForward = isForwardTurn;
#if CROSSPOINT_PAPERS3
  clearPendingPageTurn();
  abortPageFrameCacheWarmJob();
  lastReaderInputAt = millis();
#endif
  if (isForwardTurn) {
    if (section->currentPage < section->pageCount - 1) {
      section->currentPage++;
    } else {
      // We don't want to delete the section mid-render, so grab the semaphore
      {
        RenderLock lock(*this);
        nextPageNumber = 0;
        currentSpineIndex++;
        section.reset();
#if CROSSPOINT_PAPERS3
  clearPageFrameCache(false);
#endif
      }
    }
  } else {
    if (section->currentPage > 0) {
      section->currentPage--;
    } else if (currentSpineIndex > 0) {
      // We don't want to delete the section mid-render, so grab the semaphore
      {
        RenderLock lock(*this);
        nextPageNumber = UINT16_MAX;
        currentSpineIndex--;
        section.reset();
#if CROSSPOINT_PAPERS3
  clearPageFrameCache(false);
#endif
      }
    }
  }
  lastPageTurnTime = millis();
  requestUpdate();
}

// TODO: Failure handling
void EpubReaderActivity::render(RenderLock&& lock) {
  if (!epub) {
    return;
  }

  // edge case handling for sub-zero spine index
  if (currentSpineIndex < 0) {
    currentSpineIndex = 0;
  }
  // based bounds of book, show end of book screen
  if (currentSpineIndex > epub->getSpineItemsCount()) {
    currentSpineIndex = epub->getSpineItemsCount();
  }

  // Show end of book screen
  if (currentSpineIndex == epub->getSpineItemsCount()) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_END_OF_BOOK), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    automaticPageTurnActive = false;
    return;
  }

  // Apply screen viewable areas and additional padding
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  orientedMarginTop += SETTINGS.screenMargin;
  orientedMarginLeft += SETTINGS.screenMargin;
  orientedMarginRight += SETTINGS.screenMargin;

  const uint8_t statusBarHeight = UITheme::getInstance().getStatusBarHeight();

  // Reader Status Bar Margin Mode:
  // - Off: status bar stays at the bottom; content bottom margin is at least
  //   statusBarHeight so text never overlaps it.
  // - On: status bar itself follows the page margin; content reserves both the
  //   page margin and the status bar height for framed/background themes.
  if (SETTINGS.statusBarFollowsPageMargin) {
    orientedMarginBottom += SETTINGS.screenMargin + statusBarHeight;
  } else if (automaticPageTurnActive &&
             (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight())) {
    orientedMarginBottom +=
        std::max(SETTINGS.screenMargin,
                 static_cast<uint8_t>(statusBarHeight + UITheme::getInstance().getMetrics().statusBarVerticalMargin));
  } else {
    orientedMarginBottom += std::max(SETTINGS.screenMargin, statusBarHeight);
  }

  if (!section) {
    const auto filepath = epub->getSpineItem(currentSpineIndex).href;
    LOG_DBG("ERS", "Loading file: %s, index: %d", filepath.c_str(), currentSpineIndex);
    section = std::unique_ptr<Section>(new Section(epub, currentSpineIndex, renderer));

    const uint16_t viewportWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
    const uint16_t viewportHeight = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;

    if (!section->loadSectionFile(SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(),
                                  SETTINGS.getReaderCharacterSpacing(), SETTINGS.extraParagraphSpacing, SETTINGS.paragraphAlignment, viewportWidth,
                                  viewportHeight, SETTINGS.hyphenationEnabled, SETTINGS.embeddedStyle,
                                  SETTINGS.imageRendering, SETTINGS.readingLayout)) {
      LOG_DBG("ERS", "Cache not found, building...");

      Rect popupRect{};
      bool popupShown = false;
      const auto popupFn = [this, &popupRect, &popupShown]() {
        if (!popupShown) {
          popupRect = GUI.drawPopup(renderer, tr(STR_INDEXING));
          popupShown = true;
        }
      };
      const auto popupProgressFn = [this, &popupRect, &popupShown](int progress) {
        if (!popupShown) {
          popupRect = GUI.drawPopup(renderer, tr(STR_INDEXING));
          popupShown = true;
        }
        GUI.fillPopupProgress(renderer, popupRect, progress);
      };

      if (!section->createSectionFile(SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(),
                                      SETTINGS.getReaderCharacterSpacing(), SETTINGS.extraParagraphSpacing, SETTINGS.paragraphAlignment, viewportWidth,
                                      viewportHeight, SETTINGS.hyphenationEnabled, SETTINGS.embeddedStyle,
                                      SETTINGS.imageRendering, SETTINGS.readingLayout, popupFn, popupProgressFn)) {
        LOG_ERR("ERS", "Failed to persist page data to SD");
        section.reset();
#if CROSSPOINT_PAPERS3
  clearPageFrameCache(false);
#endif
        return;
      }
#if CROSSPOINT_PAPERS3
      // Force full refresh after indexing popup to clear its ghost
      renderer.requestFullRefresh();
#endif
    } else {
      LOG_DBG("ERS", "Cache found, skipping build...");
    }

    if (nextPageNumber == UINT16_MAX) {
      section->currentPage = section->pageCount - 1;
    } else {
      section->currentPage = nextPageNumber;
      if (section->currentPage < 0 || section->currentPage >= section->pageCount) {
        LOG_DBG("ERS", "Saved page out of range: %d (pageCount=%d), resetting to first page",
                section->currentPage, section->pageCount);
        section->currentPage = 0;
      }
    }

    if (!pendingAnchor.empty()) {
      if (const auto page = section->getPageForAnchor(pendingAnchor)) {
        section->currentPage = *page;
        LOG_DBG("ERS", "Resolved anchor '%s' to page %d", pendingAnchor.c_str(), *page);
      } else {
        LOG_DBG("ERS", "Anchor '%s' not found in section %d", pendingAnchor.c_str(), currentSpineIndex);
      }
      pendingAnchor.clear();
    }

    // handles changes in reader settings and reset to approximate position based on cached progress
    if (cachedChapterTotalPageCount > 0) {
      // only goes to relative position if spine index matches cached value
      if (currentSpineIndex == cachedSpineIndex && section->pageCount != cachedChapterTotalPageCount) {
        float progress = static_cast<float>(section->currentPage) / static_cast<float>(cachedChapterTotalPageCount);
        int newPage = static_cast<int>(progress * section->pageCount);
        section->currentPage = newPage;
      }
      cachedChapterTotalPageCount = 0;  // resets to 0 to prevent reading cached progress again
    }

    if (pendingPercentJump && section->pageCount > 0) {
      // Apply the pending percent jump now that we know the new section's page count.
      int newPage = static_cast<int>(pendingSpineProgress * static_cast<float>(section->pageCount));
      if (newPage >= section->pageCount) {
        newPage = section->pageCount - 1;
      }
      section->currentPage = newPage;
      pendingPercentJump = false;
    }
  }

  renderer.clearScreen();

  if (section->pageCount == 0) {
    LOG_DBG("ERS", "No pages to render");
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_CHAPTER), true, EpdFontFamily::BOLD);
    renderStatusBar();
    renderer.displayBuffer();
    automaticPageTurnActive = false;
    return;
  }

  if (section->currentPage < 0 || section->currentPage >= section->pageCount) {
    LOG_DBG("ERS", "Page out of bounds: %d (max %d)", section->currentPage, section->pageCount);
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_OUT_OF_BOUNDS), true, EpdFontFamily::BOLD);
    renderStatusBar();
    renderer.displayBuffer();
    automaticPageTurnActive = false;
    return;
  }

#if CROSSPOINT_PAPERS3
  if (restorePageFrameCacheToRenderer(currentSpineIndex, section->currentPage, true)) {
    const auto t0 = millis();
    if (lastVisiblePageHadImages && !restoredPageFrameHadImages) {
      renderer.requestFullRefresh();
      LOG_DBG("ERS", "Full refresh requested: image page -> text page cache hit");
    }
    ReaderUtils::displayWithRefreshCycle(
        renderer,
        pagesUntilFullRefresh,
        lastPageTurnWasForward
    );
    waitForVisibleDisplayIdle("cache-hit");
    lastVisiblePageHadImages = restoredPageFrameHadImages;
    LOG_DBG(
        "ERS",
        "Page render: cache=hit display=%lums total=%lums",
        millis() - t0,
        millis() - t0
    );
  } else
#endif
  {
#if CROSSPOINT_PAPERS3
    const ReaderHeapTrace visiblePageLoadBefore = captureReaderHeapTrace();
    const unsigned long visiblePageLoadStart = millis();
#endif
    auto p = section->loadPageFromSectionFile();
#if CROSSPOINT_PAPERS3
    const ReaderHeapTrace visiblePageLoadAfter = captureReaderHeapTrace();
    logReaderHeapDelta("visible-page-object-load", visiblePageLoadBefore, visiblePageLoadAfter, millis() - visiblePageLoadStart);
#endif
    if (!p) {
      LOG_ERR("ERS", "Failed to load page from SD - clearing section cache");
      section->clearCache();
      section.reset();
#if CROSSPOINT_PAPERS3
  clearPageFrameCache(false);
#endif
      requestUpdate();  // Try again after clearing cache
                        // TODO: prevent infinite loop if the page keeps failing to load for some reason
      automaticPageTurnActive = false;
      return;
    }

    // Collect footnotes from the loaded page
    currentPageFootnotes = std::move(p->footnotes);

    const auto start = millis();
    renderContents(std::move(p), orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    LOG_DBG("ERS", "Rendered page in %dms", millis() - start);
  }
  scheduleSilentIndexNextChapter(renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight,
                                 renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom);
  saveProgress(currentSpineIndex, section->currentPage, section->pageCount);
#if CROSSPOINT_PAPERS3
  // Give the user an idle window after each visible page update before warming
  // frame cache, otherwise a cache render can start immediately after a slow
  // page render and make the next tap feel ignored.
  lastReaderInputAt = millis();
#endif

  if (pendingScreenshot) {
    pendingScreenshot = false;
    ScreenshotUtil::takeScreenshot(renderer);
  }
}

void EpubReaderActivity::scheduleSilentIndexNextChapter(const uint16_t viewportWidth,
                                                          const uint16_t viewportHeight) {
  pendingNextChapterPreindex = false;
  if (!epub || !section || section->pageCount < 2) {
    return;
  }

  // Schedule pre-indexing near chapter end, but do not run it immediately after
  // page render.  Running it immediately made the penultimate page feel slow.
  if (section->currentPage < section->pageCount - 2) {
    return;
  }

  const int nextSpineIndex = currentSpineIndex + 1;
  if (nextSpineIndex < 0 || nextSpineIndex >= epub->getSpineItemsCount()) {
    return;
  }

#if CROSSPOINT_PAPERS3
  if (!readerMemoryAllowsSilentIndexing("silent-index-schedule")) {
    return;
  }
#endif

  pendingNextChapterPreindex = true;
  nextChapterPreindexAt = millis();
  pendingPreindexViewportWidth = viewportWidth;
  pendingPreindexViewportHeight = viewportHeight;
  LOG_DBG("ERS", "Scheduled silent indexing for chapter %d after idle delay", nextSpineIndex);
}

void EpubReaderActivity::silentIndexNextChapterIfNeeded(const uint16_t viewportWidth, const uint16_t viewportHeight) {
  if (!epub || !section || section->pageCount < 2) {
    return;
  }

  // Build the next chapter cache only while user is near chapter end.
  if (section->currentPage < section->pageCount - 2) {
    return;
  }

  const int nextSpineIndex = currentSpineIndex + 1;
  if (nextSpineIndex < 0 || nextSpineIndex >= epub->getSpineItemsCount()) {
    return;
  }

#if CROSSPOINT_PAPERS3
  if (!readerMemoryAllowsSilentIndexing("silent-index")) {
    return;
  }
#endif

  Section nextSection(epub, nextSpineIndex, renderer);
  if (nextSection.loadSectionFile(SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(),
                                  SETTINGS.getReaderCharacterSpacing(), SETTINGS.extraParagraphSpacing, SETTINGS.paragraphAlignment, viewportWidth,
                                  viewportHeight, SETTINGS.hyphenationEnabled, SETTINGS.embeddedStyle,
                                  SETTINGS.imageRendering, SETTINGS.readingLayout)) {
    return;
  }

  LOG_DBG("ERS", "Silently indexing next chapter: %d", nextSpineIndex);
  if (!nextSection.createSectionFile(SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(),
                                     SETTINGS.getReaderCharacterSpacing(), SETTINGS.extraParagraphSpacing,
                                     SETTINGS.paragraphAlignment, viewportWidth,
                                     viewportHeight, SETTINGS.hyphenationEnabled, SETTINGS.embeddedStyle,
                                     SETTINGS.imageRendering, SETTINGS.readingLayout, std::function<void()>{},
                                     std::function<void(int)>{}, true)) {
    LOG_ERR("ERS", "Failed silent indexing for chapter: %d", nextSpineIndex);
  }
}

void EpubReaderActivity::saveProgress(int spineIndex, int currentPage, int pageCount) {
  if (!epub || spineIndex < 0 || spineIndex >= epub->getSpineItemsCount() ||
      currentPage < 0 || currentPage == UINT16_MAX ||
      (pageCount > 0 && currentPage >= pageCount)) {
    LOG_DBG(
        "ERS",
        "Progress save skipped: invalid spine=%d page=%d pageCount=%d spineCount=%d",
        spineIndex,
        currentPage,
        pageCount,
        epub ? epub->getSpineItemsCount() : -1
    );
    return;
  }

  FsFile f;
  if (Storage.openFileForWrite("ERS", epub->getCachePath() + "/progress.bin", f)) {
    uint8_t data[6];
    data[0] = spineIndex & 0xFF;
    data[1] = (spineIndex >> 8) & 0xFF;
    data[2] = currentPage & 0xFF;
    data[3] = (currentPage >> 8) & 0xFF;
    data[4] = pageCount & 0xFF;
    data[5] = (pageCount >> 8) & 0xFF;
    f.write(data, 6);
    LOG_DBG("ERS", "Progress saved: Chapter %d, Page %d", spineIndex, currentPage);
  } else {
    LOG_ERR("ERS", "Could not save progress!");
  }
}


#if CROSSPOINT_PAPERS3
bool EpubReaderActivity::renderContentsProgressive(
    std::unique_ptr<Page> page,
    const int orientedMarginTop,
    const int orientedMarginRight,
    const int orientedMarginBottom,
    const int orientedMarginLeft
) {
  if (!page || page->hasImages()) {
    return false;
  }
  if (SETTINGS.readingLayout != CrossPointSettings::VERTICAL_LAYOUT) {
    return false;
  }

  const auto t0 = millis();
  auto* fcm = renderer.getFontCacheManager();
  fcm->resetStats();
  const uint32_t heapBefore = esp_get_free_heap_size();
  fcm->logStats("prewarm");
  const auto tPrewarm = millis();
  LOG_DBG(
      "ERS",
      "Heap: before=%lu after=%lu delta=%ld",
      heapBefore,
      esp_get_free_heap_size(),
      static_cast<int32_t>(esp_get_free_heap_size()) - static_cast<int32_t>(heapBefore)
  );

  const int fontId = SETTINGS.getReaderFontId();
  std::vector<ProgressiveElementInfo> elementInfo;
  elementInfo.reserve(page->elements.size());
  for (size_t i = 0; i < page->elements.size(); ++i) {
    ProgressiveElementInfo info = makeProgressiveElementInfo(
        renderer,
        *page->elements[i],
        fontId,
        orientedMarginLeft,
        orientedMarginTop
    );
    info.index = i;
    elementInfo.push_back(info);
  }

  const bool rowsAscending = progressiveRowsAscending(renderer, lastPageTurnWasForward);
  std::stable_sort(
      elementInfo.begin(),
      elementInfo.end(),
      [rowsAscending](const ProgressiveElementInfo& a, const ProgressiveElementInfo& b) {
        if (a.rowCenter == b.rowCenter) {
          return rowsAscending ? (a.index < b.index) : (a.index > b.index);
        }
        return rowsAscending ? (a.rowCenter < b.rowCenter) : (a.rowCenter > b.rowCenter);
      }
  );

  renderer.clearScreen();
  prepareReaderContentBackground(renderer, orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
  beginReaderContentRender(renderer);
  if (ReaderUtils::shouldUseTextAntiAliasingForReader()) {
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_DIRECT);
  }

  LOG_DBG(
      "PRG",
      "foreground grouped progressive start: elements=%u groupSize=%u order=%s refreshMode=%s cacheMissOnly=1",
      static_cast<unsigned>(elementInfo.size()),
      static_cast<unsigned>(foregroundProgressiveElementsPerRefresh),
      rowsAscending ? "physical-row-ascending" : "physical-row-descending",
      SETTINGS.pageTurnRefreshMode == CrossPointSettings::PAGE_TURN_REFRESH_ORIGINAL ? "single-refresh-path" : "band-scan"
  );
  logProgressiveInternalHeap("start");

  // The page-turn gesture that triggered this render can still be physically
  // down/releasing while the first progressive groups are being drawn.  Drain it
  // before we start polling inside the render loop, otherwise the same gesture
  // (or a second gesture during the render) can be processed by the reader loop
  // after this page finishes, advancing the page number without a matching
  // visible render.  V32 ignores gestures while a foreground cache-miss render
  // is in progress; the user can swipe again after the page is complete.
  mappedInput.clearState();
  bool inputDuringProgressiveRender = false;

  const auto tElementsStart = millis();
  unsigned renderedElements = 0;
  unsigned refreshGroups = 0;
  unsigned long textTotal = 0;
  unsigned long imageTotal = 0;
  unsigned long displayTotal = 0;
  unsigned long slowestDraw = 0;
  size_t slowestIndex = 0;

  size_t groupBeginOrder = 0;
  int groupRowStart = HalDisplay::DISPLAY_HEIGHT - 1;
  int groupRowEnd = 0;
  unsigned groupElements = 0;
  unsigned long groupDrawTotal = 0;
  bool groupHasContent = false;

  const auto flushGroup = [&](const size_t nextOrderIndex) {
    if (!groupHasContent) {
      return;
    }

    groupRowStart = std::max(0, std::min<int>(HalDisplay::DISPLAY_HEIGHT - 1, groupRowStart));
    groupRowEnd = std::max(0, std::min<int>(HalDisplay::DISPLAY_HEIGHT - 1, groupRowEnd));
    if (groupRowStart > groupRowEnd) {
      std::swap(groupRowStart, groupRowEnd);
    }

    const auto tDisplayStart = millis();
    renderer.displayPhysicalRows(groupRowStart, groupRowEnd);
    const auto tDisplayEnd = millis();
    const unsigned long displayMs = tDisplayEnd - tDisplayStart;
    displayTotal += displayMs;
    ++refreshGroups;

    LOG_DBG(
        "PRG",
        "group %u elements=%u order=%u..%u rows=%d..%d draw=%lums displayCall=%lums",
        refreshGroups,
        groupElements,
        static_cast<unsigned>(groupBeginOrder + 1),
        static_cast<unsigned>(nextOrderIndex),
        groupRowStart,
        groupRowEnd,
        groupDrawTotal,
        displayMs
    );

    groupBeginOrder = nextOrderIndex;
    groupRowStart = HalDisplay::DISPLAY_HEIGHT - 1;
    groupRowEnd = 0;
    groupElements = 0;
    groupDrawTotal = 0;
    groupHasContent = false;

    // Keep the touch system fresh between visible chunks.  We do not abort the
    // foreground page render here because that would leave a half-old/half-new
    // page on screen.  The next reader loop can handle the queued input after
    // the current page has reached a consistent complete state.
    mappedInput.update();
    if (hasReaderInputPending()) {
      lastReaderInputAt = millis();
      inputDuringProgressiveRender = true;
      LOG_DBG(
          "PRG",
          "input observed and will be ignored during grouped progressive after group=%u elements=%u/%u",
          refreshGroups,
          renderedElements,
          static_cast<unsigned>(elementInfo.size())
      );
      mappedInput.clearState();
    }
  };

  for (size_t orderIndex = 0; orderIndex < elementInfo.size(); ++orderIndex) {
    const auto& info = elementInfo[orderIndex];
    if (info.index >= page->elements.size()) {
      continue;
    }

    const auto& element = page->elements[info.index];
    const PageElementTag tag = element->getTag();
    const auto tDrawStart = millis();
    {
      PageRenderProfiler::Scoped pageRenderProfile(true);
      element->render(
          renderer,
          fontId,
          orientedMarginLeft,
          orientedMarginTop
      );
    }
    const auto tDrawEnd = millis();

    const unsigned long drawMs = tDrawEnd - tDrawStart;
    if (tag == TAG_PageImage) {
      imageTotal += drawMs;
    } else {
      textTotal += drawMs;
    }
    if (drawMs > slowestDraw) {
      slowestDraw = drawMs;
      slowestIndex = info.index;
    }

    if (!groupHasContent) {
      groupBeginOrder = orderIndex;
      groupHasContent = true;
    }
    groupRowStart = std::min(groupRowStart, info.rowStart);
    groupRowEnd = std::max(groupRowEnd, info.rowEnd);
    ++groupElements;
    groupDrawTotal += drawMs;
    ++renderedElements;

    LOG_DBG(
        "PRG",
        "element %u/%u index=%u tag=%u rows=%d..%d center=%d draw=%lums",
        static_cast<unsigned>(orderIndex + 1),
        static_cast<unsigned>(elementInfo.size()),
        static_cast<unsigned>(info.index),
        static_cast<unsigned>(tag),
        info.rowStart,
        info.rowEnd,
        info.rowCenter,
        drawMs
    );

    if (groupElements >= foregroundProgressiveElementsPerRefresh) {
      flushGroup(orderIndex + 1);
    }
  }
  flushGroup(elementInfo.size());

  renderer.setRenderMode(GfxRenderer::BW);
  endReaderContentRender(renderer);
  const auto tElementsEnd = millis();

  const auto tStatusStart = millis();
  renderStatusBar();
  int statusRowStart = 0;
  int statusRowEnd = 0;
  const int statusHeight = std::max<int>(
      1,
      UITheme::getInstance().getStatusBarHeight() +
          UITheme::getInstance().getMetrics().statusBarVerticalMargin * 2 +
          SETTINGS.screenMargin
  );
  renderer.logicalRectToPhysicalRows(
      0,
      std::max(0, renderer.getScreenHeight() - statusHeight),
      renderer.getScreenWidth(),
      statusHeight,
      &statusRowStart,
      &statusRowEnd
  );
  renderer.displayPhysicalRows(statusRowStart, statusRowEnd);
  const auto tStatusEnd = millis();

  fcm->logStats("page_render");

  const auto tIdleBeforeCacheStart = millis();
  renderer.waitDisplayIdle();
  const auto tIdleBeforeCacheEnd = millis();
  logProgressiveInternalHeap("before-cache-store");

  const auto tCacheStoreStart = millis();
  if (section) {
    copyCurrentFrameToPageFrameCache(currentSpineIndex, section->currentPage, currentPageFootnotes, false);
  }
  const auto tCacheStoreEnd = millis();

  // Do not let any gesture sampled during the visible page render become a
  // delayed page turn after the current framebuffer has already been stored.
  // This fixes the observed "swipe during render, next swipe jumps two pages"
  // state mismatch by treating all touches during foreground rendering as
  // consumed/no-op.
  mappedInput.clearState();
  if (inputDuringProgressiveRender) {
    LOG_DBG("PRG", "input drained after grouped progressive render");
  } else {
    LOG_DBG("PRG", "input state cleared after grouped progressive render");
  }

  LOG_DBG(
      "ERS",
      "render phase: displayIdleBeforeCache=%lums cacheStore=%lums",
      tIdleBeforeCacheEnd - tIdleBeforeCacheStart,
      tCacheStoreEnd - tCacheStoreStart
  );
  logProgressiveInternalHeap("done");

  LOG_DBG(
      "PRG",
      "foreground grouped progressive done: elements=%u groups=%u draw=%lums text=%lums image=%lums displayCalls=%lums status=%lums idleBeforeCache=%lums slowestIndex=%u slowest=%lums",
      renderedElements,
      refreshGroups,
      tElementsEnd - tElementsStart,
      textTotal,
      imageTotal,
      displayTotal,
      tStatusEnd - tStatusStart,
      tIdleBeforeCacheEnd - tIdleBeforeCacheStart,
      static_cast<unsigned>(slowestIndex),
      slowestDraw
  );

  LOG_DBG(
      "ERS",
      "Page render progressive-groups: prewarm=%lums render=%lums status=%lums idleBeforeCache=%lums cacheStore=%lums total=%lums",
      tPrewarm - t0,
      tElementsEnd - tPrewarm,
      tStatusEnd - tStatusStart,
      tIdleBeforeCacheEnd - tIdleBeforeCacheStart,
      tCacheStoreEnd - tCacheStoreStart,
      millis() - t0
  );

  (void)orientedMarginBottom;
  (void)orientedMarginRight;
  return true;
}
#endif

void EpubReaderActivity::renderContents(
    std::unique_ptr<Page> page,
    const int orientedMarginTop,
    const int orientedMarginRight,
    const int orientedMarginBottom,
    const int orientedMarginLeft
) {
#if CROSSPOINT_PAPERS3
  // V1.8.0 reader path: visible cache-miss pages are rendered into the
  // framebuffer once and displayed once.  Next/previous responsiveness comes
  // from adjacent-page frame cache readiness, while TTF safety is handled by
  // background glyph-miss aborts, conservative idle glyph prewarm, and the
  // FreeType PSRAM allocator used by OpenFontRender.
#endif
  const auto t0 = millis();
#if CROSSPOINT_PAPERS3
  logReaderHeapCheckpoint("visible-render-start");
#endif
  const bool currentPageHasImages = page ? page->hasImages() : false;
#if CROSSPOINT_PAPERS3
  const ReaderMemorySnapshot visibleTtfSnapshot = getReaderMemorySnapshot();
  const bool allowVisibleTtfRasterize =
      visibleTtfSnapshot.internalFree >= visibleTtfRasterizeLowMemoryFreeThreshold &&
      visibleTtfSnapshot.internalMaxAlloc >= visibleTtfRasterizeLowMemoryMaxAllocThreshold;
  if (!allowVisibleTtfRasterize) {
    LOG_DBG(
        "ERS",
        "Visible TTF rasterize guarded: internalFree=%lu internalMaxAlloc=%lu requiredFree=%lu requiredMaxAlloc=%lu",
        static_cast<unsigned long>(visibleTtfSnapshot.internalFree),
        static_cast<unsigned long>(visibleTtfSnapshot.internalMaxAlloc),
        static_cast<unsigned long>(visibleTtfRasterizeLowMemoryFreeThreshold),
        static_cast<unsigned long>(visibleTtfRasterizeLowMemoryMaxAllocThreshold)
    );
  }
#endif

  // V1.5 固定直排測試開關。
  constexpr bool ENABLE_VERTICAL_RENDER_TEST = false;

  auto* fcm = renderer.getFontCacheManager();
  fcm->resetStats();

  // Font prewarm is disabled for the framebuffer-cache page-turn path.
  // On the tested TTF/CJK path it produced no FDC hits and added ~700-800ms.
  constexpr bool ENABLE_READER_FONT_PREWARM = false;
  const uint32_t heapBefore = esp_get_free_heap_size();

  if (ENABLE_READER_FONT_PREWARM) {
    auto scope = fcm->createPrewarmScope();

    page->render(
        renderer,
        SETTINGS.getReaderFontId(),
        orientedMarginLeft,
        orientedMarginTop
    );

    scope.endScanAndPrewarm();
  }

  const uint32_t heapAfter = esp_get_free_heap_size();
  fcm->logStats("prewarm");
  const auto tPrewarm = millis();

  LOG_DBG(
      "ERS",
      "Heap: before=%lu after=%lu delta=%ld",
      heapBefore,
      heapAfter,
      static_cast<int32_t>(heapAfter) -
          static_cast<int32_t>(heapBefore)
  );

  /*
   * V1.5 固定 Vertical TextBlock 測試。
   *
   * 使用 lambda，因為含圖片＋AA 模式時，
   * 畫面會做第二次 page->render()，測試字也要重新畫一次。
   */
  const auto renderVerticalTest = [this,
                                   orientedMarginTop,
                                   orientedMarginRight]() {
    const int testFontId =
        SETTINGS.getReaderFontId();

    const int advance =
        renderer.getLineHeight(testFontId);

    std::vector<std::string> glyphs = {
        "天", "地", "玄", "黃",
        "宇", "宙", "洪", "荒",
        "日", "月", "盈", "昃"
    };

    std::vector<int16_t> glyphX = {
        0,
        0,
        0,
        0,

        static_cast<int16_t>(-advance),
        static_cast<int16_t>(-advance),
        static_cast<int16_t>(-advance),
        static_cast<int16_t>(-advance),

        static_cast<int16_t>(-advance * 2),
        static_cast<int16_t>(-advance * 2),
        static_cast<int16_t>(-advance * 2),
        static_cast<int16_t>(-advance * 2)
    };

    std::vector<int16_t> glyphY = {
        0,
        static_cast<int16_t>(advance),
        static_cast<int16_t>(advance * 2),
        static_cast<int16_t>(advance * 3),

        0,
        static_cast<int16_t>(advance),
        static_cast<int16_t>(advance * 2),
        static_cast<int16_t>(advance * 3),

        0,
        static_cast<int16_t>(advance),
        static_cast<int16_t>(advance * 2),
        static_cast<int16_t>(advance * 3)
    };

    std::vector<EpdFontFamily::Style> glyphStyles(
        glyphs.size(),
        EpdFontFamily::REGULAR
    );

    TextBlock verticalTest(
        std::move(glyphs),
        std::move(glyphX),
        std::move(glyphY),
        std::move(glyphStyles),
        BlockStyle(),
        TextLayoutMode::Vertical,
        12
    );

    verticalTest.render(
        renderer,
        testFontId,
        renderer.getScreenWidth() -
            orientedMarginRight -
            60,
        orientedMarginTop + 20
    );
  };

  /*
   * 正式繪圖。
   *
   * 若開啟 anti-aliasing，正文先使用 GRAYSCALE_DIRECT。
   * 這裡的 page->render() 才是真正畫到 framebuffer 的 render pass。
   */
  const auto tBackgroundStart = millis();
  prepareReaderContentBackground(renderer, orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
  const auto tBackgroundEnd = millis();
  beginReaderContentRender(renderer);
  const auto tBeginContentEnd = millis();

  if (ReaderUtils::shouldUseTextAntiAliasingForReader()) {
    renderer.setRenderMode(
        GfxRenderer::GRAYSCALE_DIRECT
    );
  }
  const auto tModeSetEnd = millis();

  {
#if CROSSPOINT_PAPERS3
    const ReaderHeapTrace glyphBefore = captureReaderHeapTrace();
    const unsigned long glyphStart = millis();
#endif
    PageRenderProfiler::Scoped pageRenderProfile(true);
#if CROSSPOINT_PAPERS3
    {
      ScopedTtfRasterizePolicy ttfPolicy(allowVisibleTtfRasterize, allowVisibleTtfRasterize ? "visible" : "visible-low-memory");
      page->render(
          renderer,
          SETTINGS.getReaderFontId(),
          orientedMarginLeft,
          orientedMarginTop
      );
    }
    if (!allowVisibleTtfRasterize && ExternalFont::consumeRuntimeTtfMissSuppressed()) {
      LOG_DBG("ERS", "Visible render used fallback for one or more TTF glyph misses due to low memory");
    }
#else
    page->render(
        renderer,
        SETTINGS.getReaderFontId(),
        orientedMarginLeft,
        orientedMarginTop
    );
#endif
#if CROSSPOINT_PAPERS3
    const ReaderHeapTrace glyphAfter = captureReaderHeapTrace();
    logReaderHeapDelta("visible-glyph-render", glyphBefore, glyphAfter, millis() - glyphStart);
#endif
  }
  const auto tPageRenderEnd = millis();

  // 固定測試字先使用 BW 模式，避免測試階段受灰階混合影響。
  renderer.setRenderMode(GfxRenderer::BW);
  endReaderContentRender(renderer);
  const auto tEndContentEnd = millis();

  LOG_DBG(
      "ERS",
      "render phase: background=%lums beginContent=%lums modeSet=%lums pageRender=%lums endContent=%lums",
      tBackgroundEnd - tBackgroundStart,
      tBeginContentEnd - tBackgroundEnd,
      tModeSetEnd - tBeginContentEnd,
      tPageRenderEnd - tModeSetEnd,
      tEndContentEnd - tPageRenderEnd
  );

  const auto tVerticalTestStart = millis();
  if (ENABLE_VERTICAL_RENDER_TEST) {
    renderVerticalTest();
  }
  const auto tVerticalTestEnd = millis();
  renderStatusBar();
  const auto tStatusBarEnd = millis();

  LOG_DBG(
      "ERS",
      "render phase: verticalTest=%lums statusBar=%lums",
      tVerticalTestEnd - tVerticalTestStart,
      tStatusBarEnd - tVerticalTestEnd
  );

  fcm->logStats("page_render");

  const auto tRender = millis();

  /*
   * 含圖片且文字開啟 AA 時，沿用原本的兩階段刷新方式：
   *
   * 1. 先清空圖片區域並快速刷新。
   * 2. 再重新畫完整頁面。
   */
  const bool imagePageWithAA =
      currentPageHasImages &&
      ReaderUtils::shouldUseTextAntiAliasingForReader();

  if (lastVisiblePageHadImages && !currentPageHasImages) {
    renderer.requestFullRefresh();
    LOG_DBG("ERS", "Full refresh requested: image page -> text page cache miss");
  }

  if (imagePageWithAA) {
    int16_t imgX = 0;
    int16_t imgY = 0;
    int16_t imgW = 0;
    int16_t imgH = 0;

    if (page->getImageBoundingBox(
            imgX,
            imgY,
            imgW,
            imgH)) {
      renderer.fillRect(
          imgX + orientedMarginLeft,
          imgY + orientedMarginTop,
          imgW,
          imgH,
          SETTINGS.readerContentInvert ? true : false
      );

      // Keep the existing pre-clean step for image pages.  This path is not
      // the visible page-turn effect; the final paint below uses the dedicated
      // page-turn refresh cycle.
      renderer.displayBuffer(
          HalDisplay::FAST_REFRESH
      );
      renderer.waitDisplayIdle();
      LOG_DBG("ERS", "Image pre-clean display idle before final render");

      // 第二次完整繪製。
      renderer.setRenderMode(
          GfxRenderer::GRAYSCALE_DIRECT
      );
      beginReaderContentRender(renderer);

      {
        PageRenderProfiler::Scoped pageRenderProfile(true);
#if CROSSPOINT_PAPERS3
        {
          ScopedTtfRasterizePolicy ttfPolicy(allowVisibleTtfRasterize, allowVisibleTtfRasterize ? "visible-image-second-pass" : "visible-low-memory-image-second-pass");
          page->render(
              renderer,
              SETTINGS.getReaderFontId(),
              orientedMarginLeft,
              orientedMarginTop
          );
        }
        if (!allowVisibleTtfRasterize && ExternalFont::consumeRuntimeTtfMissSuppressed()) {
          LOG_DBG("ERS", "Visible image second-pass used fallback for one or more TTF glyph misses due to low memory");
        }
#else
        page->render(
            renderer,
            SETTINGS.getReaderFontId(),
            orientedMarginLeft,
            orientedMarginTop
        );
#endif
      }

      renderer.setRenderMode(
          GfxRenderer::BW
      );
      endReaderContentRender(renderer);

      // 第二次 page->render() 後，也必須重新畫測試字與狀態列。
      if (ENABLE_VERTICAL_RENDER_TEST) {
        renderVerticalTest();
      }

      renderStatusBar();

      ReaderUtils::displayWithRefreshCycle(
          renderer,
          pagesUntilFullRefresh,
          lastPageTurnWasForward
      );
    } else {
      ReaderUtils::displayWithRefreshCycle(
          renderer,
          pagesUntilFullRefresh,
          lastPageTurnWasForward
      );
    }
  } else {
    ReaderUtils::displayWithRefreshCycle(
        renderer,
        pagesUntilFullRefresh,
        lastPageTurnWasForward
    );
  }

#if CROSSPOINT_PAPERS3
  waitForVisibleDisplayIdle("cache-miss");
  const auto tCacheStoreStart = millis();
  const ReaderHeapTrace visibleCacheStoreBefore = captureReaderHeapTrace();
  if (section) {
    const ReaderMemoryState storeState = updateReaderMemoryState("visible-frame-store");
    prunePageFrameCacheForMemoryState(storeState, "visible-frame-store");
    copyCurrentFrameToPageFrameCache(currentSpineIndex, section->currentPage, currentPageFootnotes, currentPageHasImages);
  }
  const ReaderHeapTrace visibleCacheStoreAfter = captureReaderHeapTrace();
  logReaderHeapDelta("visible-cache-store-total", visibleCacheStoreBefore, visibleCacheStoreAfter, millis() - tCacheStoreStart);
  lastVisiblePageHadImages = currentPageHasImages;
  const auto tCacheStoreEnd = millis();
  LOG_DBG("ERS", "render phase: cacheStore=%lums", tCacheStoreEnd - tCacheStoreStart);
#endif

  const auto tDisplay = millis();
  const auto tEnd = millis();

  LOG_DBG(
      "ERS",
      "Page render: prewarm=%lums render=%lums "
      "display=%lums total=%lums",
      tPrewarm - t0,
      tRender - tPrewarm,
      tDisplay - tRender,
      tEnd - t0
  );

  // 避免未使用參數警告。
  (void)orientedMarginBottom;
}

void EpubReaderActivity::renderStatusBar() const {
  // Calculate progress in book
  const int currentPage = section->currentPage + 1;
  const float pageCount = section->pageCount;
  const float sectionChapterProg = (pageCount > 0) ? (static_cast<float>(currentPage) / pageCount) : 0;
  const float bookProgress = epub->calculateProgress(currentSpineIndex, sectionChapterProg) * 100;

  std::string title;

  int textYOffset = 0;

  if (automaticPageTurnActive) {
    title = tr(STR_AUTO_TURN_ENABLED) + std::to_string(60 * 1000 / pageTurnDuration);

    // calculates textYOffset when rendering title in status bar
    const uint8_t statusBarHeight = UITheme::getInstance().getStatusBarHeight();

    // offsets text if no status bar or progress bar only
    if (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight()) {
      textYOffset += UITheme::getInstance().getMetrics().statusBarVerticalMargin;
    }

  } else if (SETTINGS.statusBarTitle == CrossPointSettings::STATUS_BAR_TITLE::CHAPTER_TITLE) {
    title = tr(STR_UNNAMED);
    const int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
    if (tocIndex != -1) {
      const auto tocItem = epub->getTocItem(tocIndex);
      title = tocItem.title;
    }

  } else if (SETTINGS.statusBarTitle == CrossPointSettings::STATUS_BAR_TITLE::BOOK_TITLE) {
    title = epub->getTitle();
  }

  const int statusPaddingBottom = SETTINGS.statusBarFollowsPageMargin ? SETTINGS.screenMargin : 0;
  const bool previousInvertDrawing = renderer.getInvertDrawing();
  renderer.setInvertDrawing(SETTINGS.readerContentInvert != 0);
  GUI.drawStatusBar(renderer, bookProgress, currentPage, pageCount, title, statusPaddingBottom, textYOffset);
  renderer.setInvertDrawing(previousInvertDrawing);
}

void EpubReaderActivity::navigateToHref(const std::string& hrefStr, const bool savePosition) {
  if (!epub) return;

  // Push current position onto saved stack
  if (savePosition && section && footnoteDepth < MAX_FOOTNOTE_DEPTH) {
    savedPositions[footnoteDepth] = {currentSpineIndex, section->currentPage};
    footnoteDepth++;
    LOG_DBG("ERS", "Saved position [%d]: spine %d, page %d", footnoteDepth, currentSpineIndex, section->currentPage);
  }

  // Extract fragment anchor (e.g. "#note1" or "chapter2.xhtml#note1")
  std::string anchor;
  const auto hashPos = hrefStr.find('#');
  if (hashPos != std::string::npos && hashPos + 1 < hrefStr.size()) {
    anchor = hrefStr.substr(hashPos + 1);
  }

  // Check for same-file anchor reference (#anchor only)
  bool sameFile = !hrefStr.empty() && hrefStr[0] == '#';

  int targetSpineIndex;
  if (sameFile) {
    targetSpineIndex = currentSpineIndex;
  } else {
    targetSpineIndex = epub->resolveHrefToSpineIndex(hrefStr);
  }

  if (targetSpineIndex < 0) {
    LOG_DBG("ERS", "Could not resolve href: %s", hrefStr.c_str());
    if (savePosition && footnoteDepth > 0) footnoteDepth--;  // undo push
    return;
  }

  {
    RenderLock lock(*this);
    pendingAnchor = std::move(anchor);
    currentSpineIndex = targetSpineIndex;
    nextPageNumber = 0;
    section.reset();
#if CROSSPOINT_PAPERS3
  clearPageFrameCache(false);
#endif
  }
  requestUpdate();
  LOG_DBG("ERS", "Navigated to spine %d for href: %s", targetSpineIndex, hrefStr.c_str());
}

void EpubReaderActivity::restoreSavedPosition() {
  if (footnoteDepth <= 0) return;
  footnoteDepth--;
  const auto& pos = savedPositions[footnoteDepth];
  LOG_DBG("ERS", "Restoring position [%d]: spine %d, page %d", footnoteDepth, pos.spineIndex, pos.pageNumber);

  {
    RenderLock lock(*this);
    currentSpineIndex = pos.spineIndex;
    nextPageNumber = pos.pageNumber;
    section.reset();
#if CROSSPOINT_PAPERS3
  clearPageFrameCache(false);
#endif
  }
  requestUpdate();
}
