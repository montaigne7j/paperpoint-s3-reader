# Runtime TTF support — third-party notice

The optional runtime TrueType reader-font feature uses **OpenFontRender v1.2**
by takkaO. OpenFontRender embeds portions of FreeType and is distributed under
the FreeType Project License (FTL).

Required acknowledgement:

> Portions of this software are copyright © The FreeType Project
> (www.freetype.org). All rights reserved.

OpenFontRender project and license:

- https://github.com/takkaO/OpenFontRender/tree/v1.2
- https://github.com/takkaO/OpenFontRender/blob/v1.2/LICENSE

This project supplies its own `OFR_fopen`/`OFR_fread`/`OFR_fseek`/`OFR_ftell`
adapter so OpenFontRender reads TTF files through CrossPoint's thread-safe SD
storage layer.

Font files placed by users in `/fonts` retain their own licenses. Do not
redistribute a TTF, BIN, or EPDF font unless its license permits redistribution
and format conversion where applicable.
