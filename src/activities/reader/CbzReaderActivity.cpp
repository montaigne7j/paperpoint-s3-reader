#include "CbzReaderActivity.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <Epub/converters/JpegToFramebufferConverter.h>
#include <Epub/converters/PngToFramebufferConverter.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <ZipFile.h>

#if CROSSPOINT_PAPERS3
#include <esp_heap_caps.h>
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <variant>

#include "ComicReaderMenuActivity.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long goHomeMs = 1000;
constexpr int kStatusBarHeight = 30;
constexpr uint32_t kInvalidPage = 0xFFFFFFFFu;
constexpr size_t kCbzCopyChunkSize = 32 * 1024;

void cooperativeCbzYield() { vTaskDelay(1); }

bool naturalLess(const std::string& a, const std::string& b) {
  const char* s1 = a.c_str();
  const char* s2 = b.c_str();

  while (*s1 && *s2) {
    if (std::isdigit(static_cast<unsigned char>(*s1)) && std::isdigit(static_cast<unsigned char>(*s2))) {
      char* end1;
      char* end2;
      const long n1 = std::strtol(s1, &end1, 10);
      const long n2 = std::strtol(s2, &end2, 10);
      if (n1 != n2) return n1 < n2;
      const int len1 = end1 - s1;
      const int len2 = end2 - s2;
      if (len1 != len2) return len1 < len2;
      s1 = end1;
      s2 = end2;
    } else {
      const char c1 = std::tolower(static_cast<unsigned char>(*s1));
      const char c2 = std::tolower(static_cast<unsigned char>(*s2));
      if (c1 != c2) return c1 < c2;
      ++s1;
      ++s2;
    }
  }

  return *s1 == '\0' && *s2 != '\0';
}

std::string basenameOf(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string extensionOf(const std::string& path) {
  const std::string name = basenameOf(path);
  const size_t dot = name.find_last_of('.');
  if (dot == std::string::npos) return "";
  return name.substr(dot);
}

uint64_t stablePathHash(const std::string& path) { return ZipFile::fnvHash64(path.c_str(), path.size()); }

int fitLength(int srcW, int srcH, int maxW, int maxH, bool widthAxis) {
  if (srcW <= 0 || srcH <= 0 || maxW <= 0 || maxH <= 0) return 0;
  float scaleX = static_cast<float>(maxW) / static_cast<float>(srcW);
  float scaleY = static_cast<float>(maxH) / static_cast<float>(srcH);
  float scale = scaleX < scaleY ? scaleX : scaleY;
  if (scale > 1.0f) scale = 1.0f;
  return static_cast<int>((widthAxis ? srcW : srcH) * scale);
}

}  // namespace

CbzReaderActivity::CbzReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string archivePath)
    : Activity("CbzReader", renderer, mappedInput), archivePath(std::move(archivePath)) {
  cachePath = "/.crosspoint/cbz_" + std::to_string(stablePathHash(this->archivePath));
  tempImagePath = cachePath + "/page.tmp";
}

CbzReaderActivity::~CbzReaderActivity() { clearFrameCache(); }

