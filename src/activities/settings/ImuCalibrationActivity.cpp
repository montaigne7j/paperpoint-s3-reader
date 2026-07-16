#include "ImuCalibrationActivity.h"

#include <Arduino.h>
#include <I18n.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/PocketLock.h"

void ImuCalibrationActivity::onEnter() {
  Activity::onEnter();
  step = 0;
  unstable = false;
  completed = false;
  faceDownStableSince = 0;
  faceDownLastSampleAt = 0;
  imuUnavailable = !PocketLock::isAvailable();
  PocketLock::beginCalibrationSession();
  requestUpdate(true);
}

void ImuCalibrationActivity::onExit() {
  PocketLock::endCalibrationSession(completed);
  Activity::onExit();
}

void ImuCalibrationActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (completed) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) finish();
    return;
  }

  if (imuUnavailable) {
    // Keep Back functional and make the failure explicit instead of silently
    // ignoring Select/Confirm when BMI270 initialization failed.
    return;
  }

  if (step < 3 && mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    sampling = true;
    unstable = false;
    requestUpdateAndWait();

    const bool ok = PocketLock::captureCalibrationPose(static_cast<PocketLock::CalibrationPose>(step));
    sampling = false;
    unstable = !ok;
    if (ok) {
      ++step;
    }
    requestUpdate(true);
    return;
  }

  if (step == 3) {
    const uint32_t now = millis();
    const auto detection = PocketLock::pollFaceDownDetection();

    if (detection == PocketLock::FaceDownDetection::NoSample) {
      // BMI270 publishes samples periodically.  A loop iteration without a
      // fresh sample must preserve the hold timer.  Only abandon it if the
      // sensor has provided no sample for an abnormally long interval.
      if (faceDownStableSince != 0 && faceDownLastSampleAt != 0 && now - faceDownLastSampleAt > 600) {
        LOG_DBG("IMU", "Face-down hold reset: no fresh sample for %lu ms", now - faceDownLastSampleAt);
        faceDownStableSince = 0;
        faceDownLastSampleAt = 0;
        requestUpdate();
      }
      return;
    }

    faceDownLastSampleAt = now;
    if (detection == PocketLock::FaceDownDetection::NotFaceDown) {
      if (faceDownStableSince != 0) {
        LOG_DBG("IMU", "Face-down hold reset: posture changed");
        faceDownStableSince = 0;
        requestUpdate();
      }
      return;
    }

    if (faceDownStableSince == 0) {
      faceDownStableSince = now;
      LOG_INF("IMU", "Face-down posture detected; starting stable hold");
      requestUpdate();
      return;
    }

    if (now - faceDownStableSince >= 1200) {
      sampling = true;
      requestUpdateAndWait();

      completed = PocketLock::captureCalibrationPose(PocketLock::CalibrationPose::FaceDown);
      sampling = false;
      unstable = !completed;
      faceDownStableSince = 0;
      faceDownLastSampleAt = 0;

      if (completed) LOG_INF("IMU", "Four-pose calibration completed");
      else LOG_ERR("IMU", "Face-down capture or four-pose validation failed");
      requestUpdate(true);
    }
  }
}

void ImuCalibrationActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_IMU_CALIBRATION));

  const StrId steps[] = {
      StrId::STR_IMU_CAL_STEP_1,
      StrId::STR_IMU_CAL_STEP_2,
      StrId::STR_IMU_CAL_STEP_3,
      StrId::STR_IMU_CAL_STEP_4,
  };
  if (imuUnavailable) {
    renderer.drawCenteredText(UI_12_FONT_ID, height / 2 - 35, "BMI270 初始化失敗", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, height / 2 + 15, "請重新啟動後再進行校正");
  } else if (completed) {
    renderer.drawCenteredText(UI_12_FONT_ID, height / 2 - 15, tr(STR_IMU_CAL_SAVED), true, EpdFontFamily::BOLD);
  } else {
    // The fourth instruction is wider than the 540 px Paper S3 panel.  Wrap
    // every step so rotated rendering never writes outside the framebuffer.
    const auto instructionLines = renderer.wrappedText(
        UI_10_FONT_ID, I18n::getInstance().get(steps[step]), width - 56, 4, EpdFontFamily::BOLD);
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    int instructionY = height / 2 - 90 - static_cast<int>(instructionLines.size() * lineHeight) / 2;
    for (const auto& line : instructionLines) {
      renderer.drawCenteredText(UI_10_FONT_ID, instructionY, line.c_str(), true, EpdFontFamily::BOLD);
      instructionY += lineHeight + 4;
    }

    if (sampling) renderer.drawCenteredText(UI_10_FONT_ID, height / 2 + 35, "讀取中，請保持不動…");
    else if (unstable) renderer.drawCenteredText(UI_10_FONT_ID, height / 2 + 35, tr(STR_IMU_CAL_UNSTABLE));
    if (!sampling && step == 3 && faceDownStableSince != 0) {
      renderer.drawCenteredText(UI_10_FONT_ID, height / 2 + 70, "已偵測螢幕朝下，請保持不動…");
    }
  }

  const auto labels = completed
      ? mappedInput.mapLabels(tr(STR_BACK), tr(STR_DONE), "", "")
      : mappedInput.mapLabels(tr(STR_BACK), (!imuUnavailable && step < 3) ? tr(STR_CONFIRM) : "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Rendering only updates the shared framebuffer.  The calibration activity
  // must explicitly submit it to the e-paper panel, just like the other
  // settings activities.  Without this call the activity and its input state
  // run normally in the log, but the previous Settings screen remains visible.
  renderer.displayBuffer();
}
