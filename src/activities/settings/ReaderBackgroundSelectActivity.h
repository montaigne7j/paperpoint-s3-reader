#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class ReaderBackgroundSelectActivity final : public Activity {
 public:
  explicit ReaderBackgroundSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReaderBackgroundSelect", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;

  struct BackgroundItem {
    std::string path;
    std::string title;
    std::string subtitle;
  };

 private:
  std::vector<BackgroundItem> items;
  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;

  int totalItems() const;
  void scanBackgroundFiles();
  void applySelection();
};
