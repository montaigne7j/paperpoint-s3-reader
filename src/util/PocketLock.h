#pragma once

#include <stdint.h>

namespace PocketLock {

enum class CalibrationPose : uint8_t { Upright = 0, Inverted = 1, FaceUp = 2, FaceDown = 3 };

// A polling result must distinguish "no fresh BMI270 sample yet" from
// "a fresh sample says the device is not face down".  Treating both as
// false resets the fourth-step hold timer on every sensor data interval.
enum class FaceDownDetection : uint8_t { NoSample = 0, NotFaceDown = 1, FaceDown = 2 };

struct AccelSample {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

void begin();
void update();
void beginCalibrationSession();
void endCalibrationSession(bool completedSuccessfully);
bool isAvailable();
bool isCalibrated();
bool isLocked();
uint8_t desiredOrientation();
bool consumeOrientationChanged();
bool readSample(AccelSample& sample);
bool captureCalibrationPose(CalibrationPose pose);
FaceDownDetection pollFaceDownDetection();
bool isFaceDownCandidate();
void clearCalibration();
void clearStoredCalibration();

}  // namespace PocketLock
