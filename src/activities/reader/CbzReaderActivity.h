#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../Activity.h"
#include "MappedInputManager.h"

class CbzReaderActivity final : public Activity {
 public:
  CbzReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string archivePath);
  ~CbzReaderActivity() override;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  // Keep CBZ image reading at full speed. Large ZIP extraction and image
  // decoding are cooperative; do not enter the 80 MHz idle path while this
  // activity is current. Auto-sleep still works because preventAutoSleep()
  // remains false.
  bool skipLoopDelay() override { return true; }

 private:
  struct FrameCache {
    uint32_t page = 0xFFFFFFFFu;
    uint8_t* buffer = nullptr;
    uint8_t orientation = 0;
    bool valid = false;
  };

  std::string archivePath;
  std::string cachePath;
  std::string tempImagePath;
  std::vector<std::string> imageEntries;
  uint32_t currentPage = 0;
  int pagesUntilFullRefresh = 0;
  bool loaded = false;
  bool loadFailed = false;
  bool preloadBusy = false;
  uint32_t failedPreloadPage = 0xFFFFFFFFu;
  uint32_t lastDisplayMs = 0;
  int8_t pendingPageDelta = 0;
  bool pendingDuringPreload = false;
  bool forceFullRefreshOnNextDisplay = true;
  FrameCache nextFrameCache;

  bool scanArchive();
  bool extractCurrentPage(std::string& outPath);
  bool extractPageToPath(uint32_t pageIndex, const std::string& targetPath, std::string& outPath);
  bool renderExtractedImage(const std::string& imagePath);
  bool renderImageFrame(const std::string& imagePath, uint32_t pageIndex, bool displayNow);
  bool renderCurrentPageFromCache();
  int getComicFullRefreshFrequency() const;
  void displayComicPageBuffer();
  void preloadNextFrame();
  bool preloadFrame(uint32_t pageIndex);
  bool applyPageDelta(int delta);
  void pollPendingPageInputDuringPreload();
  void handleMenuResult(const ActivityResult& result);
  void applyComicToneToFramebuffer();
  bool ensureFrameCacheBuffer(FrameCache& cache);
  uint8_t* allocateFrameBuffer() const;
  void freeFrameBuffer(uint8_t* ptr) const;
  void clearFrameCache();
  void renderStatusBar(uint32_t pageIndex);
  void renderMessage(const char* message);
  void saveProgress() const;
  void loadProgress();
  void setupCacheDir() const;
  std::string getTitle() const;
  std::string getProgressPath() const;
  static bool isSupportedImageEntry(const std::string& entry);
};
