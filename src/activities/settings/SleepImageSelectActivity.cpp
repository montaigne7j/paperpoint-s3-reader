#include "SleepImageSelectActivity.h"

#include <Bitmap.h>
#include <Epub/converters/JpegToFramebufferConverter.h>
#include <Epub/converters/PngToFramebufferConverter.h>
#include <FsHelpers.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <SleepImageManager.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/util/DirectTouchSelection.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

bool isSupportedImage(const std::string& filename) {
  return FsHelpers::hasBmpExtension(filename) || FsHelpers::hasJpgExtension(filename) ||
         FsHelpers::hasPngExtension(filename);
}

std::string filenameOf(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

void calculateFit(int sourceWidth, int sourceHeight, int areaWidth, int areaHeight, int& outputWidth,
                  int& outputHeight) {
  if (sourceWidth <= 0 || sourceHeight <= 0 || areaWidth <= 0 || areaHeight <= 0) {
    outputWidth = 1;
    outputHeight = 1;
    return;
  }

  const double scale =
      std::min(static_cast<double>(areaWidth) / sourceWidth, static_cast<double>(areaHeight) / sourceHeight);
  outputWidth = std::max(1, static_cast<int>(std::lround(sourceWidth * scale)));
  outputHeight = std::max(1, static_cast<int>(std::lround(sourceHeight * scale)));
}

}  // namespace

void SleepImageSelectActivity::onEnter() {
  Activity::onEnter();
  Storage.mkdir("/.sleep");
  Storage.mkdir("/cover");
  showRoot();
  requestUpdate();
}

std::string SleepImageSelectActivity::currentDirectory() const {
  return directoryStack.empty() ? std::string() : directoryStack.back();
}

void SleepImageSelectActivity::showRoot() {
  viewMode = ViewMode::Root;
  directoryStack.clear();
  previewPath.clear();
  items.clear();
  items.push_back(Item{"", tr(STR_RANDOM), false, true});
  items.push_back(Item{"/.sleep", ".sleep", true, false});
  items.push_back(Item{"/cover", "cover", true, false});

  selectedIndex = 0;
  const std::string activePath(SETTINGS.sleepCustomImagePath);
  if (!activePath.empty()) {
    if (activePath.rfind("/.sleep/", 0) == 0)
      selectedIndex = 1;
    else if (activePath.rfind("/cover/", 0) == 0)
      selectedIndex = 2;
  }
}

void SleepImageSelectActivity::enterDirectory(const std::string& path) {
  directoryStack.push_back(path);
  viewMode = ViewMode::Folder;
  previewPath.clear();
  scanCurrentDirectory();
}

void SleepImageSelectActivity::scanCurrentDirectory() {
  items.clear();
  const std::string directory = currentDirectory();
  auto dir = Storage.open(directory.c_str());
  if (dir && dir.isDirectory()) {
    char name[500];
    for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
      file.getName(name, sizeof(name));
      const std::string filename(name);
      if (filename.empty() || filename[0] == '.') continue;

      std::string fullPath = directory;
      if (fullPath.size() > 1 && fullPath.back() == '/') fullPath.pop_back();
      fullPath += "/" + filename;

      if (file.isDirectory()) {
        items.push_back(Item{fullPath, filename, true, false});
      } else if (isSupportedImage(filename)) {
        items.push_back(Item{fullPath, filename, false, false});
      }
    }
    dir.close();
  }

  std::sort(items.begin(), items.end(), [](const Item& lhs, const Item& rhs) {
    if (lhs.directory != rhs.directory) return lhs.directory > rhs.directory;
    return lhs.title < rhs.title;
  });

  selectedIndex = 0;
  const std::string activePath(SETTINGS.sleepCustomImagePath);
  for (int i = 0; i < static_cast<int>(items.size()); ++i) {
    if (items[i].path == activePath) {
      selectedIndex = i;
      break;
    }
  }
}

