#!/usr/bin/env python3
"""Generate an SPDX 2.3 JSON SBOM for a PaperPoint firmware release.

The generator is intentionally dependency-free so it can run in local builds and
GitHub Actions. It records the exact versions pinned in platformio.ini and the
third-party code/data stored directly in this repository.
"""
from __future__ import annotations

import argparse
import configparser
import datetime as dt
import hashlib
import json
import re
import subprocess
import uuid
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
PROJECT_URL = "https://github.com/montaigne7j/paperpoint-s3-reader"


def git_value(*args: str, default: str = "NOASSERTION") -> str:
    try:
        value = subprocess.check_output(
            ["git", "-C", str(ROOT), *args], text=True, stderr=subprocess.DEVNULL
        ).strip()
        return value or default
    except (OSError, subprocess.CalledProcessError):
        return default


def read_project_version() -> str:
    text = (ROOT / "platformio.ini").read_text(encoding="utf-8")
    match = re.search(r"(?ms)^\[crosspoint\].*?^version\s*=\s*([^\s;]+)", text)
    if not match:
        raise RuntimeError("Unable to read [crosspoint] version from platformio.ini")
    return match.group(1)


def read_lib_deps() -> list[str]:
    text = (ROOT / "platformio.ini").read_text(encoding="utf-8")
    in_base = False
    collecting = False
    deps: list[str] = []
    for raw in text.splitlines():
        line = raw.rstrip()
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            in_base = stripped == "[base]"
            collecting = False
            continue
        if not in_base:
            continue
        if stripped.startswith("lib_deps") and "=" in stripped:
            collecting = True
            tail = stripped.split("=", 1)[1].strip()
            if tail:
                deps.append(tail)
            continue
        if collecting:
            if not line.startswith((" ", "\t")) or not stripped:
                collecting = False
                continue
            if not stripped.startswith((";", "#")):
                deps.append(stripped)
    return deps


def safe_id(name: str) -> str:
    return "SPDXRef-Package-" + re.sub(r"[^A-Za-z0-9.-]", "-", name).strip("-")


def package(
    name: str,
    version: str,
    license_id: str,
    download: str,
    supplier: str = "NOASSERTION",
    purl: str | None = None,
    comment: str | None = None,
) -> dict[str, Any]:
    item: dict[str, Any] = {
        "SPDXID": safe_id(name),
        "name": name,
        "versionInfo": version,
        "downloadLocation": download,
        "filesAnalyzed": False,
        "licenseConcluded": license_id,
        "licenseDeclared": license_id,
        "copyrightText": "NOASSERTION",
        "supplier": supplier,
    }
    if purl:
        item["externalRefs"] = [
            {
                "referenceCategory": "PACKAGE-MANAGER",
                "referenceType": "purl",
                "referenceLocator": purl,
            }
        ]
    if comment:
        item["comment"] = comment
    return item


