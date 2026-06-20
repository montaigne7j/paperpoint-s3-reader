#pragma once

#include <GfxRenderer.h>

#include <string>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

/** Selects the reader-body font from /fonts (.bin, .epdf or .ttf). */
class ReaderFontSelectActivity final : public Activity {
 public:
  explicit ReaderFontSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReaderFontSelect", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void applySelection();
  int totalItems() const;
  std::string itemTitle(int index) const;
  std::string itemSubtitle(int index) const;

  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;  // 0=built-in, n+1=FontManager index n
};
