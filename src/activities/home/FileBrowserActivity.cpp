#include "FileBrowserActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "../util/ConfirmationActivity.h"
#include "../util/DirectTouchSelection.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "resources/BuiltinManualEpub.h"

namespace {
constexpr unsigned long GO_HOME_MS = 1000;
}  // namespace

void sortFileList(std::vector<std::string>& strs) {
  std::sort(begin(strs), end(strs), [](const std::string& str1, const std::string& str2) {
    // Directories first
    bool isDir1 = str1.back() == '/';
    bool isDir2 = str2.back() == '/';
    if (isDir1 != isDir2) return isDir1;

    // Start naive natural sort
    const char* s1 = str1.c_str();
    const char* s2 = str2.c_str();

    // Iterate while both strings have characters
    while (*s1 && *s2) {
      // Check if both are at the start of a number
      if (isdigit(*s1) && isdigit(*s2)) {
        // Skip leading zeros and track them
        const char* start1 = s1;
        const char* start2 = s2;
        while (*s1 == '0') s1++;
        while (*s2 == '0') s2++;

        // Count digits to compare lengths first
        int len1 = 0, len2 = 0;
        while (isdigit(s1[len1])) len1++;
        while (isdigit(s2[len2])) len2++;

        // Different length so return smaller integer value
        if (len1 != len2) return len1 < len2;

        // Same length so compare digit by digit
        for (int i = 0; i < len1; i++) {
          if (s1[i] != s2[i]) return s1[i] < s2[i];
        }

        // Numbers equal so advance pointers
        s1 += len1;
        s2 += len2;
      } else {
        // Regular case-insensitive character comparison
        char c1 = tolower(*s1);
        char c2 = tolower(*s2);
        if (c1 != c2) return c1 < c2;
        s1++;
        s2++;
      }
    }

    // One string is prefix of other
    return *s1 == '\0' && *s2 != '\0';
  });
}

void FileBrowserActivity::loadFiles() {
  files.clear();

  if (basepath == "/book" || basepath == "/book/") {
    BuiltinManualEpub::ensureInstalled();
  }

  auto root = Storage.open(basepath.c_str());
  if (!root || !root.isDirectory()) {
    return;
  }

  root.rewindDirectory();

  char name[500];
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(name, sizeof(name));
    if ((!SETTINGS.showHiddenFiles && name[0] == '.') || strcmp(name, "System Volume Information") == 0) {
      continue;
    }

    if (file.isDirectory()) {
      files.emplace_back(std::string(name) + "/");
    } else {
      std::string_view filename{name};
      if (FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename) ||
          FsHelpers::hasTxtExtension(filename) || FsHelpers::hasMarkdownExtension(filename) ||
          FsHelpers::hasBmpExtension(filename)) {
        files.emplace_back(filename);
      }
    }
  }
  sortFileList(files);
}

void FileBrowserActivity::moveSelectionTo(const size_t newIndex) {
  if (files.empty() || newIndex >= files.size() || newIndex == selectorIndex) {
    return;
  }

  selectorIndex = newIndex;
  selectionMovePending = true;
  requestUpdate();
}

void FileBrowserActivity::requestFullPageUpdate(const bool immediate) {
  selectionMovePending = false;
  requestUpdate(immediate);
}

void FileBrowserActivity::openSelectedEntry() {
  if (files.empty() || selectorIndex >= files.size()) return;

  const std::string& entry = files[selectorIndex];
  const bool isDirectory = (entry.back() == '/');
  if (basepath.back() != '/') basepath += "/";

  if (isDirectory) {
    basepath += entry.substr(0, entry.length() - 1);
    loadFiles();
    selectorIndex = 0;
    requestFullPageUpdate();
  } else {
    onSelectBook(basepath + entry);
  }
}

