#include "SettingsActivity.h"

#include <GfxRenderer.h>
#include <FontManager.h>
#include <Logging.h>

#include <algorithm>

#include "ButtonRemapActivity.h"
#include "CalibreSettingsActivity.h"
#include "ClearCacheActivity.h"
#include "CrossPointSettings.h"
#include "KOReaderSettingsActivity.h"
#include "LanguageSelectActivity.h"
#include "MappedInputManager.h"
#include "activities/util/DirectTouchSelection.h"
#include "OtaUpdateActivity.h"
#include "ReaderFontSelectActivity.h"
#include "ReaderFontSizeActivity.h"
#include "ReaderValueAdjustActivity.h"
#include "SettingsList.h"
#include "StatusBarSettingsActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

const StrId SettingsActivity::categoryNames[categoryCount] = {StrId::STR_CAT_DISPLAY, StrId::STR_CAT_READER,
                                                              StrId::STR_CAT_CONTROLS, StrId::STR_CAT_SYSTEM};

void SettingsActivity::enterCategory(int categoryIndex) {
  selectedCategoryIndex = std::max(0, std::min(categoryCount - 1, categoryIndex));

  switch (selectedCategoryIndex) {
    case 1:
      currentSettings = &readerSettings;
      break;
    case 2:
      currentSettings = &controlsSettings;
      break;
    case 3:
      currentSettings = &systemSettings;
      break;
    case 0:
    default:
      currentSettings = &displaySettings;
      break;
  }

  settingsCount = currentSettings != nullptr ? static_cast<int>(currentSettings->size()) : 0;
}

void SettingsActivity::onEnter() {
  Activity::onEnter();

  // Build per-category vectors from the shared settings list
  displaySettings.clear();
  readerSettings.clear();
  controlsSettings.clear();
  systemSettings.clear();

  for (const auto& setting : getSettingsList()) {
    if (setting.category == StrId::STR_NONE_OPT) continue;
    if (setting.category == StrId::STR_CAT_DISPLAY) {
      displaySettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_READER) {
      readerSettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_CONTROLS) {
      controlsSettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_SYSTEM) {
      systemSettings.push_back(setting);
    }
    // Web-only categories (KOReader Sync, OPDS Browser) are skipped for device UI
  }

  // Append device-only ACTION items
  readerSettings.insert(readerSettings.begin(),
                        SettingInfo::Action(StrId::STR_READER_FONT_FILE, SettingAction::ReaderFontFile));
  controlsSettings.insert(controlsSettings.begin(),
                          SettingInfo::Action(StrId::STR_REMAP_FRONT_BUTTONS, SettingAction::RemapFrontButtons));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_WIFI_NETWORKS, SettingAction::Network));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_KOREADER_SYNC, SettingAction::KOReaderSync));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_OPDS_BROWSER, SettingAction::OPDSBrowser));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_CLEAR_READING_CACHE, SettingAction::ClearCache));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_CHECK_UPDATES, SettingAction::CheckForUpdates));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_LANGUAGE, SettingAction::Language));
  readerSettings.push_back(SettingInfo::Action(StrId::STR_CUSTOMISE_STATUS_BAR, SettingAction::CustomiseStatusBar));

  // Start in the requested category. Reader-menu launches use category 1
  // and return to the active book instead of navigating to Home.
  enterCategory(initialCategoryIndex);
  // Reader-menu launches focus the first Reader setting immediately. Normal
  // Settings launches keep the category tab focused as before.
  selectedSettingIndex = returnToCaller ? 1 : 0;

  // Trigger first update
  requestUpdate();
}

void SettingsActivity::onExit() {
  Activity::onExit();

  UITheme::getInstance().reload();  // Re-apply theme in case it was changed
}

void SettingsActivity::loop() {
  bool hasChangedCategory = false;

#if CROSSPOINT_PAPERS3
  {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int pageWidth = renderer.getScreenWidth();

    // Direct Touch Selection: Settings category tab bar.
    // Tap any of the four category tabs to enter that category. If the same
    // category is already active, move focus back to the tab bar instead of
    // toggling a setting.
    if (mappedInput.wasContentTapReleased()) {
      const int tapX = mappedInput.getContentTapX();
      const int tapY = mappedInput.getContentTapY();
      const Rect tabRect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight};
      if (tapX >= tabRect.x && tapX < tabRect.x + tabRect.width &&
          tapY >= tabRect.y && tapY < tabRect.y + tabRect.height) {
        const int targetCategory = std::max(0, std::min(categoryCount - 1,
            ((tapX - tabRect.x) * categoryCount) / std::max(1, tabRect.width)));
        if (targetCategory != selectedCategoryIndex) {
          enterCategory(targetCategory);
        }
        selectedSettingIndex = 0;
        requestUpdate();
        return;
      }
    }

    const int listTop = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing;
    const int listHeight = renderer.getScreenHeight() -
                           (metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight +
                            metrics.buttonHintsHeight + metrics.verticalSpacing * 2);
    const int targetSetting = DirectTouchSelection::hitListRow(
        mappedInput, Rect{0, listTop, pageWidth, listHeight}, settingsCount,
        std::max(0, selectedSettingIndex - 1), metrics.listRowHeight);
    if (targetSetting >= 0) {
      const int targetSelection = targetSetting + 1;
      if (targetSelection == selectedSettingIndex) {
        toggleCurrentSetting();
      } else {
        selectedSettingIndex = targetSelection;
      }
      requestUpdate();
      return;
    }
  }
