# Compile Fix: JPEGDEC `File is not a type`

## Symptom

PlatformIO build stops while compiling `lib/SleepImageCache/SleepImageManager.cpp`:

```text
.pio/libdeps/default/JPEGDEC/src/JPEGDEC.h:264:14: error: 'File' is not a type
```

The `INTELSHORT` / `INTELLONG` / `MOTOSHORT` / `MOTOLONG` messages around it are warnings caused by JPEGDEC and PNGdec defining the same helper macros.

## Root cause

`SleepImageManager.cpp` includes `JPEGDEC.h` directly. In this build context, JPEGDEC's optional Arduino `File` overload is visible before the ESP32 Arduino `File` type has been declared.

Other image converter translation units may compile because they pull in the filesystem type through a different include path. `SleepImageCache` is built as an independent PlatformIO library, so it needs the dependency explicitly.

## Fix

`SleepImageManager.cpp` now includes ESP32 Arduino's filesystem header before JPEGDEC:

```cpp
#include <FS.h>
#include <JPEGDEC.h>
```

The JPEGDEC byte-order helper macros are also undefined before including PNGdec, removing the macro-redefinition warnings in this file.

## Validation note

This change only affects include ordering and macro hygiene. It does not change the sleep image decoding logic.
