#!/usr/bin/env python3
"""Convert a fixed-cell 1-bit BMP-Unicode .bin font into compact EpdFontData.

The legacy input layout is one row-byte-aligned bitmap for every BMP codepoint
(U+0000..U+FFFF). The output keeps only selected CJK/symbol ranges, removes the
font's repeated missing-glyph boxes, crops each retained bitmap to its ink
bounds, and packs its bits continuously for direct flash rendering.

This intentionally does not use runtime DEFLATE: cropped sparse packing gives
fast random access and avoids allocating/decompressing groups while rendering.
"""
from __future__ import annotations

import argparse
import hashlib
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

# Broad BMP coverage useful for Traditional Chinese books and UI. Latin text
# remains served by the project's proportional built-in Latin families.
DEFAULT_RANGES = [
    (0x2000, 0x206F),  # General Punctuation
    (0x20A0, 0x20CF),  # Currency Symbols
    (0x2100, 0x214F),  # Letterlike Symbols
    (0x2150, 0x218F),  # Number Forms
    (0x2190, 0x21FF),  # Arrows
    (0x2200, 0x22FF),  # Mathematical Operators
    (0x2300, 0x23FF),  # Miscellaneous Technical
    (0x2460, 0x24FF),  # Enclosed Alphanumerics
    (0x2500, 0x257F),  # Box Drawing
    (0x2580, 0x259F),  # Block Elements
    (0x25A0, 0x25FF),  # Geometric Shapes
    (0x2600, 0x26FF),  # Miscellaneous Symbols
    (0x2700, 0x27BF),  # Dingbats
    (0x27F0, 0x27FF),  # Supplemental Arrows-A
    (0x2E80, 0x2FFF),  # CJK radicals / IDS
    (0x3000, 0x303F),  # CJK Symbols and Punctuation
    (0x3040, 0x30FF),  # Hiragana and Katakana
    (0x3100, 0x312F),  # Bopomofo
    (0x3130, 0x318F),  # Hangul Compatibility Jamo
    (0x3190, 0x319F),  # Kanbun
    (0x31A0, 0x31BF),  # Bopomofo Extended
    (0x31C0, 0x31EF),  # CJK Strokes
    (0x31F0, 0x31FF),  # Katakana Phonetic Extensions
    (0x3200, 0x32FF),  # Enclosed CJK Letters and Months
    (0x3300, 0x33FF),  # CJK Compatibility
    (0x3400, 0x4DBF),  # CJK Unified Ideographs Extension A
    (0x4E00, 0x9FFF),  # CJK Unified Ideographs
    (0xF900, 0xFAFF),  # CJK Compatibility Ideographs
    (0xFE10, 0xFE1F),  # Vertical Forms
    (0xFE30, 0xFE4F),  # CJK Compatibility Forms
    (0xFE50, 0xFE6F),  # Small Form Variants
    (0xFF00, 0xFFEF),  # Halfwidth and Fullwidth Forms
]


@dataclass(frozen=True)
class Glyph:
    codepoint: int
    width: int
    height: int
    advance_fp4: int
    left: int
    top: int
    bitmap: bytes