#endif

  // Handle actions with early return
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectedSettingIndex == 0) {
      selectedCategoryIndex = (selectedCategoryIndex < categoryCount - 1) ? (selectedCategoryIndex + 1) : 0;
      hasChangedCategory = true;
      requestUpdate();
    } else {
      toggleCurrentSetting();
      requestUpdate();
      return;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (selectedSettingIndex > 0) {
      selectedSettingIndex = 0;
      requestUpdate();
    } else {
      SETTINGS.saveToFile();
      if (returnToCaller) {
        finish();
      } else {
        onGoHome();
      }
    }
    return;
  }

  // Footer Previous / Next are page-level navigation for the settings list.
  // Category tabs are selected by direct touch, not by footer paging.
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int listTop = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing;
  const int listHeight = renderer.getScreenHeight() -
                         (metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight +
                          metrics.buttonHintsHeight + metrics.verticalSpacing * 2);
  const int pageItems = std::max(1, listHeight / std::max(1, metrics.listRowHeight));

  buttonNavigator.onNextRelease([this, pageItems] {
    const int currentListIndex = std::max(0, selectedSettingIndex - 1);
    selectedSettingIndex = ButtonNavigator::nextPageIndex(currentListIndex, settingsCount, pageItems) + 1;
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, pageItems] {
    const int currentListIndex = std::max(0, selectedSettingIndex - 1);
    selectedSettingIndex = ButtonNavigator::previousPageIndex(currentListIndex, settingsCount, pageItems) + 1;
    requestUpdate();
  });

  if (hasChangedCategory) {
    selectedSettingIndex = (selectedSettingIndex == 0) ? 0 : 1;
    enterCategory(selectedCategoryIndex);
  }
}

void SettingsActivity::toggleCurrentSetting() {
  int selectedSetting = selectedSettingIndex - 1;
  if (selectedSetting < 0 || selectedSetting >= settingsCount) {
    return;
  }

  const auto& setting = (*currentSettings)[selectedSetting];

  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    // Toggle the boolean value using the member pointer
    const bool currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = !currentValue;
  } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
#if CROSSPOINT_PAPERS3
    if (setting.valuePtr == &CrossPointSettings::orientation) {
      const uint8_t currentValue = SETTINGS.*(setting.valuePtr);
      SETTINGS.*(setting.valuePtr) = CrossPointSettings::nextPaperS3Orientation(currentValue);
    } else