uint8_t* CbzReaderActivity::allocateFrameBuffer() const {
#if CROSSPOINT_PAPERS3
  return static_cast<uint8_t*>(heap_caps_malloc(GfxRenderer::getBufferSize(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
  return static_cast<uint8_t*>(malloc(GfxRenderer::getBufferSize()));
#endif
}

void CbzReaderActivity::freeFrameBuffer(uint8_t* ptr) const {
  if (!ptr) return;
#if CROSSPOINT_PAPERS3
  heap_caps_free(ptr);
#else
  free(ptr);
#endif
}

bool CbzReaderActivity::ensureFrameCacheBuffer(FrameCache& cache) {
  if (cache.buffer) return true;
  cache.buffer = allocateFrameBuffer();
  if (!cache.buffer) {
    LOG_ERR("CBZ", "Failed to allocate CBZ frame cache (%u bytes)",
            static_cast<unsigned>(GfxRenderer::getBufferSize()));
    return false;
  }
  return true;
}

void CbzReaderActivity::clearFrameCache() {
  if (nextFrameCache.buffer) {
    freeFrameBuffer(nextFrameCache.buffer);
  }
  nextFrameCache.buffer = nullptr;
  nextFrameCache.valid = false;
  nextFrameCache.page = kInvalidPage;
  failedPreloadPage = kInvalidPage;
}

void CbzReaderActivity::setupCacheDir() const {
  if (!Storage.exists("/.crosspoint")) {
    Storage.mkdir("/.crosspoint");
  }
  if (!Storage.exists(cachePath.c_str())) {
    Storage.mkdir(cachePath.c_str());
  }
}

std::string CbzReaderActivity::getTitle() const {
  std::string name = basenameOf(archivePath);
  if (FsHelpers::hasCbzExtension(name)) {
    name.resize(name.length() - 4);
  } else if (FsHelpers::hasZipExtension(name)) {
    name.resize(name.length() - 4);
  }
  return name;
}

std::string CbzReaderActivity::getProgressPath() const { return cachePath + "/progress.bin"; }

bool CbzReaderActivity::isSupportedImageEntry(const std::string& entry) {
  if (entry.empty() || entry.back() == '/') return false;
  if (entry.starts_with("__MACOSX/")) return false;

  const std::string name = basenameOf(entry);
  if (name.empty() || name[0] == '.') return false;

  return FsHelpers::hasJpgExtension(entry) || FsHelpers::hasPngExtension(entry) || FsHelpers::hasBmpExtension(entry);
}

bool CbzReaderActivity::scanArchive() {
  std::vector<std::string> entries;
  ZipFile zip(archivePath);
  if (!zip.listEntryNames(entries)) {
    LOG_ERR("CBZ", "Failed to list zip entries: %s", archivePath.c_str());
    return false;
  }

  imageEntries.clear();
  for (const auto& entry : entries) {
    if (isSupportedImageEntry(entry)) {
      imageEntries.push_back(entry);
    }
  }

  std::sort(imageEntries.begin(), imageEntries.end(), naturalLess);
  LOG_INF("CBZ", "Loaded image zip: %s images=%u", archivePath.c_str(), static_cast<unsigned>(imageEntries.size()));
  return !imageEntries.empty();
}

void CbzReaderActivity::onEnter() {
  Activity::onEnter();

  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
  mappedInput.setTouchOrientation(SETTINGS.orientation);

  setupCacheDir();
  SETTINGS.comicGrayLevels = 1;  // four-level comic mode removed

  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);

  cooperativeCbzYield();
  loaded = scanArchive();
  cooperativeCbzYield();
  loadFailed = !loaded;

  if (loaded) {
    loadProgress();
    if (currentPage >= imageEntries.size()) currentPage = 0;

    APP_STATE.openEpubPath = archivePath;
    APP_STATE.saveToFile();
    RECENT_BOOKS.addBook(archivePath, getTitle(), "CBZ", "");
  }

  // Entering a comic should start with a clean full refresh, then page turns can
  // return to the tuned page-turn waveform / cached display path.
  pagesUntilFullRefresh = 0;
  forceFullRefreshOnNextDisplay = true;
  renderer.requestFullRefresh();
  requestUpdate();
}

void CbzReaderActivity::onExit() {
  Activity::onExit();
  cooperativeCbzYield();
  saveProgress();
  if (!tempImagePath.empty() && Storage.exists(tempImagePath.c_str())) {
    Storage.remove(tempImagePath.c_str());
  }
  const std::string preloadJpg = cachePath + "/preload.jpg";
  if (Storage.exists(preloadJpg.c_str())) Storage.remove(preloadJpg.c_str());
  clearFrameCache();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  mappedInput.setTouchOrientation(CrossPointSettings::PORTRAIT);
}

void CbzReaderActivity::loop() {
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= goHomeMs) {
    activityManager.goToComicFileBrowser(archivePath);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && mappedInput.getHeldTime() < goHomeMs) {
    activityManager.goToComicFileBrowser(archivePath);
    return;
  }

  if (!loaded || imageEntries.empty()) {
    return;
  }

  if (pendingPageDelta != 0) {
    const int delta = pendingPageDelta;
    pendingPageDelta = 0;
    applyPageDelta(delta);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    auto handler = [this](const ActivityResult& result) { handleMenuResult(result); };
    startActivityForResult(
        std::make_unique<ComicReaderMenuActivity>(renderer, mappedInput, getTitle(), static_cast<int>(currentPage + 1),
                                                  static_cast<int>(imageEntries.size())),
        handler);
    return;
  }

  // Comic reading rule: next page is on the left side of the screen.
  // Keep PageForward/Power as "next" for hardware/virtual buttons, but swap
  // the left/right content taps for manga-style reading.
  const bool prevTriggered = mappedInput.wasReleased(MappedInputManager::Button::PageBack) ||
                             mappedInput.wasReleased(MappedInputManager::Button::Right);
  const bool powerPageTurn = SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::PAGE_TURN &&
                             mappedInput.wasReleased(MappedInputManager::Button::Power);
  const bool nextTriggered = mappedInput.wasReleased(MappedInputManager::Button::PageForward) ||
                             mappedInput.wasReleased(MappedInputManager::Button::Left) || powerPageTurn;

  if (prevTriggered) {
    applyPageDelta(-1);
    return;
  }

  if (nextTriggered) {
    applyPageDelta(+1);
    return;
  }

  preloadNextFrame();
}
void CbzReaderActivity::render(RenderLock&&) {
  if (loadFailed || imageEntries.empty()) {
    renderMessage("No images in ZIP/CBZ");
    return;
  }

  if (renderCurrentPageFromCache()) {
    saveProgress();
    return;
  }

  std::string imagePath;
  cooperativeCbzYield();
  if (!extractCurrentPage(imagePath)) {
    renderMessage(tr(STR_PAGE_LOAD_ERROR));
    return;
  }

  cooperativeCbzYield();
  if (!renderExtractedImage(imagePath)) {
    renderMessage(tr(STR_PAGE_LOAD_ERROR));
    return;
  }

  saveProgress();
}

bool CbzReaderActivity::applyPageDelta(const int delta) {
  if (delta == 0 || imageEntries.empty()) return false;

  if (delta < 0) {
    if (currentPage == 0) return false;
    currentPage--;
  } else {
    if (currentPage + 1 >= imageEntries.size()) {
      renderMessage(tr(STR_END_OF_BOOK));
      return false;
    }
    currentPage++;
  }

  failedPreloadPage = kInvalidPage;
  // If the requested page is not the currently prepared next-page cache, drop
  // stale cache.  This keeps previous-page and multi-page jumps correct.
  if (!nextFrameCache.valid || nextFrameCache.page != currentPage) {
    nextFrameCache.valid = false;
    nextFrameCache.page = kInvalidPage;
  }
  requestUpdate();
  return true;
}

void CbzReaderActivity::pollPendingPageInputDuringPreload() {
  mappedInput.update();

  const bool pendingPrev = mappedInput.wasReleased(MappedInputManager::Button::PageBack) ||
                           mappedInput.wasReleased(MappedInputManager::Button::Right) ||
                           mappedInput.isPressed(MappedInputManager::Button::PageBack) ||
                           mappedInput.isPressed(MappedInputManager::Button::Right);

  const bool pendingNext = mappedInput.wasReleased(MappedInputManager::Button::PageForward) ||
                           mappedInput.wasReleased(MappedInputManager::Button::Left) ||
                           mappedInput.isPressed(MappedInputManager::Button::PageForward) ||
                           mappedInput.isPressed(MappedInputManager::Button::Left);

  // Prefer next if both are somehow present; manga reading often uses repeated
  // left-zone taps, and the user's complaint was missed next-page input.
  if (pendingNext) {
    if (pendingPageDelta != +1) {
      LOG_INF("CBZ", "Queued pending next-page input during preload");
    }
    pendingPageDelta = +1;
    pendingDuringPreload = true;
  } else if (pendingPrev) {
    if (pendingPageDelta != -1) {
      LOG_INF("CBZ", "Queued pending previous-page input during preload");
    }
    pendingPageDelta = -1;
    pendingDuringPreload = true;
  }
}

void CbzReaderActivity::handleMenuResult(const ActivityResult& result) {
  if (result.isCancelled) return;

  const auto* menu = std::get_if<MenuResult>(&result.data);
  if (!menu) return;

  if (menu->action == ComicReaderMenuActivity::ACTION_GO_HOME) {
    activityManager.goHome();
    return;
  }

  if (menu->action == ComicReaderMenuActivity::ACTION_SETTINGS_CHANGED) {
    clearFrameCache();
    failedPreloadPage = kInvalidPage;
    pagesUntilFullRefresh = 0;
    forceFullRefreshOnNextDisplay = true;
    renderer.requestFullRefresh();
    requestUpdate();
    return;
  }
}

int CbzReaderActivity::getComicFullRefreshFrequency() const {
  switch (SETTINGS.comicFullRefreshFrequency) {
    case 0:
      return 1;  // every page
    case 1:
      return 2;  // every 2 pages
    case 2:
      return 5;  // every 5 pages
    case 3:
      return 10;  // every 10 pages
    case 4:
    default:
      return 0;  // never
  }
}

void CbzReaderActivity::displayComicPageBuffer() {
  const int refreshFrequency = getComicFullRefreshFrequency();

  if (forceFullRefreshOnNextDisplay) {
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    pagesUntilFullRefresh = refreshFrequency;
    forceFullRefreshOnNextDisplay = false;
    return;
  }

  if (refreshFrequency <= 0) {
    pagesUntilFullRefresh = 0;
    renderer.displayBuffer(ReaderUtils::pageTurnRefreshModeFor(renderer, true));
    return;
  }

  if (pagesUntilFullRefresh <= 1) {
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    pagesUntilFullRefresh = refreshFrequency;
  } else {
    renderer.displayBuffer(ReaderUtils::pageTurnRefreshModeFor(renderer, true));
    pagesUntilFullRefresh--;
  }
}

bool CbzReaderActivity::renderCurrentPageFromCache() {
  if (!nextFrameCache.valid || nextFrameCache.page != currentPage || !nextFrameCache.buffer ||
      nextFrameCache.orientation != static_cast<uint8_t>(renderer.getOrientation())) {
    return false;
  }

  renderer.beginFrame();
  std::memcpy(renderer.getFrameBuffer(), nextFrameCache.buffer, GfxRenderer::getBufferSize());
  displayComicPageBuffer();
  lastDisplayMs = millis();
  LOG_INF("CBZ", "Displayed page %lu from decoded frame cache", static_cast<unsigned long>(currentPage + 1));

  nextFrameCache.valid = false;
  nextFrameCache.page = kInvalidPage;
  return true;
}

bool CbzReaderActivity::extractCurrentPage(std::string& outPath) {
  const std::string ext = extensionOf(imageEntries[currentPage]);
  const std::string targetPath = cachePath + "/page" + ext;
  return extractPageToPath(currentPage, targetPath, outPath);
}

bool CbzReaderActivity::extractPageToPath(uint32_t pageIndex, const std::string& targetPath, std::string& outPath) {
  if (pageIndex >= imageEntries.size()) return false;

  if (Storage.exists(tempImagePath.c_str()) && tempImagePath != targetPath) {
    Storage.remove(tempImagePath.c_str());
  }

  if (Storage.exists(targetPath.c_str())) {
    Storage.remove(targetPath.c_str());
  }

  FsFile out;
  if (!Storage.openFileForWrite("CBZ", targetPath, out)) {
    LOG_ERR("CBZ", "Failed to open temp image: %s", targetPath.c_str());
    return false;
  }

  ZipFile zip(archivePath);
  LOG_INF("CBZ", "Extracting page %lu/%u: %s", static_cast<unsigned long>(pageIndex + 1),
          static_cast<unsigned>(imageEntries.size()), imageEntries[pageIndex].c_str());
  const uint32_t startMs = millis();

  // ZIP_STORED entries are copied directly by ZipFile::readFileToStream().
  // The 32 KB chunk size keeps stored CBZ pages fast while deflated files still
  // fall back to streaming inflate.
  const bool ok = zip.readFileToStream(imageEntries[pageIndex].c_str(), out, kCbzCopyChunkSize);
  out.close();

  if (!ok) {
    LOG_ERR("CBZ", "Failed to extract entry: %s", imageEntries[pageIndex].c_str());
    Storage.remove(targetPath.c_str());
    return false;
  }

  tempImagePath = targetPath;
  outPath = targetPath;
  LOG_INF("CBZ", "Extracted page to: %s (%lu ms)", targetPath.c_str(), static_cast<unsigned long>(millis() - startMs));
  return true;
}

bool CbzReaderActivity::renderExtractedImage(const std::string& imagePath) {
  return renderImageFrame(imagePath, currentPage, true);
}

bool CbzReaderActivity::renderImageFrame(const std::string& imagePath, const uint32_t pageIndex,
                                         const bool displayNow) {
  LOG_INF("CBZ", "Rendering extracted image: %s page=%lu display=%d", imagePath.c_str(),
          static_cast<unsigned long>(pageIndex + 1), displayNow ? 1 : 0);
  const uint32_t decodeStartMs = millis();

  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();

  // Comic CBZ files produced by the Paper S3 converter are already prepared
  // for the panel: normally 540 px wide and <= the available height.  Do not
  // fit-to-page or upscale.  Draw at native size, horizontally centered when
  // narrower than the screen, top-aligned, and leave the remaining area white
  // above the fixed bottom status bar.
  const int statusH = std::max(kStatusBarHeight, UITheme::getStatusBarHeight());
  const int imageH = std::max(1, screenH - statusH);

  renderer.clearScreen();
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_DIRECT);

  bool ok = false;
  auto nativePlacement = [screenW, imageH](const ImageDimensions& dims, RenderConfig& cfg) {
    int dstW = std::max<int>(1, dims.width);
    int dstH = std::max<int>(1, dims.height);

    if (dstW > screenW || dstH > imageH) {
      // Safety fallback for non-converted archives. Converted Paper S3 pages
      // normally do not enter this branch.
      dstW = fitLength(dims.width, dims.height, screenW, imageH, true);
      dstH = fitLength(dims.width, dims.height, screenW, imageH, false);
    }

    cfg.x = std::max(0, (screenW - dstW) / 2);
    cfg.y = 0;
    cfg.maxWidth = dstW;
    cfg.maxHeight = dstH;
    cfg.useExactDimensions = true;
    cfg.useGrayscale = true;
    cfg.useDithering = true;
  };

  if (FsHelpers::hasJpgExtension(imagePath)) {
    ImageDimensions dims{};
    JpegToFramebufferConverter decoder;
    if (decoder.getDimensions(imagePath, dims)) {
      RenderConfig cfg;
      nativePlacement(dims, cfg);
      LOG_DBG("CBZ", "Native JPEG draw %dx%d at %d,%d", dims.width, dims.height, cfg.x, cfg.y);
      ok = decoder.decodeToFramebuffer(imagePath, renderer, cfg);
      cooperativeCbzYield();
    }
  } else if (FsHelpers::hasPngExtension(imagePath)) {
    ImageDimensions dims{};
    PngToFramebufferConverter decoder;
    if (decoder.getDimensions(imagePath, dims)) {
      RenderConfig cfg;
      nativePlacement(dims, cfg);
      LOG_DBG("CBZ", "Native PNG draw %dx%d at %d,%d", dims.width, dims.height, cfg.x, cfg.y);
      ok = decoder.decodeToFramebuffer(imagePath, renderer, cfg);
      cooperativeCbzYield();
    }
  } else if (FsHelpers::hasBmpExtension(imagePath)) {
    FsFile file;
    if (Storage.openFileForRead("CBZ", imagePath, file)) {
      Bitmap bitmap(file, true);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        int dstW = std::max<int>(1, bitmap.getWidth());
        int dstH = std::max<int>(1, bitmap.getHeight());
        if (dstW > screenW || dstH > imageH) {
          dstW = fitLength(bitmap.getWidth(), bitmap.getHeight(), screenW, imageH, true);
          dstH = fitLength(bitmap.getWidth(), bitmap.getHeight(), screenW, imageH, false);
        }
        const int x = std::max(0, (screenW - dstW) / 2);
        renderer.drawBitmap(bitmap, x, 0, dstW, dstH, 0, 0);
        ok = true;
      }
      file.close();
    }
  }

  if (!ok) {
    renderer.setRenderMode(GfxRenderer::BW);
    return false;
  }

  renderer.setRenderMode(GfxRenderer::BW);
  applyComicToneToFramebuffer();
  LOG_INF("CBZ", "Image decode completed for page %lu (%lu ms)", static_cast<unsigned long>(pageIndex + 1),
          static_cast<unsigned long>(millis() - decodeStartMs));
  renderStatusBar(pageIndex);

  if (displayNow) {
    cooperativeCbzYield();
    displayComicPageBuffer();
    lastDisplayMs = millis();
    cooperativeCbzYield();
    LOG_DBG("CBZ", "Rendered page %lu/%u: %s", static_cast<unsigned long>(pageIndex + 1),
            static_cast<unsigned>(imageEntries.size()), imageEntries[pageIndex].c_str());
  }

  return true;
}