void FileBrowserActivity::onEnter() {
  Activity::onEnter();

  selectorIndex = 0;

  auto root = Storage.open(basepath.c_str());
  if (!root) {
    basepath = "/";
    loadFiles();
  } else if (!root.isDirectory()) {
    lockLongPressBack = mappedInput.isPressed(MappedInputManager::Button::Back);

    const std::string oldPath = basepath;
    basepath = FsHelpers::extractFolderPath(basepath);
    loadFiles();

    const auto pos = oldPath.find_last_of('/');
    const std::string fileName = oldPath.substr(pos + 1);
    selectorIndex = findEntry(fileName);
  } else {
    loadFiles();
  }

  hasRenderedList = false;
  selectionMovePending = false;
  requestUpdate();
}

void FileBrowserActivity::onExit() {
  Activity::onExit();
  files.clear();
}

void FileBrowserActivity::clearFileMetadata(const std::string& fullPath) {
  // Only clear cache for .epub files
  if (FsHelpers::hasEpubExtension(fullPath)) {
    Epub(fullPath, "/.crosspoint").clearCache();
    LOG_DBG("FileBrowser", "Cleared metadata cache for: %s", fullPath.c_str());
  }
}

void FileBrowserActivity::loop() {
  // Long press BACK (1s+) goes to root folder
  // but Long press BACK (1s+) from ReaderActivity sends us here with the MappedInput already set.
  // So ignore it the first time.
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= GO_HOME_MS &&
      basepath != "/" && !lockLongPressBack) {
    basepath = "/";
    loadFiles();
    selectorIndex = 0;
    requestFullPageUpdate();
    return;
  }

  if (lockLongPressBack && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    lockLongPressBack = false;
    return;
  }

  const int pathReserved = renderer.getLineHeight(UI_10_FONT_ID) + UITheme::getInstance().getMetrics().verticalSpacing;
  const int pageItems =
      UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, false, pathReserved);

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (files.empty()) return;
    const std::string& entry = files[selectorIndex];
    bool isDirectory = (entry.back() == '/');

    if (mappedInput.getHeldTime() >= GO_HOME_MS && !isDirectory) {
      // --- LONG PRESS ACTION: DELETE FILE ---
      std::string cleanBasePath = basepath;
      if (cleanBasePath.back() != '/') cleanBasePath += "/";
      const std::string fullPath = cleanBasePath + entry;

      auto handler = [this, fullPath](const ActivityResult& res) {
        if (!res.isCancelled) {
          LOG_DBG("FileBrowser", "Attempting to delete: %s", fullPath.c_str());
          clearFileMetadata(fullPath);
          if (Storage.remove(fullPath.c_str())) {
            LOG_DBG("FileBrowser", "Deleted successfully");
            loadFiles();
            if (files.empty()) {
              selectorIndex = 0;
            } else if (selectorIndex >= files.size()) {
              // Move selection to the new "last" item
              selectorIndex = files.size() - 1;
            }

            requestFullPageUpdate(true);
          } else {
            LOG_ERR("FileBrowser", "Failed to delete file: %s", fullPath.c_str());
          }
        } else {
          LOG_DBG("FileBrowser", "Delete cancelled by user");
        }
      };

      std::string heading = tr(STR_DELETE) + std::string("? ");

      startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, entry), handler);
      return;
    } else {
      // --- SHORT PRESS ACTION: OPEN/NAVIGATE ---
      openSelectedEntry();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // Short press: go up one directory, or go home if at root
    if (mappedInput.getHeldTime() < GO_HOME_MS) {
      if (basepath != "/") {
        const std::string oldPath = basepath;

        basepath.replace(basepath.find_last_of('/'), std::string::npos, "");
        if (basepath.empty()) basepath = "/";
        loadFiles();

        const auto pos = oldPath.find_last_of('/');
        const std::string dirName = oldPath.substr(pos + 1) + "/";
        selectorIndex = findEntry(dirName);

        requestFullPageUpdate();
        return;
      } else {
        onGoHome();
        return;
      }
    }
  }

  int listSize = static_cast<int>(files.size());
