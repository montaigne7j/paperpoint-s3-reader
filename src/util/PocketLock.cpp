#include "PocketLock.h"

#include <Arduino.h>
#include <CrossPointSettings.h>
#include <Logging.h>

#include <algorithm>

#if CROSSPOINT_PAPERS3
#include <M5Unified.h>
#include <Preferences.h>
#include <driver/i2c.h>

#include <cmath>
#endif

namespace PocketLock {

#if CROSSPOINT_PAPERS3
namespace {
bool available = false;
bool calibrated = false;
bool locked = false;
bool orientationChanged = false;
bool calibrationSession = false;
uint8_t currentOrientation = CrossPointSettings::PORTRAIT;
uint32_t lastPollMs = 0;
uint32_t candidateSinceMs = 0;

AccelSample poses[4];
constexpr uint32_t POLL_INTERVAL_MS = 100;
constexpr uint32_t HOLD_MS = 450;
constexpr float AXIS_MIN_G = 0.45f;
constexpr float DOT_ENTER = 0.72f;
constexpr float DOT_EXIT = 0.45f;

float dot3(const AccelSample& a, const AccelSample& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float mag3(const AccelSample& a) { return std::sqrt(dot3(a, a)); }
AccelSample normalized(AccelSample s) {
  const float m = mag3(s);
  if (m > 0.001f) {
    s.x /= m;
    s.y /= m;
    s.z /= m;
  }
  return s;
}

void saveCalibration() {
  Preferences p;
  if (!p.begin("paperpoint-imu", false)) return;
  p.putBool("valid", true);
  p.putBytes("poses", poses, sizeof(poses));
  p.end();
}

void loadCalibration() {
  Preferences p;
  if (!p.begin("paperpoint-imu", true)) return;
  calibrated = p.getBool("valid", false) && p.getBytesLength("poses") == sizeof(poses);
  if (calibrated) p.getBytes("poses", poses, sizeof(poses));
  p.end();
  if (calibrated) LOG_INF("IMU", "Four-pose calibration loaded");
}

bool averageStable(AccelSample& out) {
  constexpr int N = 24;
  AccelSample sum{};
  AccelSample first{};
  float maxDelta = 0.0f;
  int got = 0;
  for (int i = 0; i < N; ++i) {
    if (M5.Imu.update()) {
      const auto d = M5.Imu.getImuData();
      AccelSample s{d.accel.x, d.accel.y, d.accel.z};
      if (got == 0) first = s;
      const float dx = s.x - first.x, dy = s.y - first.y, dz = s.z - first.z;
      maxDelta = std::max(maxDelta, std::sqrt(dx * dx + dy * dy + dz * dz));
      sum.x += s.x;
      sum.y += s.y;
      sum.z += s.z;
      ++got;
    }
    delay(12);
  }
  if (got < N / 2 || maxDelta > 0.18f) return false;
  const AccelSample average{sum.x / got, sum.y / got, sum.z / got};
  const float averageMagnitude = mag3(average);
  if (averageMagnitude < 0.65f || averageMagnitude > 1.35f) return false;
  out = normalized(average);
  return true;
}
}  // namespace
#endif

void begin() {
#if CROSSPOINT_PAPERS3
  // Paper S3 places BMI270 and GT911 on the same physical I2C bus
  // (SDA=41, SCL=42).  This project does not call M5.begin(), therefore
  // M5Unified's internal I2C object is not initialized automatically.
  // Bind it explicitly to I2C controller 1, which is also used by HalTouch.
  M5.In_I2C.begin(I2C_NUM_1, 41, 42);
  if (!M5.In_I2C.isEnabled()) {
    available = false;
    LOG_ERR("IMU", "Paper S3 internal I2C init failed (port=1 SDA=41 SCL=42)");
    return;
  }

  available = M5.Imu.begin(&M5.In_I2C, m5::board_t::board_M5PaperS3);
  if (!available) {
    LOG_ERR("IMU", "BMI270 unavailable at address 0x68/0x69");
    return;
  }
  loadCalibration();
  LOG_INF("IMU", "BMI270 available calibrated=%d", calibrated ? 1 : 0);
#endif
}

bool readSample(AccelSample& sample) {
#if CROSSPOINT_PAPERS3
  if (!available || !M5.Imu.update()) return false;
  const auto d = M5.Imu.getImuData();
  sample = {d.accel.x, d.accel.y, d.accel.z};
  return true;
#else
  return false;
#endif
}

bool captureCalibrationPose(CalibrationPose pose) {
#if CROSSPOINT_PAPERS3
  if (!available) return false;
  AccelSample sample;
  if (!averageStable(sample)) return false;
  poses[static_cast<uint8_t>(pose)] = sample;
  LOG_INF("IMU", "Calibration pose=%u x=%.3f y=%.3f z=%.3f", static_cast<unsigned>(pose), sample.x, sample.y, sample.z);
  if (pose == CalibrationPose::FaceDown) {
    const float uprightVsInverted = dot3(poses[0], poses[1]);
    const float upVsDown = dot3(poses[2], poses[3]);
    calibrated = uprightVsInverted < -0.55f && upVsDown < -0.55f;
    if (calibrated) saveCalibration();
    LOG_INF("IMU", "Calibration complete valid=%d portraitDot=%.2f flatDot=%.2f", calibrated, uprightVsInverted,
            upVsDown);
  }
  return pose != CalibrationPose::FaceDown || calibrated;
#else
  return false;
#endif
}

FaceDownDetection pollFaceDownDetection() {
#if CROSSPOINT_PAPERS3
  if (!available) return FaceDownDetection::NoSample;

  AccelSample raw;
  // M5.Imu.update() returns false between BMI270 data-ready events.  That is
  // not a posture change and must not reset the fourth-step stability timer.
  if (!readSample(raw)) return FaceDownDetection::NoSample;

  if (mag3(raw) < AXIS_MIN_G) return FaceDownDetection::NotFaceDown;
  const AccelSample sample = normalized(raw);
  const float faceUpDot = dot3(sample, poses[static_cast<uint8_t>(CalibrationPose::FaceUp)]);
  return faceUpDot < -0.72f ? FaceDownDetection::FaceDown : FaceDownDetection::NotFaceDown;
#else
  return FaceDownDetection::NoSample;
#endif
}

bool isFaceDownCandidate() { return pollFaceDownDetection() == FaceDownDetection::FaceDown; }

void clearCalibration() {
#if CROSSPOINT_PAPERS3
  clearStoredCalibration();
  calibrated = false;
  locked = false;
#endif
}

void clearStoredCalibration() {
#if CROSSPOINT_PAPERS3
  Preferences p;
  if (p.begin("paperpoint-imu", false)) {
    p.clear();
    p.end();
  }
#endif
}

void update() {
#if CROSSPOINT_PAPERS3
  locked = false;
  if (calibrationSession || !available || !calibrated) return;
  const uint32_t now = millis();
  if (now - lastPollMs < POLL_INTERVAL_MS) return;
  lastPollMs = now;

  AccelSample raw;
  if (!readSample(raw) || mag3(raw) < AXIS_MIN_G) return;
  const AccelSample s = normalized(raw);
  const float uprightDot = dot3(s, poses[0]);
  const float invertedDot = dot3(s, poses[1]);
  const bool invertedCandidate = invertedDot > DOT_ENTER && uprightDot < -DOT_EXIT;
  const bool uprightCandidate = uprightDot > DOT_ENTER && invertedDot < -DOT_EXIT;

  const uint8_t mode = SETTINGS.readerOrientationMode;
  if (mode == CrossPointSettings::READER_ORIENTATION_AUTO) {
    const uint8_t wanted = invertedCandidate  ? CrossPointSettings::INVERTED
                           : uprightCandidate ? CrossPointSettings::PORTRAIT
                                              : currentOrientation;
    if (wanted != currentOrientation) {
      if (candidateSinceMs == 0) candidateSinceMs = now;
      if (now - candidateSinceMs >= HOLD_MS) {
        currentOrientation = wanted;
        orientationChanged = true;
        candidateSinceMs = 0;
        LOG_INF("IMU", "Auto reader orientation=%u", currentOrientation);
      }
    } else
      candidateSinceMs = 0;
  } else {
    currentOrientation = mode == CrossPointSettings::READER_ORIENTATION_FIXED_180 ? CrossPointSettings::INVERTED
                                                                                  : CrossPointSettings::PORTRAIT;
    candidateSinceMs = 0;
    if (SETTINGS.readerInversionLock) {
      locked = (currentOrientation == CrossPointSettings::PORTRAIT) ? invertedCandidate : uprightCandidate;
    }
  }
#endif
}

void beginCalibrationSession() {
#if CROSSPOINT_PAPERS3
  calibrationSession = true;
  locked = false;
  candidateSinceMs = 0;
#endif
}

void endCalibrationSession(bool completedSuccessfully) {
#if CROSSPOINT_PAPERS3
  calibrationSession = false;
  locked = false;
  candidateSinceMs = 0;
  if (!completedSuccessfully) {
    // Restore the last valid saved calibration after a cancelled/failed retry.
    loadCalibration();
  }
#endif
}

bool isAvailable() {
#if CROSSPOINT_PAPERS3
  return available;
#else
  return false;
#endif
}
bool isCalibrated() {
#if CROSSPOINT_PAPERS3
  return calibrated;
#else
  return false;
#endif
}
bool isLocked() {
#if CROSSPOINT_PAPERS3
  return available && calibrated && locked;
#else
  return false;
#endif
}
uint8_t desiredOrientation() {
#if CROSSPOINT_PAPERS3
  return currentOrientation;
#else
  return 0;
#endif
}
bool consumeOrientationChanged() {
#if CROSSPOINT_PAPERS3
  const bool changed = orientationChanged;
  orientationChanged = false;
  return changed;
#else
  return false;
#endif
}

}  // namespace PocketLock