void CbzReaderActivity::applyComicToneToFramebuffer() {
  const int enhance = static_cast<int>(SETTINGS.comicGrayEnhanceEncoded) - 50;
  if (enhance == 0) return;

  // Framebuffer values are EPD darkness levels: 0=white, 3=black.
  // Treat "gray enhance" as contrast around the middle gray level. +50 makes
  // dark strokes darker and whites cleaner; -50 softens contrast.
  const int factor = 100 + enhance;  // 50..150
  uint8_t* fb = renderer.getFrameBuffer();
  if (!fb) return;

  for (size_t i = 0; i < GfxRenderer::getBufferSize(); ++i) {
    int v = fb[i];
    if (v > 3) v = 3;
    const int centeredTimes2 = v * 2 - 3;  // -3, -1, +1, +3
    int adjustedTimes2 = 3 + (centeredTimes2 * factor + (centeredTimes2 >= 0 ? 50 : -50)) / 100;
    int adjusted = (adjustedTimes2 + 1) / 2;
    if (adjusted < 0) adjusted = 0;
    if (adjusted > 3) adjusted = 3;
    fb[i] = static_cast<uint8_t>(adjusted);
  }
}

void CbzReaderActivity::preloadNextFrame() {
  if (preloadBusy || imageEntries.empty() || pendingPageDelta != 0) return;

  // r28 could start the hidden next-page preload before the visible current
  // page had finished rendering.  That made the log show page N and N+1 being
  // extracted at the same time and could leave the reader apparently stuck.
  // Only preload after at least one visible page display has completed, and
  // never while the render task is active.
  if (lastDisplayMs == 0) return;
  if (RenderLock::peek()) return;

  // Do not start hidden decoding soon after a page is shown.  This keeps
  // quick follow-up taps responsive.  If the user taps while a preload is
  // already running, preloadFrame() will poll the input once it finishes and
  // queue a pending page turn when possible.
  if (millis() - lastDisplayMs < 1000) return;
  if (mappedInput.wasAnyPressed() || mappedInput.wasAnyReleased() ||
      mappedInput.isPressed(MappedInputManager::Button::Left) ||
      mappedInput.isPressed(MappedInputManager::Button::Right) ||
      mappedInput.isPressed(MappedInputManager::Button::PageForward) ||
      mappedInput.isPressed(MappedInputManager::Button::PageBack)) {
    return;
  }

  const uint32_t targetPage = currentPage + 1;
  if (targetPage >= imageEntries.size()) return;
  if (nextFrameCache.valid && nextFrameCache.page == targetPage &&
      nextFrameCache.orientation == static_cast<uint8_t>(renderer.getOrientation()))
    return;
  if (failedPreloadPage == targetPage) return;

  preloadFrame(targetPage);
}

