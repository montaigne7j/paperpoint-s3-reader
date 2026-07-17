#include "Activity.h"

#include "ActivityManager.h"
#if CROSSPOINT_PAPERS3
#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "reader/ReaderUtils.h"
#endif

void Activity::onEnter() {
  LOG_DBG("ACT", "Entering activity: %s", name.c_str());
  mappedInput.clearState();  // Prevent stale touches from triggering actions in the new activity
#if CROSSPOINT_PAPERS3
  renderer.requestFullRefresh();  // Full e-ink refresh on every activity transition to prevent ghosting
  if (!isReaderActivity()) {
    // Reader sub-screens (reader menu, chapter list, settings opened from the
    // reader, comic menu, etc.) must follow the reader's 0/180 orientation.
    // Only top-level non-reader UI is forced back to normal portrait.
    if (activityManager.isReaderContextActive()) {
      ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
    } else {
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
    }
  }
  mappedInput.setTouchOrientation(renderer.getOrientation());
  // Enable footer nav buttons for all non-reader activities; readers use full screen for content
  mappedInput.setFooterHeight(isReaderActivity() ? 0 : UITheme::getInstance().getMetrics().buttonHintsHeight);
#endif
}

void Activity::onExit() { LOG_DBG("ACT", "Exiting activity: %s", name.c_str()); }

void Activity::requestUpdate(bool immediate) { activityManager.requestUpdate(immediate); }

void Activity::requestUpdateAndWait() { activityManager.requestUpdateAndWait(); }

void Activity::onGoHome() { activityManager.goHome(); }

void Activity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void Activity::startActivityForResult(std::unique_ptr<Activity>&& activity, ActivityResultHandler resultHandler) {
  this->resultHandler = std::move(resultHandler);
  activityManager.pushActivity(std::move(activity));
}

void Activity::setResult(ActivityResult&& result) { this->result = std::move(result); }

void Activity::finish() { activityManager.popActivity(); }
