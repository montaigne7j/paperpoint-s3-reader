#pragma once

#include "MappedInputManager.h"

#include <algorithm>

#include "components/themes/BaseTheme.h"

namespace DirectTouchSelection {

inline bool getDirectTap(const MappedInputManager& input, int& x, int& y) {
  if (input.wasContentTapReleased()) {
    x = input.getContentTapX();
    y = input.getContentTapY();
    return true;
  }
#if CROSSPOINT_PAPERS3
  // Some screens, such as Home, hide the footer entirely. In that mode the
  // normal reader-style tap recognizer is active, so expose it as a direct tap
  // with raw logical coordinates.
  if (input.wasTapped()) {
    x = input.getTouchX();
    y = input.getTouchY();
    return true;
  }
#endif
  return false;
}

inline int hitListRow(const MappedInputManager& input, const Rect& rect, const int itemCount,
                      const int selectedIndex, const int rowHeight) {
  if (itemCount <= 0 || rowHeight <= 0 || selectedIndex < 0) {
    return -1;
  }

  int x = 0;
  int y = 0;
  if (!getDirectTap(input, x, y)) {
    return -1;
  }
  if (x < rect.x || x >= rect.x + rect.width || y < rect.y || y >= rect.y + rect.height) {
    return -1;
  }

  const int pageItems = std::max(1, rect.height / rowHeight);
  const int pageStartIndex = selectedIndex / pageItems * pageItems;
  const int row = (y - rect.y) / rowHeight;
  if (row < 0 || row >= pageItems) {
    return -1;
  }

  const int targetIndex = pageStartIndex + row;
  return targetIndex >= 0 && targetIndex < itemCount ? targetIndex : -1;
}

inline int hitButtonMenuRow(const MappedInputManager& input, const Rect& rect, const int itemCount,
                            const int rowHeight, const int rowSpacing, const int topOffset = 0) {
  if (itemCount <= 0 || rowHeight <= 0) {
    return -1;
  }

  int x = 0;
  int y = 0;
  if (!getDirectTap(input, x, y)) {
    return -1;
  }
  if (x < rect.x || x >= rect.x + rect.width || y < rect.y || y >= rect.y + rect.height) {
    return -1;
  }

  const int step = rowHeight + rowSpacing;
  if (step <= 0) {
    return -1;
  }

  const int relY = y - rect.y - topOffset;
  if (relY < 0) {
    return -1;
  }

  const int row = relY / step;
  const int within = relY % step;
  if (row < 0 || row >= itemCount || within >= rowHeight) {
    return -1;
  }
  return row;
}

}  // namespace DirectTouchSelection
