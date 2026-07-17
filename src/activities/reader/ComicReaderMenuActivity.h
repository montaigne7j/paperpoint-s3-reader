#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "MappedInputManager.h"
#include "util/ButtonNavigator.h"

class ComicReaderMenuActivity final : public Activity {
 public:
  enum Action {
    ACTION_NONE = -1,
    ACTION_GO_HOME = 1,
    ACTION_SETTINGS_CHANGED = 2,
  };

  ComicReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title, int currentPage,
                          int totalPages);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return false; }
  bool allowIdlePowerSaving() const override { return false; }

 private:
  enum Item {
    ITEM_GO_HOME = 0,
    ITEM_GRAY_ENHANCE = 1,
    ITEM_FULL_REFRESH = 2,
    ITEM_COUNT = 3,
  };

  std::string title;
  int currentPage = 0;
  int totalPages = 0;
  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;
  bool settingsChanged = false;

  const char* itemLabel(int index) const;
  std::string itemValue(int index) const;
  void activateSelected();
  void finishWithAction(Action action);
};