bool CbzReaderActivity::preloadFrame(const uint32_t pageIndex) {
  if (pageIndex >= imageEntries.size()) return false;
  preloadBusy = true;

  uint8_t* currentFrameBackup = allocateFrameBuffer();
  if (!currentFrameBackup) {
    preloadBusy = false;
    failedPreloadPage = pageIndex;
    return false;
  }

  std::memcpy(currentFrameBackup, renderer.getFrameBuffer(), GfxRenderer::getBufferSize());

  const std::string ext = extensionOf(imageEntries[pageIndex]);
  const std::string preloadPath = cachePath + "/preload" + ext;
  std::string imagePath;
  bool ok = extractPageToPath(pageIndex, preloadPath, imagePath);
  pollPendingPageInputDuringPreload();
  if (ok) {
    ok = renderImageFrame(imagePath, pageIndex, false);
    pollPendingPageInputDuringPreload();
  }

  if (ok && ensureFrameCacheBuffer(nextFrameCache)) {
    std::memcpy(nextFrameCache.buffer, renderer.getFrameBuffer(), GfxRenderer::getBufferSize());
    nextFrameCache.page = pageIndex;
    nextFrameCache.orientation = static_cast<uint8_t>(renderer.getOrientation());
    nextFrameCache.valid = true;
    failedPreloadPage = kInvalidPage;
    LOG_INF("CBZ", "Preloaded decoded frame cache for page %lu", static_cast<unsigned long>(pageIndex + 1));
  } else {
    nextFrameCache.valid = false;
    nextFrameCache.page = kInvalidPage;
    failedPreloadPage = pageIndex;
    LOG_ERR("CBZ", "Failed to preload page %lu", static_cast<unsigned long>(pageIndex + 1));
  }

  // Restore visible current page framebuffer so sleep image / UI transitions do
  // not accidentally capture the hidden preloaded page.
  std::memcpy(renderer.getFrameBuffer(), currentFrameBackup, GfxRenderer::getBufferSize());
  freeFrameBuffer(currentFrameBackup);

  if (Storage.exists(preloadPath.c_str())) {
    Storage.remove(preloadPath.c_str());
  }

  // One final poll after the blocking preload.  If the user tapped or is still
  // holding a page-turn area/button, queue it so it runs immediately after
  // preload ends instead of requiring a second tap.
  pollPendingPageInputDuringPreload();

  preloadBusy = false;

  if (pendingPageDelta != 0) {
    const int delta = pendingPageDelta;
    pendingPageDelta = 0;
    pendingDuringPreload = false;
    LOG_INF("CBZ", "Applying pending page delta %d after preload", delta);
    applyPageDelta(delta);
  }

  return ok;
}