def bit_at(raw: bytes, bytes_per_row: int, x: int, y: int) -> int:
    return (raw[y * bytes_per_row + x // 8] >> (7 - (x % 8))) & 1


def ink_bbox(raw: bytes, width: int, height: int, bytes_per_row: int):
    min_x, min_y = width, height
    max_x = max_y = -1
    for y in range(height):
        for x in range(width):
            if bit_at(raw, bytes_per_row, x, y):
                min_x = min(min_x, x)
                min_y = min(min_y, y)
                max_x = max(max_x, x)
                max_y = max(max_y, y)
    if max_x < 0:
        return None
    return min_x, min_y, max_x, max_y


def pack_crop(raw: bytes, bytes_per_row: int, bbox) -> bytes:
    if bbox is None:
        return b""
    min_x, min_y, max_x, max_y = bbox
    out = bytearray()
    current = 0
    bits = 0
    for y in range(min_y, max_y + 1):
        for x in range(min_x, max_x + 1):
            current = (current << 1) | bit_at(raw, bytes_per_row, x, y)
            bits += 1
            if bits == 8:
                out.append(current)
                current = 0
                bits = 0
    if bits:
        out.append(current << (8 - bits))
    return bytes(out)


def c_array_bytes(data: bytes, indent: str = "    ") -> str:
    if not data:
        return indent + "0x00,"
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i : i + 16]
        lines.append(indent + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    return "\n".join(lines)


def make_intervals(glyphs: list[Glyph]):
    intervals = []
    start = last = glyphs[0].codepoint
    offset = 0
    for index, glyph in enumerate(glyphs[1:], start=1):
        if glyph.codepoint == last + 1:
            last = glyph.codepoint
            continue
        intervals.append((start, last, offset))
        start = last = glyph.codepoint
        offset = index
    intervals.append((start, last, offset))
    return intervals


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--name", default="paperpoint_sans_tc_15_5_medium")
    parser.add_argument("--width", type=int, default=21)
    parser.add_argument("--height", type=int, default=30)
    parser.add_argument("--advance", type=int, default=21)
    parser.add_argument("--source-sha256")
    args = parser.parse_args()

    source = args.input.read_bytes()
    bytes_per_row = (args.width + 7) // 8
    bytes_per_char = bytes_per_row * args.height
    expected = 65536 * bytes_per_char
    if len(source) != expected:
        raise SystemExit(f"Input size {len(source)} != expected {expected}")

    digest = hashlib.sha256(source).hexdigest()
    if args.source_sha256 and digest.lower() != args.source_sha256.lower():
        raise SystemExit(f"SHA-256 mismatch: {digest}")

    raw_glyphs = [source[i * bytes_per_char : (i + 1) * bytes_per_char] for i in range(65536)]
    blank = bytes(bytes_per_char)
    counts = Counter(raw_glyphs)
    # Legacy converters fill unsupported codepoints with one or more tofu boxes.
    # Treat only very frequent non-empty patterns as missing-glyph sentinels.
    missing_patterns = {
        pattern
        for pattern, count in counts.items()
        if pattern != blank and count >= 256
    }

    selected_cps: list[int] = []
    for first, last in DEFAULT_RANGES:
        for cp in range(first, last + 1):
            raw = raw_glyphs[cp]
            if raw in missing_patterns:
                continue
            if raw == blank and cp != 0x3000:  # retain ideographic space
                continue
            selected_cps.append(cp)

    glyphs: list[Glyph] = []
    bitmap = bytearray()
    for cp in selected_cps:
        raw = raw_glyphs[cp]
        bbox = ink_bbox(raw, args.width, args.height, bytes_per_row)
        if bbox is None:
            glyph = Glyph(cp, 0, 0, args.advance << 4, 0, args.height, b"")
        else:
            min_x, min_y, max_x, max_y = bbox
            packed = pack_crop(raw, bytes_per_row, bbox)
            glyph = Glyph(
                cp,
                max_x - min_x + 1,
                max_y - min_y + 1,
                args.advance << 4,
                min_x,
                args.height - min_y,
                packed,
            )
        glyphs.append(glyph)
        bitmap.extend(glyph.bitmap)

    intervals = make_intervals(glyphs)
    out = []
    out.append("/*")
    out.append(" * SPDX-License-Identifier: OFL-1.1")
    out.append(" * PaperPoint Sans TC is a format-converted bitmap derivative of")
    out.append(" * Noto Sans CJK TC Medium. The derivative uses a distinct primary name.")
    out.append(" * Copyright 2014-2021 Adobe (http://www.adobe.com/).")
    out.append(" * Full licence: LICENSES/OFL-1.1-NotoSansCJK.txt")
    out.append(" * Provenance: BUILTIN_CJK_FONT.md")
    out.append(" */")
    out.append("/**")
    out.append(" * Generated by scripts/embed_legacy_cjk_font.py")
    out.append(f" * Source SHA-256: {digest}")
    out.append(f" * Source cell: {args.width}x{args.height}, 1-bit, BMP direct-index")
    out.append(f" * Retained glyphs: {len(glyphs)}; intervals: {len(intervals)}")
    out.append(f" * Cropped packed bitmap bytes: {len(bitmap)}")
    out.append(" */")
    out.append("#pragma once")
    out.append('#include "EpdFontData.h"')
    out.append("")
    out.append(f"static const uint8_t {args.name}Bitmaps[{max(1, len(bitmap))}] = {{")
    out.append(c_array_bytes(bytes(bitmap)))
    out.append("};")
    out.append("")
    out.append(f"static const EpdGlyph {args.name}Glyphs[{len(glyphs)}] = {{")
    offset = 0
    for g in glyphs:
        out.append(
            "    { %d, %d, %d, %d, %d, %d, %d }, // U+%04X"
            % (g.width, g.height, g.advance_fp4, g.left, g.top, len(g.bitmap), offset, g.codepoint)
        )
        offset += len(g.bitmap)
    out.append("};")
    out.append("")
    out.append(f"static const EpdUnicodeInterval {args.name}Intervals[{len(intervals)}] = {{")
    for first, last, offset in intervals:
        out.append(f"    {{ 0x{first:04X}, 0x{last:04X}, {offset} }},")
    out.append("};")
    out.append("")
    out.append(f"static const EpdFontData {args.name} = {{")
    out.append(f"    {args.name}Bitmaps,")
    out.append(f"    {args.name}Glyphs,")
    out.append(f"    {args.name}Intervals,")
    out.append(f"    {len(intervals)},")
    out.append(f"    {args.height},")
    out.append(f"    {args.height},")
    out.append("    0,")
    out.append("    false,")
    out.append("    nullptr,")
    out.append("    0,")
    out.append("    nullptr,")
    out.append("    nullptr,")
    out.append("    nullptr,")
    out.append("    nullptr,")
    out.append("    0,")
    out.append("    0,")
    out.append("    0,")
    out.append("    0,")
    out.append("    nullptr,")
    out.append("    0,")
    out.append("};")
    out.append("")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(out), encoding="utf-8")
    print(f"Wrote {args.output}")
    print(f"source_sha256={digest}")
    print(f"missing_sentinel_patterns={len(missing_patterns)}")
    print(f"glyphs={len(glyphs)} intervals={len(intervals)} bitmap_bytes={len(bitmap)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
