#pragma once

#include <CrossPointSettings.h>
#include <GfxRenderer.h>
#include <Logging.h>

#include "MappedInputManager.h"

namespace ReaderUtils {

constexpr unsigned long GO_HOME_MS = 1000;

// Reader text anti-aliasing is intentionally disabled for inverted reader
// content (black page background / white text).  The normal grayscale AA
// edge pixels become dark-gray halos after inversion, which reduces contrast
// and makes white text look fuzzy on e-ink.  Keep AA enabled for normal
// white-page reading and for non-reader UI paths.
inline bool shouldUseTextAntiAliasingForReader() {
  return SETTINGS.textAntiAliasing && !SETTINGS.readerContentInvert;
}

inline void applyOrientation(GfxRenderer& renderer, const uint8_t orientation) {
  switch (orientation) {
    case CrossPointSettings::ORIENTATION::PORTRAIT:
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
      break;
    case CrossPointSettings::ORIENTATION::INVERTED:
      renderer.setOrientation(GfxRenderer::Orientation::PortraitInverted);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CCW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
      break;
    default:
      break;
  }
}

struct PageTurnResult {
  bool prev;
  bool next;
};

inline PageTurnResult detectPageTurn(
    const MappedInputManager& input,
    const bool reverseHorizontalZones = false
) {
  const bool usePress =
      !SETTINGS.longPressChapterSkip;

  const bool pageBack =
      usePress
          ? input.wasPressed(
                MappedInputManager::Button::PageBack
            )
          : input.wasReleased(
                MappedInputManager::Button::PageBack
            );

  const bool pageForward =
      usePress
          ? input.wasPressed(
                MappedInputManager::Button::PageForward
            )
          : input.wasReleased(
                MappedInputManager::Button::PageForward
            );

  const bool left =
      usePress
          ? input.wasPressed(
                MappedInputManager::Button::Left
            )
          : input.wasReleased(
                MappedInputManager::Button::Left
            );

  const bool right =
      usePress
          ? input.wasPressed(
                MappedInputManager::Button::Right
            )
          : input.wasReleased(
                MappedInputManager::Button::Right
            );

  const bool powerTurn =
      SETTINGS.shortPwrBtn ==
          CrossPointSettings::SHORT_PWRBTN::PAGE_TURN &&
      input.wasReleased(
          MappedInputManager::Button::Power
      );

  // Horizontal touch gestures fire as soon as the finger crosses the threshold,
  // not on release.  V31 request:
  //   horizontal layout: swipe right = next, swipe left = previous
  //   vertical layout:   swipe right = next, swipe left = previous
  // Tap zones keep their reading-layout-specific mapping below.
  const bool swipeLeft = SETTINGS.swipePageTurnEnabled && input.wasPressed(MappedInputManager::Button::SwipeLeft);
  const bool swipeRight = SETTINGS.swipePageTurnEnabled && input.wasPressed(MappedInputManager::Button::SwipeRight);

  /*
   * 橫排：
   *   左 = 上一頁
   *   右 = 下一頁
   *   往左手勢 = 上一頁，往右手勢 = 下一頁
   *
   * 直排：
   *   右 = 上一頁
   *   左 = 下一頁
   *   往左手勢 = 上一頁，往右手勢 = 下一頁
   *
   * PageBack / PageForward 與實體側鍵語意維持不變。
   */
  const bool previous =
      pageBack ||
      (reverseHorizontalZones ? right : left) ||
      swipeLeft;

  const bool next =
      pageForward ||
      powerTurn ||
      (reverseHorizontalZones ? left : right) ||
      swipeRight;

  return {
      previous,
      next
  };
}

inline bool physicalBandReverseForLogicalRightToLeft(const GfxRenderer& renderer) {
  switch (renderer.getOrientation()) {
    case GfxRenderer::Orientation::Portrait:
      // Physical row 0→N maps to logical right→left.
      return false;
    case GfxRenderer::Orientation::PortraitInverted:
      // Physical row 0→N maps to logical left→right.
      return true;
    case GfxRenderer::Orientation::LandscapeClockwise:
    case GfxRenderer::Orientation::LandscapeCounterClockwise:
    default:
      // Landscape row scan is vertical on the logical screen. Keep a stable
      // opposite timing for LTR/RTL requests without mirroring framebuffer rows.
      return false;
  }
}

inline HalDisplay::RefreshMode pageTurnRefreshModeFor(const GfxRenderer& renderer, const bool isForwardTurn) {
  if (SETTINGS.pageTurnRefreshMode == CrossPointSettings::PAGE_TURN_REFRESH_ORIGINAL) {
    return HalDisplay::PAGE_TURN_REFRESH_ORIGINAL;
  }

  // Requested reading-direction semantics:
  //   vertical layout:   next = logical right→left, previous = left→right
  //   horizontal layout: next = logical left→right, previous = right→left
  const bool logicalRightToLeft =
      (SETTINGS.readingLayout == CrossPointSettings::VERTICAL_LAYOUT) ? isForwardTurn : !isForwardTurn;
  bool reversePhysicalBands = physicalBandReverseForLogicalRightToLeft(renderer);
  if (!logicalRightToLeft) {
    reversePhysicalBands = !reversePhysicalBands;
  }
  return reversePhysicalBands ? HalDisplay::PAGE_TURN_REFRESH_REVERSE : HalDisplay::PAGE_TURN_REFRESH;
}

inline void displayWithRefreshCycle(const GfxRenderer& renderer, int& pagesUntilFullRefresh,
                                    const bool isForwardTurn = true) {
  const int refreshFrequency = SETTINGS.getRefreshFrequency();
  const HalDisplay::RefreshMode pageTurnMode = pageTurnRefreshModeFor(renderer, isForwardTurn);
  if (refreshFrequency <= 0) {
    pagesUntilFullRefresh = 0;
    renderer.displayBuffer(pageTurnMode);
    return;
  }

  if (pagesUntilFullRefresh <= 1) {
    // Refresh frequency is a reader-page setting. Keep the full refresh
    // local to reader page turns instead of applying a global render counter
    // that also affects menus and the file browser.
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    pagesUntilFullRefresh = refreshFrequency;
  } else {
    renderer.displayBuffer(pageTurnMode);
    pagesUntilFullRefresh--;
  }
}

// Single-pass grayscale anti-aliasing. Sets GRAYSCALE_DIRECT mode so the
// render callback writes EPD gray values (0-3) directly into the 8bpp
// framebuffer. No LSB/MSB bitplane conversion needed — EPD_Painter accepts
// 0-3 natively. Caller is responsible for displaying the buffer afterwards.
template <typename RenderFn>
void renderAntiAliased(GfxRenderer& renderer, RenderFn&& renderFn) {
  if (!shouldUseTextAntiAliasingForReader()) {
    return;
  }
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_DIRECT);
  renderFn();
  renderer.setRenderMode(GfxRenderer::BW);
}

}  // namespace ReaderUtils
