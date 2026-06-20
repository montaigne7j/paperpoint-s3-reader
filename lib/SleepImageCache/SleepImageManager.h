#pragma once

#include <Arduino.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class HalDisplay;

class SleepImageManager {
 public:
  static SleepImageManager& getInstance();

  // Scan /.sleep (or /sleep) and select the image that will be used for the
  // next custom sleep screen. The expensive conversion is deferred to loop().
  void begin();

  // Start background preparation after the foreground has been idle for a
  // short period. Safe to call every main-loop iteration.
  void loop(uint32_t idleMs, bool foregroundBusy);

  // Called on touch/button activity so a running decoder can yield while the
  // user is interacting with the reader.
  void noteUserActivity();

  // Preserve the last fully rendered reader page before a reader menu is
  // pushed. The snapshot is kept in PSRAM and is used only for transparent PNG
  // overlays when the setting requests Current Reading Page.
  void captureReaderFrame(
      const uint8_t* physicalFrameBuffer,
      uint8_t readerOrientation);

  // Stop any unfinished background job immediately. Incomplete .tmp files are
  // never used as sleep screens.
  void cancelForSleep();

  // Display the prepared image, or the last valid cache. Returns false when no
  // valid custom cache exists, allowing SleepActivity to show the built-in
  // fallback. This function never performs JPG/PNG decoding.
  bool displayPreparedOrPrevious(
      HalDisplay& display,
      bool readerContext,
      bool rotate180,
      uint8_t transparentBackgroundMode);

  bool isPreparing() const { return taskRunning; }

 private:
  SleepImageManager() = default;
  SleepImageManager(const SleepImageManager&) = delete;
  SleepImageManager& operator=(const SleepImageManager&) = delete;

  static void taskTrampoline(void* parameter);
  void prepareTask();

  void scanCandidates();
  bool prepareCandidate(size_t candidateIndex, std::string& cachePathOut);
  bool displayCache(
      HalDisplay& display,
      const std::string& cachePath,
      bool readerContext,
      bool rotate180,
      uint8_t transparentBackgroundMode);

  std::vector<std::string> candidates;
  size_t selectedCandidate = 0;

  volatile bool taskRunning = false;
  volatile bool cancelRequested = false;
  volatile uint32_t lastUserActivityMs = 0;
  TaskHandle_t taskHandle = nullptr;

  bool preparedReady = false;
  bool preparationAttempted = false;
  std::string preparedCachePath;

  uint8_t* readerFrame = nullptr;
  bool readerFrameValid = false;
  uint8_t readerFrameOrientation = 0;

  portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
};

#define SleepImages SleepImageManager::getInstance()