def parse_dep(spec: str) -> dict[str, Any]:
    if spec.startswith("http://") or spec.startswith("https://"):
        url, _, ref = spec.partition("#")
        repo = url.rstrip("/").rsplit("/", 1)[-1].removesuffix(".git")
        owner = url.rstrip("/").rsplit("/", 2)[-2]
        version = ref or "NOASSERTION"
        known = {
            "JPEGDEC": "Apache-2.0",
            "OpenFontRender": "FTL",
        }
        return package(
            repo,
            version,
            known.get(repo, "NOASSERTION"),
            spec,
            supplier=f"Organization: {owner}",
            purl=f"pkg:github/{owner}/{repo}@{version}" if ref else None,
        )

    match = re.fullmatch(r"([^/\s]+)/([^@\s]+)\s*@\s*([^\s]+)", spec)
    if not match:
        return package(spec, "NOASSERTION", "NOASSERTION", "NOASSERTION")
    owner, name, version = match.groups()
    known = {
        "SdFat": "MIT",
        "ArduinoJson": "MIT",
        "QRCode": "MIT",
        "PNGdec": "Apache-2.0",
        "WebSockets": "LGPL-2.1-only",
        "M5Unified": "MIT",
        "M5GFX": "NOASSERTION",
    }
    return package(
        name,
        version,
        known.get(name, "NOASSERTION"),
        f"https://registry.platformio.org/libraries/{owner}/{name}",
        supplier=f"Organization: {owner}",
        purl=f"pkg:platformio/{owner}/{name}@{version}",
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="SBOM.spdx.json")
    parser.add_argument("--firmware", help="Optional firmware file to checksum")
    args = parser.parse_args()

    version = read_project_version()
    commit = git_value("rev-parse", "HEAD")
    created = dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")
    project = package(
        "PaperPoint-S3-Reader",
        version,
        "MIT",
        PROJECT_URL,
        supplier="Organization: PaperPoint contributors",
        comment=f"Source revision: {commit}",
    )
    project["SPDXID"] = "SPDXRef-Package-PaperPoint-S3-Reader"

    packages = [project]
    packages.extend(parse_dep(dep) for dep in read_lib_deps())
    packages.extend(
        [
            package(
                "pioarduino-platform-espressif32",
                "55.03.37",
                "NOASSERTION",
                "https://github.com/pioarduino/platform-espressif32/releases/tag/55.03.37",
                supplier="Organization: pioarduino",
                purl="pkg:github/pioarduino/platform-espressif32@55.03.37",
            ),
            package(
                "Arduino-ESP32",
                "3.3.7",
                "LGPL-2.1-only",
                "https://github.com/espressif/arduino-esp32",
                supplier="Organization: Espressif Systems",
                purl="pkg:github/espressif/arduino-esp32@3.3.7",
            ),
            package(
                "EPD_Painter",
                "local-modified",
                "Apache-2.0",
                "https://github.com/tonywestonuk/EPD_Painter",
                supplier="Person: Tony Weston and contributors",
                comment="Locally modified; see lib/EPD_Painter/NOTICE.",
            ),
            package(
                "LovyanGFX-GC16-waveform",
                "adapted",
                "BSD-2-Clause",
                "https://github.com/lovyan03/LovyanGFX",
                supplier="Person: lovyan03",
                comment="Adapted waveform table; see the source header and LICENSES/BSD-2-Clause-LovyanGFX.txt.",
            ),
            package(
                "Hypher-pattern-data",
                "v0.1.7",
                "NOASSERTION",
                "https://github.com/typst/hypher/tree/v0.1.7",
                supplier="Organization: Typst contributors and pattern authors",
                comment="Per-language licensing is recorded in HYPHENATION_LICENSES.md.",
            ),
            package(
                "PaperPoint-Sans-TC-generated-font-data",
                "raster-0d75d0abcea1f3ce12512686fa5cfb4140cc8066fc68095aab271678e081f34a",
                "OFL-1.1",
                "https://github.com/notofonts/noto-cjk",
                supplier="Organization: Adobe and Noto CJK contributors",
                comment=(
                    "Distinctly named sparse/cropped bitmap derivative generated from "
                    "the maintainer-supplied Noto Sans CJK TC Medium 23.5pt 31x39 raster; "
                    "source raster SHA-256 is recorded in BUILTIN_CJK_FONT.md."
                ),
            ),
            package(
                "Noto-Sans-generated-font-data",
                "repository-copy",
                "OFL-1.1",
                "NOASSERTION",
                supplier="Organization: Google",
            ),
            package(
                "Ubuntu-PaperPoint-generated-font-data",
                "repository-copy",
                "Ubuntu-font-1.0",
                "NOASSERTION",
                comment="Renamed generated derivative; original source fonts are retained with UFL.txt.",
            ),
        ]
    )

    relationships = [
        {
            "spdxElementId": "SPDXRef-DOCUMENT",
            "relationshipType": "DESCRIBES",
            "relatedSpdxElement": project["SPDXID"],
        }
    ]
    relationships.extend(
        {
            "spdxElementId": project["SPDXID"],
            "relationshipType": "DEPENDS_ON",
            "relatedSpdxElement": item["SPDXID"],
        }
        for item in packages[1:]
    )

    annotations: list[dict[str, str]] = []
    if args.firmware:
        firmware = Path(args.firmware)
        if not firmware.is_absolute():
            firmware = ROOT / firmware
        if not firmware.is_file():
            raise FileNotFoundError(f"Firmware not found: {firmware}")
        digest = hashlib.sha256(firmware.read_bytes()).hexdigest()
        annotations.append(
            {
                "annotationDate": created,
                "annotationType": "OTHER",
                "annotator": "Tool: scripts/generate_sbom.py",
                "comment": f"Firmware SHA-256 ({firmware.name}): {digest}",
            }
        )

    document = {
        "spdxVersion": "SPDX-2.3",
        "dataLicense": "CC0-1.0",
        "SPDXID": "SPDXRef-DOCUMENT",
        "name": f"PaperPoint-S3-Reader-{version}-SBOM",
        "documentNamespace": f"{PROJECT_URL}/spdx/{version}/{commit}/{uuid.uuid4()}",
        "creationInfo": {
            "created": created,
            "creators": ["Tool: scripts/generate_sbom.py"],
        },
        "documentDescribes": [project["SPDXID"]],
        "packages": packages,
        "relationships": relationships,
    }
    if annotations:
        document["annotations"] = annotations

    output = Path(args.output)
    if not output.is_absolute():
        output = ROOT / output
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(document, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
