# Built-in User Manual EPUB

This build embeds a small EPUB manual in firmware.

When the File Browser opens `/book`, the firmware checks whether the manual exists at:

```text
/book/CrossPoint_User_Manual.epub
```

If the file is missing, or if its size does not match the firmware copy, it is written to SD storage automatically. It then appears in the File Browser together with the user's books and opens through the normal EPUB reader path.

The manual content is bilingual:

1. Traditional Chinese first.
2. English second.

It covers home/menu operation, settings, direct touch selection, footer button behavior, and reading page operation.

## V1.7.0 status

The manual is still embedded in firmware as `src/resources/BuiltinManualEpub.cpp` and is installed automatically to `/book/CrossPoint_User_Manual.epub` on the SD card.  A copy of the generated EPUB is also kept at `docs/CrossPoint_User_Manual.epub` for release review.

The V1.7.0 manual text mentions the pending page-turn guard, neighboring frame cache behavior, and the band-scan page-turn refresh mode.