#if CROSSPOINT_PAPERS3
  {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const bool largeTextTheme = SETTINGS.uiTheme == CrossPointSettings::UI_THEME::LARGE_TEXT;
    constexpr int largeTextScale = 2;
    const int pathLineHeight =
        largeTextTheme ? renderer.getLineHeightScaled(UI_10_FONT_ID, largeTextScale) : renderer.getLineHeight(UI_10_FONT_ID);
    const int pathReserved = pathLineHeight + metrics.verticalSpacing;
    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight = renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight -
                              metrics.verticalSpacing - pathReserved;
    const int targetIndex = DirectTouchSelection::hitListRow(
        mappedInput, Rect{0, contentTop, renderer.getScreenWidth(), contentHeight}, listSize,
        static_cast<int>(selectorIndex), metrics.listRowHeight);
    if (targetIndex >= 0) {
      if (targetIndex == static_cast<int>(selectorIndex)) {
        openSelectedEntry();
      } else {
        moveSelectionTo(static_cast<size_t>(targetIndex));
      }
      return;
    }
  }
  buttonNavigator.onNextRelease([this, listSize, pageItems] {
    moveSelectionTo(ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems));
  });

  buttonNavigator.onPreviousRelease([this, listSize, pageItems] {
    moveSelectionTo(ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems));
  });
#else
  buttonNavigator.onNextRelease([this, listSize] {
    moveSelectionTo(ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize));
  });

  buttonNavigator.onPreviousRelease([this, listSize] {
    moveSelectionTo(ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize));
  });

  buttonNavigator.onNextContinuous([this, listSize, pageItems] {
    moveSelectionTo(ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems));
  });

  buttonNavigator.onPreviousContinuous([this, listSize, pageItems] {
    moveSelectionTo(ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems));
  });
#endif
}

std::string getFileName(std::string filename) {
  if (filename.back() == '/') {
    filename.pop_back();
    if (SETTINGS.uiTheme == CrossPointSettings::UI_THEME::CLASSIC) {
      return "[" + filename + "]";
    }
    return filename;
  }
  const auto pos = filename.rfind('.');
  return filename.substr(0, pos);
}

std::string getFileExtension(std::string filename) {
  if (filename.back() == '/') {
    return "";
  }
  const auto pos = filename.rfind('.');
  return filename.substr(pos);
}

