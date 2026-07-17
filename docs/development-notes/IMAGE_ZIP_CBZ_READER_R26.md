# r26 Image ZIP / CBZ Reader

This update adds a dedicated image ZIP / CBZ reader for Paper S3.

## Supported files

- `.cbz`
- `.zip` containing image pages
- Image entries: `.jpg`, `.jpeg`, `.png`, `.bmp`

## Reader behavior

- The File Browser lists `.zip` and `.cbz` files.
- ReaderActivity dispatches `.zip` / `.cbz` to `CbzReaderActivity`.
- ZIP central-directory entries are scanned once on open.
- Image entries are naturally sorted, so `2.jpg` comes before `10.jpg`.
- One image is extracted at a time to `/.crosspoint/cbz_<hash>/page.<ext>`.
- Progress is saved to `/.crosspoint/cbz_<hash>/progress.bin`.

## Layout policy

Image ZIP / CBZ pages are treated as image pages, not reader text pages:

- Reader background PNG is not applied.
- Reader content margins / screen margins are not applied to the image area.
- A fixed bottom status area is always reserved.
- Status-bar content follows the current user settings, including page count, book progress percentage, progress bar, title, battery, and clock.
- Book title maps to the archive filename; chapter title maps to the current image filename.

## Limits

- Password-protected ZIP files are not supported.
- RAR / CBR is not supported.
- ZIP64 is not supported by the existing `ZipFile` parser.
- Streaming directly from ZIP into JPEG/PNG decoders is not implemented yet; pages are extracted to a temporary file first.