#endif
    {
      const uint8_t currentValue = SETTINGS.*(setting.valuePtr);
      SETTINGS.*(setting.valuePtr) = (currentValue + 1) % static_cast<uint8_t>(setting.enumValues.size());
    }

    if (setting.valuePtr == &CrossPointSettings::readingLayout) {
      LOG_INF("SET", "Reading layout changed to %s",
              SETTINGS.readingLayout == CrossPointSettings::VERTICAL_LAYOUT ? "vertical" : "horizontal");
    }

    if (setting.valuePtr == &CrossPointSettings::fontSize) {
      FontMgr.reloadReaderFontForSize();
    }
  } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    if (setting.valuePtr == &CrossPointSettings::fontSize) {
      // Open a focused numeric picker. +/- applies immediately; Back/Done just returns.
      startActivityForResult(
          std::make_unique<ReaderFontSizeActivity>(renderer, mappedInput, SETTINGS.fontSize),
          [](const ActivityResult&) {});
      return;
    }

    if (setting.valuePtr == &CrossPointSettings::lineSpacing) {
      startActivityForResult(
          std::make_unique<ReaderValueAdjustActivity>(
              renderer, mappedInput, StrId::STR_LINE_SPACING, SETTINGS.lineSpacing,
              CrossPointSettings::READER_LINE_SPACING_MIN, CrossPointSettings::READER_LINE_SPACING_MAX, 5, "%",
              "直排：調整欄距",
              [](uint8_t value) {
                SETTINGS.lineSpacing = value;
                SETTINGS.saveToFile();
              }),
          [](const ActivityResult&) {});
      return;
    }

    if (setting.valuePtr == &CrossPointSettings::characterSpacing) {
      startActivityForResult(
          std::make_unique<ReaderValueAdjustActivity>(
              renderer, mappedInput, StrId::STR_CHARACTER_SPACING, SETTINGS.characterSpacing,
              CrossPointSettings::READER_CHARACTER_SPACING_MIN, CrossPointSettings::READER_CHARACTER_SPACING_MAX, 1, " px",
              "0 px 為最緊密字距",
              [](uint8_t value) {
                SETTINGS.characterSpacing = value;
                SETTINGS.saveToFile();
              }),
          [](const ActivityResult&) {});
      return;
    }

    const uint8_t currentValue = SETTINGS.*(setting.valuePtr);
    if (currentValue + setting.valueRange.step > setting.valueRange.max) {
      SETTINGS.*(setting.valuePtr) = setting.valueRange.min;
    } else {
      SETTINGS.*(setting.valuePtr) = currentValue + setting.valueRange.step;
    }
  } else if (setting.type == SettingType::ACTION) {
    auto resultHandler = [this](const ActivityResult&) { SETTINGS.saveToFile(); };

    switch (setting.action) {
      case SettingAction::RemapFrontButtons:
        startActivityForResult(std::make_unique<ButtonRemapActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::CustomiseStatusBar:
        startActivityForResult(std::make_unique<StatusBarSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::KOReaderSync:
        startActivityForResult(std::make_unique<KOReaderSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::OPDSBrowser:
        startActivityForResult(std::make_unique<CalibreSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::Network:
        startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, false), resultHandler);
        break;
      case SettingAction::ClearCache:
        startActivityForResult(std::make_unique<ClearCacheActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::CheckForUpdates:
        startActivityForResult(std::make_unique<OtaUpdateActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::Language:
        startActivityForResult(std::make_unique<LanguageSelectActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::ReaderFontFile:
        startActivityForResult(std::make_unique<ReaderFontSelectActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::None:
        // Do nothing
        break;
    }
    return;  // Results will be handled in the result handler, so we can return early here
  } else {
    return;
  }

  SETTINGS.saveToFile();
}

void SettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_SETTINGS_TITLE),
                 CROSSPOINT_VERSION);

  std::vector<TabInfo> tabs;
  tabs.reserve(categoryCount);
  for (int i = 0; i < categoryCount; i++) {
    tabs.push_back({I18N.get(categoryNames[i]), selectedCategoryIndex == i});
  }
  GUI.drawTabBar(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight}, tabs,
                 selectedSettingIndex == 0);

  const auto& settings = *currentSettings;
  GUI.drawList(
      renderer,
      Rect{0, metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing, pageWidth,
           pageHeight - (metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.buttonHintsHeight +
                         metrics.verticalSpacing * 2)},
      settingsCount, selectedSettingIndex - 1,
      [&settings](int index) { return std::string(I18N.get(settings[index].nameId)); }, nullptr, nullptr,
      [&settings](int i) {
        const auto& setting = settings[i];
        std::string valueText = "";
        if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
          const bool value = SETTINGS.*(setting.valuePtr);
          valueText = value ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
        } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
          uint8_t value = SETTINGS.*(setting.valuePtr);
#if CROSSPOINT_PAPERS3
          if (setting.valuePtr == &CrossPointSettings::orientation) {
            value = CrossPointSettings::normalizePaperS3Orientation(value);
          }
#endif
          valueText = I18N.get(setting.enumValues[value]);
        } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
          valueText = std::to_string(SETTINGS.*(setting.valuePtr));
          if (setting.valuePtr == &CrossPointSettings::fontSize) {
            valueText += " px";
          } else if (setting.valuePtr == &CrossPointSettings::lineSpacing) {
            valueText += "%";
          } else if (setting.valuePtr == &CrossPointSettings::characterSpacing) {
            valueText += " px";
          }
        }
        return valueText;
      },
      true);

  // Draw help text. Large numeric reader values open a focused +/- picker,
  // while ordinary toggles/enums still change in place.
  const char* confirmLabel = nullptr;
  if (selectedSettingIndex == 0) {
    confirmLabel = I18N.get(categoryNames[(selectedCategoryIndex + 1) % categoryCount]);
  } else {
    const auto& selected = settings[selectedSettingIndex - 1];
    const bool opensSelector =
        selected.type == SettingType::ACTION ||
        (selected.type == SettingType::VALUE &&
         (selected.valuePtr == &CrossPointSettings::fontSize ||
          selected.valuePtr == &CrossPointSettings::lineSpacing ||
          selected.valuePtr == &CrossPointSettings::characterSpacing));
    confirmLabel = opensSelector ? tr(STR_SELECT) : tr(STR_TOGGLE);
  }
  const int currentListIndex = std::max(0, selectedSettingIndex - 1);
  const int listPageItems = std::max(1, (pageHeight - (metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight +
                                                      metrics.buttonHintsHeight + metrics.verticalSpacing * 2)) /
                                           std::max(1, metrics.listRowHeight));
  const char* prevPageLabel = ButtonNavigator::hasPreviousPage(currentListIndex, settingsCount, listPageItems)
                                  ? tr(STR_DIR_UP)
                                  : "";
  const char* nextPageLabel = ButtonNavigator::hasNextPage(currentListIndex, settingsCount, listPageItems)
                                  ? tr(STR_DIR_DOWN)
                                  : "";
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, prevPageLabel, nextPageLabel);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Always use standard refresh for settings screen
  renderer.displayBuffer();
}
