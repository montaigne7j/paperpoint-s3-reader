# IMU calibration display commit fix (r34e)

`ImuCalibrationActivity::render()` previously drew the calibration UI into the
shared framebuffer but did not call `renderer.displayBuffer()`. The activity was
entered and its input loop ran (visible in logs), while the panel continued to
show the previous Settings screen.

The render method now submits the completed framebuffer to the e-paper display.
This also makes the sampling, unstable-reading, fourth-step detection, and
completion screens visible.
