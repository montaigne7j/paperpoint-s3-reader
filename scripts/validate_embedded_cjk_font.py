#!/usr/bin/env python3
"""Validate the checked-in embedded Traditional Chinese font artifact."""
from __future__ import annotations

import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "lib/EpdFont/builtinFonts/source/PaperPointSansTC/PaperPointSansTC-Medium-15_5pt-21x30.bin"
HEADER = ROOT / "lib/EpdFont/builtinFonts/paperpoint_sans_tc_15_5_medium.h"
EXPECTED_SHA = "bf143dd1bb632af7af6107dc4e32e8426e5cd375580a77300982a3f697dcb6fc"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    if SOURCE.stat().st_size != 65_536 * 3 * 30:
        raise SystemExit(f"Unexpected legacy raster size: {SOURCE.stat().st_size}")
    source_sha = sha256(SOURCE)
    if source_sha != EXPECTED_SHA:
        raise SystemExit(f"Source raster SHA-256 mismatch: {source_sha}")

    body = HEADER.read_text(encoding="utf-8")
    for token in (
        "SPDX-License-Identifier: OFL-1.1",
        "Noto Sans CJK TC Medium",
        EXPECTED_SHA,
        "Retained glyphs: 31338; intervals: 37",
        "Cropped packed bitmap bytes: 1444571",
        "// U+3000",
        "// U+3105",
        "// U+4E2D",
        "// U+9AD4",
    ):
        if token not in body:
            raise SystemExit(f"Generated header is missing expected token: {token}")
    if "// U+0041" in body:
        raise SystemExit("Latin U+0041 must remain served by proportional built-in fonts")


    renderer_body = (ROOT / "lib/GfxRenderer/GfxRenderer.cpp").read_text(encoding="utf-8")
    for token in (
        "COMPACT_CJK_SCALE_NUM = 1",
        "renderBuiltinFallbackGlyphScaled",
        "fallbackAdvancePixels",
        "fallbackMetricPixels",
    ):
        if token not in renderer_body:
            raise SystemExit(f"Render-time compact CJK scaling is missing expected token: {token}")


    with tempfile.TemporaryDirectory() as temp_dir:
        regenerated = Path(temp_dir) / HEADER.name
        subprocess.run(
            [
                sys.executable,
                str(ROOT / "scripts/embed_legacy_cjk_font.py"),
                str(SOURCE),
                str(regenerated),
                "--name",
                "paperpoint_sans_tc_15_5_medium",
                "--width",
                "21",
                "--height",
                "30",
                "--advance",
                "21",
                "--source-sha256",
                EXPECTED_SHA,
            ],
            check=True,
            stdout=subprocess.DEVNULL,
        )
        regenerated_text = regenerated.read_text(encoding="utf-8").replace("\r\n", "\n")
        header_text = HEADER.read_text(encoding="utf-8").replace("\r\n", "\n")
        if regenerated_text != header_text:
            raise SystemExit("Generated CJK header is not reproducible from the checked-in raster")

    print("Embedded CJK font validation passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
