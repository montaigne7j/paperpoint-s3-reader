# Compile fix: ReaderValueAdjustActivity dynamic title

## Problem

`ReaderValueAdjustActivity::render()` used `tr(titleId)`, but the `tr()` macro only accepts a literal `StrId` enum name and expands to `StrId::<argument>`.

When `titleId` is a runtime variable, the macro expands incorrectly and causes:

```text
error: 'titleId' is not a member of 'StrId'
```

## Fix

Changed the header title lookup to call the i18n instance directly:

```cpp
I18N.get(titleId)
```

This supports runtime `StrId` values while preserving the existing translations.
