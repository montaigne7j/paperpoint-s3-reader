#!/usr/bin/env python3
"""Fail fast when known release-license requirements regress."""
from __future__ import annotations

import hashlib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ERRORS: list[str] = []


def fail(message: str) -> None:
    ERRORS.append(message)


def require(path: str) -> Path:
    item = ROOT / path
    if not item.exists():
        fail(f"Missing required path: {path}")
    return item


def text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


required = [
    "THIRD_PARTY_NOTICES.md",
    "BUILTIN_CJK_FONT.md",
    "BUILTIN_CJK_FONT_SELF_ASSESSMENT.md",
    "HYPHENATION_LICENSES.md",
    "ASSETS_LICENSES.md",
    "BINARY_RELEASE_LGPL_COMPLIANCE.md",
    "LICENSE_COMPLIANCE_SELF_ASSESSMENT.md",
    "SBOM.spdx.json",
    "partitions.csv",
    "scripts/collect_dependency_licenses.py",
    "scripts/package_lgpl_sources.py",
    "scripts/package_lgpl_relink_kit.py",
    "scripts/merge_firmware.py",
    ".github/workflows/release.yml",
    ".github/workflows/release-compliance.yml",
    ".github/workflows/web-installer.yml",
    "docs/install/index.html",
    "LICENSES/README.md",
    "LICENSES/Apache-2.0.txt",
    "LICENSES/OFL-1.1-NotoSansCJK.txt",
    "LICENSES/CC0-1.0.txt",
    "LICENSES/LGPL-2.1.txt",
    "LICENSES/BSD-2-Clause-LovyanGFX.txt",
    "lib/EPD_Painter/LICENSE",
    "lib/EPD_Painter/NOTICE",
    "scripts/embed_legacy_cjk_font.py",
    "scripts/validate_embedded_cjk_font.py",
    "lib/EpdFont/builtinFonts/paperpoint_sans_tc_15_5_medium.h",
    "lib/EpdFont/builtinFonts/source/PaperPointSansTC/PaperPointSansTC-Medium-15_5pt-21x30.bin",
]
for item in required:
    require(item)

# Do not place the removed proprietary family name literally in this script;
# construct it so this checker itself cannot create a false positive.
forbidden = "book" + "erly"
for path in ROOT.rglob("*"):
    if not path.is_file() or any(part in {".git", ".pio", "dist"} for part in path.parts):
        continue
    if forbidden in path.name.lower():
        fail(f"Removed font remains in filename: {path.relative_to(ROOT)}")
    if path.suffix.lower() in {".cpp", ".h", ".hpp", ".c", ".ini", ".yaml", ".yml", ".py", ".sh", ".html", ".md"}:
        if forbidden in text(path).lower():
            fail(f"Removed font remains in source/config: {path.relative_to(ROOT)}")

pio = text(ROOT / "platformio.ini")
if re.search(r"@\s*\^", pio):
    fail("platformio.ini still contains a caret version range")
for expected in (
    "greiman/SdFat @ 2.3.1",
    "bitbank2/PNGdec @ 1.1.6",
    "m5stack/M5Unified @ 0.2.17",
    "m5stack/M5GFX @ 0.2.22",
):
    if expected not in pio:
        fail(f"Pinned dependency missing: {expected}")

for path in (ROOT / "lib/EpdFont/builtinFonts").glob("*.h"):
    body = text(path)
    lower_name = path.name.lower()
    if lower_name.startswith("opendyslexic_") or re.match(r"ubuntu_\d+_", lower_name):
        fail(f"Legacy generated font derivative remains: {path.relative_to(ROOT)}")
    if lower_name.startswith("notosans_") and "SPDX-License-Identifier: OFL-1.1" not in body:
        fail(f"Missing OFL notice: {path.relative_to(ROOT)}")
    if lower_name.startswith("readerdyslexic_"):
        if "SPDX-License-Identifier: OFL-1.1" not in body or "Reader Dyslexic" not in body:
            fail(f"Missing renamed-derivative notice: {path.relative_to(ROOT)}")
    if lower_name.startswith("ubuntu_derivative_paperpoint_"):
        if "SPDX-License-Identifier: Ubuntu-font-1.0" not in body or "PaperPoint" not in body:
            fail(f"Missing UFL derivative notice: {path.relative_to(ROOT)}")
    if lower_name.startswith("paperpoint_sans_tc_"):
        if (
            "SPDX-License-Identifier: OFL-1.1" not in body
            or "BUILTIN_CJK_FONT.md" not in body
            or "bf143dd1bb632af7af6107dc4e32e8426e5cd375580a77300982a3f697dcb6fc" not in body
        ):
            fail(f"Embedded CJK font notice/provenance is incomplete: {path.relative_to(ROOT)}")

