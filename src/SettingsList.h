#pragma once

#include <I18n.h>

#include <vector>

#include "CrossPointSettings.h"
#include "KOReaderCredentialStore.h"
#include "activities/settings/SettingsActivity.h"

// Shared settings list used by both the device settings UI and the web settings API.
// Each entry has a key (for JSON API) and category (for grouping).
// ACTION-type entries and entries without a key are device-only.
inline const std::vector<SettingInfo>& getSettingsList() {
  static const std::vector<SettingInfo> list = {
      // --- Display ---
      SettingInfo::Enum(StrId::STR_SLEEP_SCREEN, &CrossPointSettings::sleepScreen,
                        {StrId::STR_DARK, StrId::STR_LIGHT, StrId::STR_CUSTOM, StrId::STR_COVER, StrId::STR_NONE_OPT,
                         StrId::STR_COVER_CUSTOM},
                        "sleepScreen", StrId::STR_CAT_DISPLAY),
      // Paper S3 sleep rendering now uses the prepared/custom image path or
      // the captured reader page directly. The old cover mode/filter options
      // are intentionally hidden from the device Settings UI and web settings
      // list to avoid exposing controls that no longer match the active sleep
      // workflow. Legacy values are still readable in CrossPointSettings for
      // backward compatibility with older settings files.
      SettingInfo::Toggle(StrId::STR_SLEEP_SCREEN_ROTATE_180, &CrossPointSettings::sleepScreenRotate180,
                          "sleepScreenRotate180", StrId::STR_CAT_DISPLAY),
      SettingInfo::Enum(
          StrId::STR_TRANSPARENT_SLEEP_PNG_BACKGROUND,
          &CrossPointSettings::transparentSleepPngBackground,
          {StrId::STR_CURRENT_READING_PAGE, StrId::STR_WHITE_BACKGROUND},
          "transparentSleepPngBackground", StrId::STR_CAT_DISPLAY),
      SettingInfo::Enum(StrId::STR_HIDE_BATTERY, &CrossPointSettings::hideBatteryPercentage,
                        {StrId::STR_NEVER, StrId::STR_IN_READER, StrId::STR_ALWAYS}, "hideBatteryPercentage",
                        StrId::STR_CAT_DISPLAY),
      SettingInfo::Enum(
          StrId::STR_REFRESH_FREQ, &CrossPointSettings::refreshFrequency,
          {StrId::STR_PAGES_1, StrId::STR_PAGES_5, StrId::STR_PAGES_10, StrId::STR_PAGES_15, StrId::STR_PAGES_30,
           StrId::STR_PAGES_NEVER},
          "refreshFrequency", StrId::STR_CAT_DISPLAY),
      SettingInfo::Enum(StrId::STR_UI_THEME, &CrossPointSettings::uiTheme,
                        {StrId::STR_THEME_CLASSIC, StrId::STR_THEME_LYRA, StrId::STR_THEME_LYRA_EXTENDED,
                         StrId::STR_THEME_LARGE_TEXT},
                        "uiTheme",
                        StrId::STR_CAT_DISPLAY),
      SettingInfo::Toggle(StrId::STR_SUNLIGHT_FADING_FIX, &CrossPointSettings::fadingFix, "fadingFix",
                          StrId::STR_CAT_DISPLAY),
      SettingInfo::Toggle(StrId::STR_READER_CONTENT_INVERT, &CrossPointSettings::readerContentInvert,
                          "readerContentInvert", StrId::STR_CAT_DISPLAY),

      // --- Reader ---
      SettingInfo::Value(StrId::STR_FONT_SIZE, &CrossPointSettings::fontSize,
                         {CrossPointSettings::READER_FONT_SIZE_MIN, CrossPointSettings::READER_FONT_SIZE_MAX, 1},
                         "fontSize", StrId::STR_CAT_READER),
      SettingInfo::Value(StrId::STR_LINE_SPACING, &CrossPointSettings::lineSpacing,
                         {CrossPointSettings::READER_LINE_SPACING_MIN,
                          CrossPointSettings::READER_LINE_SPACING_MAX, 5},
                         "lineSpacing", StrId::STR_CAT_READER),
      SettingInfo::Value(StrId::STR_CHARACTER_SPACING, &CrossPointSettings::characterSpacing,
                         {CrossPointSettings::READER_CHARACTER_SPACING_MIN,
                          CrossPointSettings::READER_CHARACTER_SPACING_MAX, 1},
                         "characterSpacing", StrId::STR_CAT_READER),
      SettingInfo::Value(StrId::STR_SCREEN_MARGIN, &CrossPointSettings::screenMargin, {5, 40, 5}, "screenMargin",
                         StrId::STR_CAT_READER),
      SettingInfo::Toggle(StrId::STR_STATUS_BAR_FOLLOWS_MARGIN, &CrossPointSettings::statusBarFollowsPageMargin,
                          "statusBarFollowsPageMargin", StrId::STR_CAT_READER),
      SettingInfo::Enum(StrId::STR_PARA_ALIGNMENT, &CrossPointSettings::paragraphAlignment,
                        {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                         StrId::STR_BOOK_S_STYLE},
                        "paragraphAlignment", StrId::STR_CAT_READER),
      SettingInfo::Toggle(StrId::STR_EMBEDDED_STYLE, &CrossPointSettings::embeddedStyle, "embeddedStyle",
                          StrId::STR_CAT_READER),
      SettingInfo::Toggle(StrId::STR_HYPHENATION, &CrossPointSettings::hyphenationEnabled, "hyphenationEnabled",
                          StrId::STR_CAT_READER),
      SettingInfo::Enum(StrId::STR_ORIENTATION, &CrossPointSettings::orientation,
                        {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_INVERTED, StrId::STR_LANDSCAPE_CCW},
                        "orientation", StrId::STR_CAT_READER),
      SettingInfo::Enum(StrId::STR_READING_LAYOUT, &CrossPointSettings::readingLayout,
                        {StrId::STR_HORIZONTAL_LAYOUT, StrId::STR_VERTICAL_LAYOUT},
                        "readingLayout", StrId::STR_CAT_READER),
      SettingInfo::Toggle(StrId::STR_EXTRA_SPACING, &CrossPointSettings::extraParagraphSpacing, "extraParagraphSpacing",
                          StrId::STR_CAT_READER),
      SettingInfo::Toggle(StrId::STR_TEXT_AA, &CrossPointSettings::textAntiAliasing, "textAntiAliasing",
                          StrId::STR_CAT_READER),
      SettingInfo::Enum(StrId::STR_IMAGES, &CrossPointSettings::imageRendering,
                        {StrId::STR_IMAGES_DISPLAY, StrId::STR_IMAGES_PLACEHOLDER, StrId::STR_IMAGES_SUPPRESS},
                        "imageRendering", StrId::STR_CAT_READER),
      // --- Controls ---
      SettingInfo::Toggle(StrId::STR_LONG_PRESS_SKIP, &CrossPointSettings::longPressChapterSkip, "longPressChapterSkip",
                          StrId::STR_CAT_CONTROLS),
      SettingInfo::Toggle(StrId::STR_SWIPE_PAGE_TURN, &CrossPointSettings::swipePageTurnEnabled,
                          "swipePageTurnEnabled", StrId::STR_CAT_CONTROLS),
      SettingInfo::Enum(StrId::STR_PAGE_TURN_REFRESH_MODE, &CrossPointSettings::pageTurnRefreshMode,
                        {StrId::STR_PAGE_TURN_REFRESH_ORIGINAL, StrId::STR_PAGE_TURN_REFRESH_BAND_SCAN},
                        "pageTurnRefreshMode", StrId::STR_CAT_CONTROLS),

      // --- System ---
      SettingInfo::Enum(StrId::STR_TIME_TO_SLEEP, &CrossPointSettings::sleepTimeout,
                        {StrId::STR_MIN_1, StrId::STR_MIN_5, StrId::STR_MIN_10, StrId::STR_MIN_15, StrId::STR_MIN_30},
                        "sleepTimeout", StrId::STR_CAT_SYSTEM),
      SettingInfo::Toggle(StrId::STR_SHOW_HIDDEN_FILES, &CrossPointSettings::showHiddenFiles, "showHiddenFiles",
                          StrId::STR_CAT_SYSTEM),

      // --- KOReader Sync (web-only, uses KOReaderCredentialStore) ---
      SettingInfo::DynamicString(
          StrId::STR_KOREADER_USERNAME, [] { return KOREADER_STORE.getUsername(); },
          [](const std::string& v) {
            KOREADER_STORE.setCredentials(v, KOREADER_STORE.getPassword());
            KOREADER_STORE.saveToFile();
          },
          "koUsername", StrId::STR_KOREADER_SYNC),
      SettingInfo::DynamicString(
          StrId::STR_KOREADER_PASSWORD, [] { return KOREADER_STORE.getPassword(); },
          [](const std::string& v) {
            KOREADER_STORE.setCredentials(KOREADER_STORE.getUsername(), v);
            KOREADER_STORE.saveToFile();
          },
          "koPassword", StrId::STR_KOREADER_SYNC),
      SettingInfo::DynamicString(
          StrId::STR_SYNC_SERVER_URL, [] { return KOREADER_STORE.getServerUrl(); },
          [](const std::string& v) {
            KOREADER_STORE.setServerUrl(v);
            KOREADER_STORE.saveToFile();
          },
          "koServerUrl", StrId::STR_KOREADER_SYNC),
      SettingInfo::DynamicEnum(
          StrId::STR_DOCUMENT_MATCHING, {StrId::STR_FILENAME, StrId::STR_BINARY},
          [] { return static_cast<uint8_t>(KOREADER_STORE.getMatchMethod()); },
          [](uint8_t v) {
            KOREADER_STORE.setMatchMethod(static_cast<DocumentMatchMethod>(v));
            KOREADER_STORE.saveToFile();
          },
          "koMatchMethod", StrId::STR_KOREADER_SYNC),

      // --- OPDS Browser (web-only, uses CrossPointSettings char arrays) ---
      SettingInfo::String(StrId::STR_OPDS_SERVER_URL, SETTINGS.opdsServerUrl, sizeof(SETTINGS.opdsServerUrl),
                          "opdsServerUrl", StrId::STR_OPDS_BROWSER),
      SettingInfo::String(StrId::STR_USERNAME, SETTINGS.opdsUsername, sizeof(SETTINGS.opdsUsername), "opdsUsername",
                          StrId::STR_OPDS_BROWSER),
      SettingInfo::String(StrId::STR_PASSWORD, SETTINGS.opdsPassword, sizeof(SETTINGS.opdsPassword), "opdsPassword",
                          StrId::STR_OPDS_BROWSER)
          .withObfuscated(),
      // --- Status Bar Settings (web-only, uses StatusBarSettingsActivity) ---
      SettingInfo::Toggle(StrId::STR_CHAPTER_PAGE_COUNT, &CrossPointSettings::statusBarChapterPageCount,
                          "statusBarChapterPageCount", StrId::STR_CUSTOMISE_STATUS_BAR),
      SettingInfo::Toggle(StrId::STR_BOOK_PROGRESS_PERCENTAGE, &CrossPointSettings::statusBarBookProgressPercentage,
                          "statusBarBookProgressPercentage", StrId::STR_CUSTOMISE_STATUS_BAR),
      SettingInfo::Enum(StrId::STR_PROGRESS_BAR, &CrossPointSettings::statusBarProgressBar,
                        {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE}, "statusBarProgressBar",
                        StrId::STR_CUSTOMISE_STATUS_BAR),
      SettingInfo::Enum(StrId::STR_PROGRESS_BAR_THICKNESS, &CrossPointSettings::statusBarProgressBarThickness,
                        {StrId::STR_PROGRESS_BAR_THIN, StrId::STR_PROGRESS_BAR_MEDIUM, StrId::STR_PROGRESS_BAR_THICK},
                        "statusBarProgressBarThickness", StrId::STR_CUSTOMISE_STATUS_BAR),
      SettingInfo::Enum(StrId::STR_TITLE, &CrossPointSettings::statusBarTitle,
                        {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE}, "statusBarTitle",
                        StrId::STR_CUSTOMISE_STATUS_BAR),
      SettingInfo::Toggle(StrId::STR_BATTERY, &CrossPointSettings::statusBarBattery, "statusBarBattery",
                          StrId::STR_CUSTOMISE_STATUS_BAR),
      SettingInfo::Toggle(StrId::STR_CLOCK, &CrossPointSettings::statusBarClock, "statusBarClock",
                          StrId::STR_CUSTOMISE_STATUS_BAR),
      SettingInfo::Value(StrId::STR_CLOCK_UTC_OFFSET, &CrossPointSettings::clockUtcOffsetQ, {0, 104, 1},
                         "clockUtcOffsetQ", StrId::STR_CUSTOMISE_STATUS_BAR),
      SettingInfo::Enum(StrId::STR_CLOCK_FORMAT, &CrossPointSettings::clockFormat,
                        {StrId::STR_CLOCK_FORMAT_24H, StrId::STR_CLOCK_FORMAT_12H}, "clockFormat",
                        StrId::STR_CUSTOMISE_STATUS_BAR),
      SettingInfo::Toggle(StrId::STR_CLOCK_SYNCED, &CrossPointSettings::clockHasBeenSynced, "clockHasBeenSynced",
                          StrId::STR_CUSTOMISE_STATUS_BAR),
  };
  return list;
}