void SleepImageSelectActivity::goBackOneLevel() {
  if (viewMode == ViewMode::Preview) {
    cancelPreview();
    return;
  }
  if (viewMode == ViewMode::Root) {
    finish();
    return;
  }

  if (!directoryStack.empty()) directoryStack.pop_back();
  if (directoryStack.empty()) {
    showRoot();
  } else {
    scanCurrentDirectory();
  }
  requestUpdate();
}

void SleepImageSelectActivity::chooseRandom() {
  SETTINGS.sleepCustomImagePath[0] = '\0';
  SETTINGS.saveToFile();
  SleepImages.refreshSelection();
  LOG_INF("SLP_UI", "Custom sleep image mode set to random");
  showRoot();
  requestUpdate();
}

void SleepImageSelectActivity::openPreview(const std::string& path) {
  previewPath = path;
  viewMode = ViewMode::Preview;
  requestUpdate();
}

void SleepImageSelectActivity::confirmPreview() {
  if (previewPath.empty()) return;
  std::strncpy(SETTINGS.sleepCustomImagePath, previewPath.c_str(), sizeof(SETTINGS.sleepCustomImagePath) - 1);
  SETTINGS.sleepCustomImagePath[sizeof(SETTINGS.sleepCustomImagePath) - 1] = '\0';
  SETTINGS.saveToFile();
  SleepImages.refreshSelection();
  LOG_INF("SLP_UI", "Custom sleep image selected: %s", SETTINGS.sleepCustomImagePath);

  viewMode = ViewMode::Folder;
  previewPath.clear();
  forceFullListRefresh = true;
  scanCurrentDirectory();
  requestUpdate();
}

void SleepImageSelectActivity::cancelPreview() {
  viewMode = ViewMode::Folder;
  previewPath.clear();
  forceFullListRefresh = true;
  scanCurrentDirectory();
  requestUpdate();
}

void SleepImageSelectActivity::activateSelection() {
  if (selectedIndex < 0 || selectedIndex >= totalItems()) return;
  const Item item = items[selectedIndex];
  if (item.random) {
    chooseRandom();
  } else if (item.directory) {
    enterDirectory(item.path);
    requestUpdate();
  } else {
    openPreview(item.path);
  }
}

void SleepImageSelectActivity::loop() {
  if (viewMode == ViewMode::Preview) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      cancelPreview();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      confirmPreview();
      return;
    }
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    goBackOneLevel();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    activateSelection();
    return;
  }

#if CROSSPOINT_PAPERS3
  if (totalItems() > 0) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight =
        renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
    const int targetIndex =
        DirectTouchSelection::hitListRow(mappedInput, Rect{0, contentTop, renderer.getScreenWidth(), contentHeight},
                                         totalItems(), selectedIndex, metrics.listWithSubtitleRowHeight);
    if (targetIndex >= 0) {
      if (targetIndex == selectedIndex) {
        activateSelection();
      } else {
        selectedIndex = targetIndex;
        requestUpdate();
      }
      return;
    }
  }
#endif

  if (totalItems() <= 0) return;
  const int pageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, true);
  buttonNavigator.onNextRelease([this, pageItems] {
    selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, totalItems(), pageItems);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, pageItems] {
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, totalItems(), pageItems);
    requestUpdate();
  });
}

