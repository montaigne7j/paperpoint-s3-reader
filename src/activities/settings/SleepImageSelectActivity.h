#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Two-level custom sleep image selector.
 *
 * Root:
 *   Random
 *   /.sleep/
 *   /cover/
 *
 * Folder:
 *   sub-folders and supported BMP/JPG/PNG images
 *
 * Selecting the same image a second time opens a preview.  Preview Confirm or
 * Cancel both return to the folder list; Confirm also saves the image.
 */
class SleepImageSelectActivity final : public Activity {
 public:
  explicit SleepImageSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("SleepImageSelect", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;
  bool allowIdlePowerSaving() const override { return false; }

 private:
  enum class ViewMode { Root, Folder, Preview };

  struct Item {
    std::string path;
    std::string title;
    bool directory = false;
    bool random = false;
  };

  ViewMode viewMode = ViewMode::Root;
  std::vector<Item> items;
  std::vector<std::string> directoryStack;
  std::string previewPath;
  bool forceFullListRefresh = false;
  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;

  void showRoot();
  void enterDirectory(const std::string& path);
  void scanCurrentDirectory();
  void goBackOneLevel();
  void activateSelection();
  void chooseRandom();
  void openPreview(const std::string& path);
  void confirmPreview();
  void cancelPreview();

  void renderList();
  void renderPreview();
  bool drawPreviewImage(const std::string& path, int x, int y, int width, int height);

  [[nodiscard]] std::string currentDirectory() const;
  [[nodiscard]] int totalItems() const { return static_cast<int>(items.size()); }
};
