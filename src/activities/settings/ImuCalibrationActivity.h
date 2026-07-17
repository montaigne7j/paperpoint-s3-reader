#pragma once

#include "activities/Activity.h"

class ImuCalibrationActivity final : public Activity {
 public:
  ImuCalibrationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ImuCalibration", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;
  bool preventAutoSleep() override { return true; }

 private:
  uint8_t step = 0;
  bool unstable = false;
  bool completed = false;
  bool sampling = false;
  bool imuUnavailable = false;
  uint32_t faceDownStableSince = 0;
  uint32_t faceDownLastSampleAt = 0;
};