bool SleepImageSelectActivity::drawPreviewImage(const std::string& path, int x, int y, int width, int height) {
  ImageDimensions dimensions{};
  bool haveDimensions = false;

  if (FsHelpers::hasJpgExtension(path)) {
    haveDimensions = JpegToFramebufferConverter::getDimensionsStatic(path, dimensions);
  } else if (FsHelpers::hasPngExtension(path)) {
    haveDimensions = PngToFramebufferConverter::getDimensionsStatic(path, dimensions);
  } else if (FsHelpers::hasBmpExtension(path)) {
    FsFile file;
    if (Storage.openFileForRead("SLP_UI", path, file)) {
      Bitmap bitmap(file, true);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        dimensions.width = bitmap.getWidth();
        dimensions.height = bitmap.getHeight();
        haveDimensions = true;
      }
      file.close();
    }
  }

  if (!haveDimensions || dimensions.width <= 0 || dimensions.height <= 0) return false;

  int drawWidth = 1;
  int drawHeight = 1;
  calculateFit(dimensions.width, dimensions.height, width, height, drawWidth, drawHeight);
  const int drawX = x + (width - drawWidth) / 2;
  const int drawY = y + (height - drawHeight) / 2;

  renderer.setRenderMode(GfxRenderer::GRAYSCALE_DIRECT);
  bool ok = false;
  if (FsHelpers::hasJpgExtension(path)) {
    JpegToFramebufferConverter decoder;
    RenderConfig config{};
    config.x = drawX;
    config.y = drawY;
    config.maxWidth = drawWidth;
    config.maxHeight = drawHeight;
    config.useExactDimensions = true;
    config.useGrayscale = true;
    config.useDithering = true;
    ok = decoder.decodeToFramebuffer(path, renderer, config);
  } else if (FsHelpers::hasPngExtension(path)) {
    PngToFramebufferConverter decoder;
    RenderConfig config{};
    config.x = drawX;
    config.y = drawY;
    config.maxWidth = drawWidth;
    config.maxHeight = drawHeight;
    config.useExactDimensions = true;
    config.useGrayscale = true;
    config.useDithering = true;
    ok = decoder.decodeToFramebuffer(path, renderer, config);
  } else if (FsHelpers::hasBmpExtension(path)) {
    FsFile file;
    if (Storage.openFileForRead("SLP_UI", path, file)) {
      Bitmap bitmap(file, true);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        renderer.drawBitmap(bitmap, drawX, drawY, drawWidth, drawHeight, 0, 0);
        ok = true;
      }
      file.close();
    }
  }
  renderer.setRenderMode(GfxRenderer::BW);
  return ok;
}

void SleepImageSelectActivity::renderPreview() {
  renderer.clearScreen(0xFF);
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  const std::string previewTitle = std::string(tr(STR_SLEEP_IMAGE_PREVIEW)) + "  " + filenameOf(previewPath);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, previewTitle.c_str());
  const int imageTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int imageHeight = pageHeight - imageTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  if (!drawPreviewImage(previewPath, 0, imageTop, pageWidth, imageHeight)) {
    renderer.drawCenteredText(UI_10_FONT_ID, imageTop + imageHeight / 2, tr(STR_IMAGE_PREVIEW_FAILED));
  }

  const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_CONFIRM), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
}

void SleepImageSelectActivity::renderList() {
  renderer.clearScreen();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  std::string header = tr(STR_CUSTOM_SLEEP_IMAGE);
  if (viewMode == ViewMode::Folder) header += "  " + currentDirectory();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, header.c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const std::string activePath(SETTINGS.sleepCustomImagePath);

  if (totalItems() > 0) {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, totalItems(), selectedIndex,
        [this](int index) { return items[index].title; },
        [this](int index) {
          if (items[index].random) return std::string("/.sleep + /cover");
          return items[index].path;
        },
        nullptr,
        [this, activePath](int index) {
          if (items[index].random) return activePath.empty() ? std::string(tr(STR_SELECTED)) : std::string();
          return !items[index].directory && items[index].path == activePath ? std::string(tr(STR_SELECTED))
                                                                            : std::string();
        },
        true);
  } else {
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + contentHeight / 2, tr(STR_NO_SLEEP_IMAGES));
  }

  const int hintPageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, true);
  const char* prevPageLabel =
      totalItems() > 0 && ButtonNavigator::hasPreviousPage(selectedIndex, totalItems(), hintPageItems) ? tr(STR_DIR_UP)
                                                                                                       : "";
  const char* nextPageLabel =
      totalItems() > 0 && ButtonNavigator::hasNextPage(selectedIndex, totalItems(), hintPageItems) ? tr(STR_DIR_DOWN)
                                                                                                   : "";
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), totalItems() > 0 ? tr(STR_SELECT) : "", prevPageLabel, nextPageLabel);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(forceFullListRefresh ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
  forceFullListRefresh = false;
}

void SleepImageSelectActivity::render(RenderLock&&) {
  if (viewMode == ViewMode::Preview)
    renderPreview();
  else
    renderList();
}