cjk_source = ROOT / "lib/EpdFont/builtinFonts/source/PaperPointSansTC/PaperPointSansTC-Medium-15_5pt-21x30.bin"
if cjk_source.exists():
    digest = hashlib.sha256(cjk_source.read_bytes()).hexdigest()
    if digest != "bf143dd1bb632af7af6107dc4e32e8426e5cd375580a77300982a3f697dcb6fc":
        fail(f"Embedded CJK source raster checksum changed: {digest}")
    if cjk_source.stat().st_size != 5_898_240:
        fail(f"Embedded CJK source raster size changed: {cjk_source.stat().st_size}")

all_fonts = text(ROOT / "lib/EpdFont/builtinFonts/all.h")
if "paperpoint_sans_tc_15_5_medium.h" not in all_fonts:
    fail("Built-in font aggregate does not include PaperPoint Sans TC")

main_source = text(ROOT / "src/main.cpp")
if "renderer.setBuiltinFallbackFont(&paperpointSansTcFallbackFamily)" not in main_source:
    fail("PaperPoint Sans TC is not registered as the renderer fallback")

renderer_source = text(ROOT / "lib/GfxRenderer/GfxRenderer.cpp")
for token in ("getGlyphExact", "getTextWidthBuiltinFallback", "renderBuiltinFallbackGlyphCentered"):
    if token not in renderer_source:
        fail(f"Built-in CJK fallback integration missing: {token}")

for path in (ROOT / "lib/Epub/Epub/hyphenation/generated").glob("hyph-*.trie.h"):
    body = text(path)
    if "HYPHENATION_LICENSES.md" not in body or "v0.1.7" not in body:
        fail(f"Hyphenation provenance is incomplete: {path.relative_to(ROOT)}")

assets = text(ROOT / "ASSETS_LICENSES.md") if (ROOT / "ASSETS_LICENSES.md").exists() else ""
for base in (ROOT / "src/images", ROOT / "docs/images"):
    if not base.exists():
        continue
    for path in base.rglob("*"):
        if path.is_file() and path.relative_to(ROOT).as_posix() not in assets:
            fail(f"Visual asset missing from ASSETS_LICENSES.md: {path.relative_to(ROOT)}")

release_workflow = text(ROOT / ".github/workflows/release.yml")
if "uses: ./.github/workflows/release-compliance.yml" not in release_workflow:
    fail("release.yml must delegate to release-compliance.yml")
for forbidden_release_token in ("softprops/action-gh-release", "pio run -e gh_release", "merged-firmware.bin"):
    if forbidden_release_token in release_workflow:
        fail(f"release.yml still bypasses compliance packaging: {forbidden_release_token}")

workflow = text(ROOT / ".github/workflows/release-compliance.yml")
for required_command in (
    "workflow_call",
    "scripts/generate_sbom.py",
    "scripts/collect_dependency_licenses.py",
    "scripts/package_lgpl_sources.py",
    "scripts/package_lgpl_relink_kit.py",
    "scripts/merge_firmware.py",
    "complete-application-source.zip",
    "merged-firmware.bin",
    "lgpl-relink-kit.zip",
    "license-bundle.zip",
):
    if required_command not in workflow:
        fail(f"Release workflow is missing: {required_command}")

web_installer = text(ROOT / ".github/workflows/web-installer.yml")
for required_web_token in (
    "docs/install/compliance",
    "complete-application-source.zip",
    "lgpl-component-sources.zip",
    "lgpl-relink-kit.zip",
    "license-bundle.zip",
    "SBOM.spdx.json",
    "SHA256SUMS.txt",
):
    if required_web_token not in web_installer:
        fail(f"Web installer workflow is missing compliance artifact: {required_web_token}")

installer = text(ROOT / "docs/install/index.html")
for required_link in (
    "compliance/complete-application-source.zip",
    "compliance/lgpl-component-sources.zip",
    "compliance/lgpl-relink-kit.zip",
    "compliance/license-bundle.zip",
    "compliance/SBOM.spdx.json",
    "compliance/SHA256SUMS.txt",
):
    if required_link not in installer:
        fail(f"Installer page is missing compliance link: {required_link}")

sbom_path = ROOT / "SBOM.spdx.json"
if sbom_path.exists():
    sbom_text = text(sbom_path)
    if "paperpoint.invalid" in sbom_text:
        fail("SBOM still uses a placeholder document namespace")

if ERRORS:
    print("License compliance checks FAILED:", file=sys.stderr)
    for error in ERRORS:
        print(f"- {error}", file=sys.stderr)
    raise SystemExit(1)
print("License compliance checks passed.")
