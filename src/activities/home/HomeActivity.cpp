#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Utf8.h>
#include <Xtc.h>

#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "activities/util/DirectTouchSelection.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

int HomeActivity::getMenuItemCount() const {
  int count = 4;  // File Browser, Recents, File transfer, Settings
  if (!recentBooks.empty()) {
    count += recentBooks.size();
  }
  if (hasOpdsUrl) {
    count++;
  }
  return count;
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (!Storage.exists(book.path.c_str())) {
      continue;
    }

    recentBooks.push_back(book);
  }
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    if (!book.coverBmpPath.empty()) {
      std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      if (!Storage.exists(coverPath.c_str())) {
        // If epub, try to load the metadata for title/author and cover
        if (FsHelpers::hasEpubExtension(book.path)) {
          Epub epub(book.path, "/.crosspoint");
          // Skip loading css since we only need metadata here
          epub.load(false, true);

          // Try to generate thumbnail image for Continue Reading card
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
          bool success = epub.generateThumbBmp(coverHeight);
          if (!success) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
            book.coverBmpPath = "";
          }
          coverRendered = false;
          requestUpdate();
        } else if (FsHelpers::hasXtcExtension(book.path)) {
          // Handle XTC file
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            // Try to generate thumbnail image for Continue Reading card
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
            bool success = xtc.generateThumbBmp(coverHeight);
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
            coverRendered = false;
            requestUpdate();
          }
        }
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;
#if CROSSPOINT_PAPERS3
  if (showingLoading) {
    renderer.requestFullRefresh();
  }
#endif
}

void HomeActivity::onEnter() {
  Activity::onEnter();
#if CROSSPOINT_PAPERS3
  mappedInput.setFooterHeight(0);
#endif

  // Check if OPDS browser URL is configured
  hasOpdsUrl = strlen(SETTINGS.opdsServerUrl) > 0;

  selectorIndex = 0;

  const auto& metrics = UITheme::getInstance().getMetrics();
  loadRecentBooks(metrics.homeRecentBooksCount);

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();

  // Free the stored cover buffer if any
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  // Free any existing buffer first
  freeCoverBuffer();

  const size_t bufferSize = GfxRenderer::getBufferSize();
  coverBuffer = static_cast<uint8_t*>(malloc(bufferSize));
  if (!coverBuffer) {
    return false;
  }

  memcpy(coverBuffer, frameBuffer, bufferSize);
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer) {
    return false;
  }

  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  const size_t bufferSize = GfxRenderer::getBufferSize();
  memcpy(frameBuffer, coverBuffer, bufferSize);
  return true;
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferStored = false;
}

void HomeActivity::activateSelectedItem() {
  // Execute action for current selectorIndex
  int idx = 0;
  int menuSelectedIndex = selectorIndex - static_cast<int>(recentBooks.size());
  const int fileBrowserIdx = idx++;
  const int recentsIdx = idx++;
  const int opdsLibraryIdx = hasOpdsUrl ? idx++ : -1;
  const int fileTransferIdx = idx++;
  const int settingsIdx = idx;

  if (selectorIndex >= 0 && selectorIndex < static_cast<int>(recentBooks.size())) {
    onSelectBook(recentBooks[selectorIndex].path);
  } else if (menuSelectedIndex == fileBrowserIdx) {
    onFileBrowserOpen();
  } else if (menuSelectedIndex == recentsIdx) {
    onRecentsOpen();
  } else if (menuSelectedIndex == opdsLibraryIdx) {
    onOpdsBrowserOpen();
  } else if (menuSelectedIndex == fileTransferIdx) {
    onFileTransferOpen();
  } else if (menuSelectedIndex == settingsIdx) {
    onSettingsOpen();
  }
}

void HomeActivity::loop() {
  const int menuCount = getMenuItemCount();

#if CROSSPOINT_PAPERS3
  // Home is now a pure touch screen: keep the on-screen footer hidden and do
  // not use the four footer buttons here.
  mappedInput.setFooterHeight(0);
  {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int pageWidth = renderer.getScreenWidth();
    const int pageHeight = renderer.getScreenHeight();

    // Direct Touch Selection: top Continue Reading card. Home has no footer,
    // so use the normal reader-style single tap coordinates here.
    const bool hasTap = mappedInput.wasContentTapReleased() || mappedInput.wasTapped();
    const int tapX = mappedInput.wasContentTapReleased() ? mappedInput.getContentTapX() : mappedInput.getTouchX();
    const int tapY = mappedInput.wasContentTapReleased() ? mappedInput.getContentTapY() : mappedInput.getTouchY();
    if (!recentBooks.empty() && hasTap) {
      const Rect recentRect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight};
      if (tapX >= recentRect.x && tapX < recentRect.x + recentRect.width &&
          tapY >= recentRect.y && tapY < recentRect.y + recentRect.height) {
        if (selectorIndex == 0) {
          activateSelectedItem();
        } else {
          selectorIndex = 0;
          requestUpdate();
        }
        return;
      }
    }

    const int visibleMenuItems = menuCount - static_cast<int>(recentBooks.size());
    const Rect menuRect{0, metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.verticalSpacing, pageWidth,
                        pageHeight - (metrics.headerHeight + metrics.homeTopPadding + metrics.verticalSpacing * 2)};
    const int topOffset = SETTINGS.uiTheme == CrossPointSettings::UI_THEME::CLASSIC ? metrics.verticalSpacing : 0;
    const int menuRow = DirectTouchSelection::hitButtonMenuRow(mappedInput, menuRect, visibleMenuItems,
                                                               metrics.menuRowHeight, metrics.menuSpacing, topOffset);
    if (menuRow >= 0) {
      const int targetIndex = static_cast<int>(recentBooks.size()) + menuRow;
      if (targetIndex == selectorIndex) {
        activateSelectedItem();
      } else {
        selectorIndex = targetIndex;
        requestUpdate();
      }
      return;
    }
  }
#else
  buttonNavigator.onNext([this, menuCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, menuCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelectedItem();
  }
#endif
}

void HomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  bool bufferRestored = coverBufferStored && restoreCoverBuffer();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding}, nullptr);

  GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                          recentBooks, selectorIndex, coverRendered, coverBufferStored, bufferRestored,
                          std::bind(&HomeActivity::storeCoverBuffer, this));

  // Build menu items dynamically
  std::vector<const char*> menuItems = {tr(STR_BROWSE_FILES), tr(STR_MENU_RECENT_BOOKS), tr(STR_FILE_TRANSFER),
                                        tr(STR_SETTINGS_TITLE)};
  std::vector<UIIcon> menuIcons = {Folder, Recent, Transfer, Settings};

  if (hasOpdsUrl) {
    // Insert OPDS Browser after File Browser
    menuItems.insert(menuItems.begin() + 2, tr(STR_OPDS_BROWSER));
    menuIcons.insert(menuIcons.begin() + 2, Library);
  }

  GUI.drawButtonMenu(
      renderer,
      Rect{0, metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.verticalSpacing, pageWidth,
           pageHeight - (metrics.headerHeight + metrics.homeTopPadding + metrics.verticalSpacing * 2)},
      static_cast<int>(menuItems.size()), selectorIndex - recentBooks.size(),
      [&menuItems](int index) { return std::string(menuItems[index]); },
      [&menuIcons](int index) { return menuIcons[index]; });

#if !CROSSPOINT_PAPERS3
  const auto labels = mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
#endif

  renderer.displayBuffer();

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    loadRecentCovers(metrics.homeCoverHeight);
  }
}

void HomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onRecentsOpen() { activityManager.goToRecentBooks(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }
