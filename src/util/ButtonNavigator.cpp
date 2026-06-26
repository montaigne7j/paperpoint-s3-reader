#include "ButtonNavigator.h"
#include <algorithm>

const MappedInputManager* ButtonNavigator::mappedInput = nullptr;

void ButtonNavigator::onNext(const Callback& callback) {
  onNextPress(callback);
  onNextContinuous(callback);
}

void ButtonNavigator::onPrevious(const Callback& callback) {
  onPreviousPress(callback);
  onPreviousContinuous(callback);
}

void ButtonNavigator::onPressAndContinuous(const Buttons& buttons, const Callback& callback) {
  onPress(buttons, callback);
  onContinuous(buttons, callback);
}

void ButtonNavigator::onNextPress(const Callback& callback) { onPress(getNextButtons(), callback); }

void ButtonNavigator::onPreviousPress(const Callback& callback) { onPress(getPreviousButtons(), callback); }

void ButtonNavigator::onNextRelease(const Callback& callback) { onRelease(getNextButtons(), callback); }

void ButtonNavigator::onPreviousRelease(const Callback& callback) { onRelease(getPreviousButtons(), callback); }

void ButtonNavigator::onNextContinuous(const Callback& callback) { onContinuous(getNextButtons(), callback); }

void ButtonNavigator::onPreviousContinuous(const Callback& callback) { onContinuous(getPreviousButtons(), callback); }

void ButtonNavigator::onPress(const Buttons& buttons, const Callback& callback) {
  const bool wasPressed = std::any_of(buttons.begin(), buttons.end(), [](const MappedInputManager::Button button) {
    return mappedInput != nullptr && mappedInput->wasPressed(button);
  });

  if (wasPressed) {
    callback();
  }
}

void ButtonNavigator::onRelease(const Buttons& buttons, const Callback& callback) {
  const bool wasReleased = std::any_of(buttons.begin(), buttons.end(), [](const MappedInputManager::Button button) {
    return mappedInput != nullptr && mappedInput->wasReleased(button);
  });

  if (wasReleased) {
    if (lastContinuousNavTime == 0) {
      callback();
    }

    lastContinuousNavTime = 0;
  }
}

void ButtonNavigator::onContinuous(const Buttons& buttons, const Callback& callback) {
  const bool isPressed = std::any_of(buttons.begin(), buttons.end(), [this](const MappedInputManager::Button button) {
    return mappedInput != nullptr && mappedInput->isPressed(button) && shouldNavigateContinuously();
  });

  if (isPressed) {
    callback();
    lastContinuousNavTime = millis();
  }
}

bool ButtonNavigator::shouldNavigateContinuously() const {
  if (!mappedInput) return false;

  const bool buttonHeldLongEnough = mappedInput->getHeldTime() > continuousStartMs;
  const bool navigationIntervalElapsed = (millis() - lastContinuousNavTime) > continuousIntervalMs;

  return buttonHeldLongEnough && navigationIntervalElapsed;
}

int ButtonNavigator::nextIndex(const int currentIndex, const int totalItems) {
  if (totalItems <= 0) return 0;

  // Calculate the next index with wrap-around
  return (currentIndex + 1) % totalItems;
}

int ButtonNavigator::previousIndex(const int currentIndex, const int totalItems) {
  if (totalItems <= 0) return 0;

  // Calculate the previous index with wrap-around
  return (currentIndex + totalItems - 1) % totalItems;
}


bool ButtonNavigator::hasNextPage(const int currentIndex, const int totalItems, const int itemsPerPage) {
  if (totalItems <= 0 || itemsPerPage <= 0) return false;
  if (totalItems <= itemsPerPage) return false;
  return (currentIndex / itemsPerPage) < ((totalItems - 1) / itemsPerPage);
}

bool ButtonNavigator::hasPreviousPage(const int currentIndex, const int totalItems, const int itemsPerPage) {
  if (totalItems <= 0 || itemsPerPage <= 0) return false;
  if (totalItems <= itemsPerPage) return false;
  return (currentIndex / itemsPerPage) > 0;
}
int ButtonNavigator::nextPageIndex(const int currentIndex, const int totalItems, const int itemsPerPage) {
  if (totalItems <= 0 || itemsPerPage <= 0) return 0;

  // Previous / Next are page-level navigation. If everything fits on the
  // current page, keep the selection where it is; row selection is handled by
  // Direct Touch Selection.
  if (totalItems <= itemsPerPage) return currentIndex;

  const int lastPageIndex = (totalItems - 1) / itemsPerPage;
  const int currentPageIndex = currentIndex / itemsPerPage;
  const int rowInPage = currentIndex % itemsPerPage;
  const int nextPageIndex = (currentPageIndex < lastPageIndex) ? (currentPageIndex + 1) : currentPageIndex;
  const int candidate = nextPageIndex * itemsPerPage + rowInPage;
  const int lastInTargetPage = std::min(totalItems - 1, (nextPageIndex + 1) * itemsPerPage - 1);
  return std::min(candidate, lastInTargetPage);
}

int ButtonNavigator::previousPageIndex(const int currentIndex, const int totalItems, const int itemsPerPage) {
  if (totalItems <= 0 || itemsPerPage <= 0) return 0;

  if (totalItems <= itemsPerPage) return currentIndex;

  const int lastPageIndex = (totalItems - 1) / itemsPerPage;
  const int currentPageIndex = currentIndex / itemsPerPage;
  const int rowInPage = currentIndex % itemsPerPage;
  const int previousPageIndex = (currentPageIndex > 0) ? (currentPageIndex - 1) : currentPageIndex;
  const int candidate = previousPageIndex * itemsPerPage + rowInPage;
  const int lastInTargetPage = std::min(totalItems - 1, (previousPageIndex + 1) * itemsPerPage - 1);
  return std::min(candidate, lastInTargetPage);
}
