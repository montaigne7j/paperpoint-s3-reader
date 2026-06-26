# Compile fix: JPEGDEC `File` type in SleepImageCache

## Problem

PlatformIO compiles `lib/SleepImageCache/SleepImageManager.cpp` as an independent library translation unit.
`JPEGDEC.h` declares an overload using Arduino's `File` type:

```cpp
int open(File &file, JPEG_DRAW_CALLBACK *pfnDraw);
```

On Arduino-ESP32 3.x, `File` may only be available as `fs::File` unless it is explicitly imported into the global namespace before `JPEGDEC.h` is parsed. Including `FS.h` alone was not sufficient in this build.

## Fix

`SleepImageManager.cpp` now includes Arduino/FS before JPEGDEC and explicitly exposes the type:

```cpp
#include <Arduino.h>
#include <FS.h>
using fs::File;
#include <JPEGDEC.h>
```

The PNG/JPEG macro redefinition cleanup remains after `JPEGDEC.h` and before `PNGdec.h`.
