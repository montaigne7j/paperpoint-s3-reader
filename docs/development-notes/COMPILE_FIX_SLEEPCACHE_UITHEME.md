# Compile fix: SleepImageCache -> UITheme include path

## Problem
`lib/SleepImageCache/SleepImageManager.cpp` includes `src/components/UITheme.h` from a PlatformIO library build.
When PlatformIO compiles a library, the project `src/` directory is not reliably searched the same way as source files under `src/`.

`UITheme.h` previously used:

```cpp
#include "CrossPointSettings.h"
#include "components/themes/BaseTheme.h"
```

This works when compiled from the project source tree, but fails when the header is reached indirectly from `lib/SleepImageCache`, producing:

```text
fatal error: CrossPointSettings.h: No such file or directory
```

## Fix
Make `UITheme.h` self-contained by using paths relative to its own directory:

```cpp
#include "../CrossPointSettings.h"
#include "themes/BaseTheme.h"
```

This keeps existing source builds working and also allows `SleepImageCache` to include `UITheme.h` safely.

## Note
The Windows message about terminal codepage is only a display warning and is not the build failure.
