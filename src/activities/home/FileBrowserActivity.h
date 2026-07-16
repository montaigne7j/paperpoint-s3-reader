#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../Activity.h"
#include "RecentBooksStore.h"
#include "util/ButtonNavigator.h"

class FileBrowserActivity final : public Activity {
 private:
  // Deletion
  void clearFileMetadata(const std::string& fullPath);

  ButtonNavigator buttonNavigator;

  size_t selectorIndex = 0;

  // Tracks what is currently present in the framebuffer so an in-page move
  // can update only the old and new rows. A page change falls back to a full
  // list-page render.
  size_t renderedSelectorIndex = 0;
  bool hasRenderedList = false;
  bool selectionMovePending = false;

  bool lockLongPressBack = false;
  bool comicMode = false;

  // Files state
  std::string basepath = "/";
  std::vector<std::string> files;

  // Data loading
  void loadFiles();
  size_t findEntry(const std::string& name) const;
  void moveSelectionTo(size_t newIndex);
  void openSelectedEntry();
  void navigateToParent();
  bool isAtRoot() const;
  void requestFullPageUpdate(bool immediate = false);

 public:
  explicit FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialPath = "/book",
                               bool comicMode = false)
      : Activity(comicMode ? "ComicFileBrowser" : "FileBrowser", renderer, mappedInput),
        basepath(initialPath.empty() ? "/book" : std::move(initialPath)),
        comicMode(comicMode) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  // Keep File Browser at full speed on Paper S3.  Directory navigation uses
  // SD reads, natural sort, partial EPD redraws, and direct touch hit testing.
  // r26 logs showed instability after this screen entered 80 MHz idle mode.
  bool allowIdlePowerSaving() const override { return false; }
};
