# Licence compliance self-assessment

Assessment date: 2026-06-20

## Requested controls and outcome

| Requirement | Result | Evidence |
|---|---|---|
| Remove the non-redistributable reading font | **Pass** | Source TTF files, generated headers, source references, settings options, conversion-script references and documentation references were removed; the automated checker rejects reintroduction |
| Preserve existing user settings | **Pass** | Settings schema version 2 migrates the removed family and legacy Noto value to Noto Sans, and migrates the former dyslexia-font value to ReaderDyslexic |
| EPD_Painter Apache-2.0 compliance | **Pass with provenance caveat** | Full Apache-2.0 text, upstream/source attribution, NOTICE, SPDX headers and local modification date were added throughout the bundled source |
| GC16 FreeBSD/BSD notice | **Pass** | Copyright, BSD-2-Clause identifier, source attribution, modification statement and complete terms were added |
| Central notices and licence bundle | **Pass** | `THIRD_PARTY_NOTICES.md` and the `LICENSES/` directory were added and are required by the automated check |
| LGPL binary-release method | **Process implemented; each release must execute it** | The workflow publishes application source, exact resolved LGPL component sources, object/relink material, SBOM, notices, upstream licence files and checksums |
| Hyphenation pattern licensing | **Pass for the seven included languages** | Per-language authorship/licences are recorded in `HYPHENATION_LICENSES.md`; generated trie headers identify source release and applicable licence |
| Generated-font notices and naming | **Pass** | Noto data carries OFL notice; the OpenDyslexic-derived generated data is renamed ReaderDyslexic; the Ubuntu generated derivative has a distinct derivative name and UFL notice |
| Embedded Traditional Chinese font | **Pass** | PaperPoint Sans TC Medium uses a distinct derivative name, OFL-1.1 notice, source-raster SHA-256, generator, sparse Unicode intervals and checked-in provenance |
| Image and logo inventory | **Partial—inventory complete, title clearance incomplete** | Every current file under `src/images` and `docs/images` is listed with a SHA-256 value, but some inherited assets lack authoritative creator/source records |
| Exact PlatformIO dependency versions | **Pass** | All caret ranges were replaced with exact registry versions, immutable commit identifiers or an explicit tag; the compliance check rejects new caret ranges |
| SBOM for every release | **Pass as an automated control** | SPDX 2.3 generator, checked-in current SBOM and tag-triggered release workflow were added; the release SBOM includes the final firmware checksum |

## Implemented release artifacts

A compliant release workflow now produces and publishes together:

1. `firmware.bin`.
2. `complete-application-source.zip` for the application source at the exact tag.
3. `lgpl-component-sources.zip` containing the resolved ArduinoWebSockets and Arduino-ESP32 source package trees used by that build.
4. `lgpl-relink-kit.zip` containing object files, static archives, ELF/map output and build configuration.
5. `SBOM.spdx.json` with a firmware SHA-256 annotation.
6. `license-bundle.zip`, upstream dependency licence files and `SHA256SUMS.txt`.

The release automation intentionally fails when the resolved source trees,
licence files or object files are missing rather than producing an incomplete
compliance package.

## Verification performed

- `scripts/check_license_compliance.py`: passed.
- Python syntax compilation for the new and modified compliance scripts: passed.
- Shell syntax checks for modified shell scripts: passed.
- YAML parse of `.github/workflows/release-compliance.yml`: passed.
- I18n regeneration: passed with 319 string keys.
- JSON parse and SPDX package/relationship integrity checks for `SBOM.spdx.json`: passed.
- Search for the removed font name in filenames, source and configuration: no matches.
- Generated font include-target and licence-header checks: passed.
- Embedded CJK raster SHA-256, generated-header metadata, Unicode interval ordering and representative glyph reconstruction checks: passed.
- EPD_Painter source SPDX-header scan: passed.
- Synthetic tests of the dependency-licence collector, LGPL component-source packager and relink-kit packager: passed.
- Final project ZIP integrity and exclusion checks: performed as part of delivery.

A full PlatformIO firmware build was attempted. This execution environment could
not resolve the GitHub host needed to download the pinned pioarduino platform
archive, so the build stopped before compilation. This is an external network
failure, not evidence that the modified firmware compiles. A successful
`pio run -e default` or `pio run -e gh_release` remains required on the user's
normal build machine and in the release workflow.

## Remaining limitations

1. Several inherited logos/screenshots still require authoritative creator and
   source confirmation before they should be used for commercial branding or
   marketing. Inventory alone does not establish ownership.
2. The EPD_Painter attribution is based on the identified upstream project.
   Future imports should record an exact upstream commit, not only a repository
   URL.
3. LGPL compliance is not achieved by keeping these scripts in the repository.
   It becomes effective for a particular binary only when all matching source,
   relink, licence and SBOM artifacts are actually published and kept available
   under the documented terms.
4. Secure boot, encrypted flash, locked bootloaders or product terms that prevent
   debugging/reinstallation can change the LGPL analysis and require separate
   legal review.
5. This is an engineering compliance assessment, not a legal opinion.

6. The embedded CJK source raster is kept under a distinct derivative name and fixed SHA-256; future replacements should record the same source filename, licence and checksum details before release.

## Overall assessment

The requested repository-level remediation is substantially complete. The major
known proprietary-font distribution risk has been removed, third-party notices
are materially stronger, dependency resolution is reproducible, and release
artifacts are designed to satisfy the source-and-relink obligations associated
with the identified LGPL components.

The project is **not yet unconditionally cleared for public commercial binary
distribution**. Conditional readiness depends on two external actions: a
successful release build that publishes every generated compliance artifact,
and confirmation or replacement of the visual assets marked as having unknown
provenance.