void FileBrowserActivity::render(RenderLock&&) {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const bool largeTextTheme = SETTINGS.uiTheme == CrossPointSettings::UI_THEME::LARGE_TEXT;
  constexpr int largeTextScale = 2;

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int pathLineHeight =
      largeTextTheme ? renderer.getLineHeightScaled(UI_10_FONT_ID, largeTextScale) : renderer.getLineHeight(UI_10_FONT_ID);
  const int pathReserved = pathLineHeight + metrics.verticalSpacing;
  const int contentHeight =
      pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing - pathReserved;
  const Rect listRect{0, contentTop, pageWidth, contentHeight};
  const int pageItems = UITheme::getInstance().getNumberOfItemsPerPage(
      renderer, true, false, true, false, pathReserved);

  const size_t targetSelectorIndex = selectorIndex;
  const bool validSelection = !files.empty() && targetSelectorIndex < files.size();
  const bool validRenderedSelection = hasRenderedList && !files.empty() && renderedSelectorIndex < files.size();
  const bool samePage = validSelection && validRenderedSelection && pageItems > 0 &&
                        targetSelectorIndex / pageItems == renderedSelectorIndex / pageItems;
  const bool partialSelectionUpdate = selectionMovePending && samePage;
  const bool crossedPage = selectionMovePending && validSelection && validRenderedSelection && pageItems > 0 &&
                           targetSelectorIndex / pageItems != renderedSelectorIndex / pageItems;

  if (partialSelectionUpdate) {
    renderer.beginFrame();
    GUI.redrawListSelection(
        renderer, listRect, static_cast<int>(files.size()), static_cast<int>(renderedSelectorIndex),
        static_cast<int>(targetSelectorIndex), [this](int index) { return getFileName(files[index]); }, nullptr,
        [this](int index) { return UITheme::getFileIcon(files[index]); },
        [this](int index) { return getFileExtension(files[index]); }, false);

    LOG_DBG("FileBrowser", "Partial row update: %u -> %u (page %u)",
            static_cast<unsigned>(renderedSelectorIndex), static_cast<unsigned>(targetSelectorIndex),
            static_cast<unsigned>(targetSelectorIndex / pageItems));
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  } else {
    renderer.clearScreen();

    std::string folderName = (basepath == "/") ? tr(STR_SD_CARD) : basepath.substr(basepath.rfind('/') + 1);
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, folderName.c_str());

    if (files.empty()) {
      if (largeTextTheme) {
        renderer.drawTextScaled(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_FILES_FOUND),
                                largeTextScale);
      } else {
        renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_FILES_FOUND));
      }
    } else {
      GUI.drawList(
          renderer, listRect, files.size(), targetSelectorIndex,
          [this](int index) { return getFileName(files[index]); }, nullptr,
          [this](int index) { return UITheme::getFileIcon(files[index]); },
          [this](int index) { return getFileExtension(files[index]); }, false);
    }

    // Full path display
    {
      const int pathY = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - pathLineHeight;
      const int separatorY = pathY - metrics.verticalSpacing / 2;
      renderer.drawLine(0, separatorY, pageWidth - 1, separatorY, 3, true);

      const int pathMaxWidth = pageWidth - metrics.contentSidePadding * 2;
      const char* pathStr = basepath.c_str();
      const char* pathDisplay = pathStr;
      char leftTruncBuf[256];
      auto pathWidth = [&](const char* text) {
        return largeTextTheme ? renderer.getTextWidthScaled(UI_10_FONT_ID, text, largeTextScale)
                              : renderer.getTextWidth(UI_10_FONT_ID, text);
      };
      if (pathWidth(pathStr) > pathMaxWidth) {
        const char ellipsis[] = "\xe2\x80\xa6";
        const int ellipsisWidth = pathWidth(ellipsis);
        const int available = pathMaxWidth - ellipsisWidth;
        const char* p = pathStr;
        while (*p) {
          if (pathWidth(p) <= available) break;
          ++p;
          while (*p && (static_cast<unsigned char>(*p) & 0xC0) == 0x80) ++p;
        }
        std::snprintf(leftTruncBuf, sizeof(leftTruncBuf), "%s%s", ellipsis, p);
        pathDisplay = leftTruncBuf;
      }
      if (largeTextTheme) {
        renderer.drawTextScaled(UI_10_FONT_ID, metrics.contentSidePadding, pathY, pathDisplay, largeTextScale);
      } else {
        renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, pathY, pathDisplay);
      }
    }

    // Help text: show footer page buttons only when another page exists.
    const int listSize = static_cast<int>(files.size());
    const char* prevPageLabel = (!files.empty() && ButtonNavigator::hasPreviousPage(static_cast<int>(selectorIndex), listSize, pageItems))
                                    ? tr(STR_DIR_UP)
                                    : "";
    const char* nextPageLabel = (!files.empty() && ButtonNavigator::hasNextPage(static_cast<int>(selectorIndex), listSize, pageItems))
                                    ? tr(STR_DIR_DOWN)
                                    : "";
    const auto labels =
        mappedInput.mapLabels(basepath == "/" ? tr(STR_HOME) : tr(STR_BACK), files.empty() ? "" : tr(STR_OPEN),
                              prevPageLabel, nextPageLabel);
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

    if (crossedPage) {
      LOG_DBG("FileBrowser", "Page change: %u -> %u, full screen refresh",
              static_cast<unsigned>(renderedSelectorIndex / pageItems),
              static_cast<unsigned>(targetSelectorIndex / pageItems));
      renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    } else {
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    }
  }

  renderedSelectorIndex = targetSelectorIndex;
  hasRenderedList = true;
  // If another input arrived while this render was running, preserve the pending
  // flag so the next render can reconcile from the row that is now on screen.
  selectionMovePending = selectorIndex != targetSelectorIndex;
}

size_t FileBrowserActivity::findEntry(const std::string& name) const {
  for (size_t i = 0; i < files.size(); i++)
    if (files[i] == name) return i;
  return 0;
}