void CbzReaderActivity::renderStatusBar(const uint32_t pageIndex) {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int statusH = std::max(kStatusBarHeight, UITheme::getStatusBarHeight());
  const int y = std::max(0, screenH - statusH);

  renderer.fillRect(0, y, screenW, statusH, false);
  // drawLine end coordinates are inclusive.  Using screenW as x2 causes
  // orientation transform to emit an out-of-range physical coordinate.
  renderer.drawLine(0, y, std::max(0, screenW - 1), y, true);

  const int pageCount = static_cast<int>(imageEntries.size());
  const int page = static_cast<int>(pageIndex) + 1;
  const float bookProgress = pageCount > 0 ? (static_cast<float>(page) / static_cast<float>(pageCount)) * 100.0f : 0.0f;

  std::string title;
  if (SETTINGS.statusBarTitle == CrossPointSettings::STATUS_BAR_TITLE::BOOK_TITLE) {
    title = getTitle();
  } else if (SETTINGS.statusBarTitle == CrossPointSettings::STATUS_BAR_TITLE::CHAPTER_TITLE &&
             pageIndex < imageEntries.size()) {
    title = basenameOf(imageEntries[pageIndex]);
  }

  // The CBZ image area must not inherit reader screen margins.  Temporarily
  // render the status bar with zero reader margin while preserving the user's
  // status-bar content settings (battery, clock, progress, title, etc.).
  const uint8_t savedScreenMargin = SETTINGS.screenMargin;
  const uint8_t savedStatusBarFollowsPageMargin = SETTINGS.statusBarFollowsPageMargin;
  SETTINGS.screenMargin = 0;
  SETTINGS.statusBarFollowsPageMargin = 0;
  GUI.drawStatusBar(renderer, bookProgress, page, pageCount, title, 0, 0);
  SETTINGS.screenMargin = savedScreenMargin;
  SETTINGS.statusBarFollowsPageMargin = savedStatusBarFollowsPageMargin;
}

void CbzReaderActivity::renderMessage(const char* message) {
  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() / 2, message, true, EpdFontFamily::BOLD);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void CbzReaderActivity::saveProgress() const {
  FsFile f;
  if (Storage.openFileForWrite("CBZ", getProgressPath(), f)) {
    uint8_t data[4];
    data[0] = currentPage & 0xFF;
    data[1] = (currentPage >> 8) & 0xFF;
    data[2] = (currentPage >> 16) & 0xFF;
    data[3] = (currentPage >> 24) & 0xFF;
    f.write(data, sizeof(data));
    f.close();
  }
}

void CbzReaderActivity::loadProgress() {
  FsFile f;
  if (Storage.openFileForRead("CBZ", getProgressPath(), f)) {
    uint8_t data[4];
    if (f.read(data, sizeof(data)) == sizeof(data)) {
      currentPage = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
    }
    f.close();
  }
}
