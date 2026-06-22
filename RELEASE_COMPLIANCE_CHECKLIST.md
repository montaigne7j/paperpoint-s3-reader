# Release Compliance Checklist

Run this checklist for every public firmware release.

1. Run `python scripts/check_license_compliance.py`.
2. Build from a clean checkout with `pio run -e gh_release`.
3. Generate the release SBOM:
   `python scripts/generate_sbom.py --firmware .pio/build/gh_release/firmware.bin --output dist/SBOM.spdx.json`.
4. Collect resolved dependency licences:
   `python scripts/collect_dependency_licenses.py --env gh_release`.
5. Archive the exact resolved LGPL component sources:
   `python scripts/package_lgpl_sources.py --env gh_release --output dist/lgpl-component-sources.zip`.
6. Create the LGPL relink kit:
   `python scripts/package_lgpl_relink_kit.py --env gh_release --output dist/lgpl-relink-kit.zip`.
7. Create a Complete Corresponding Application Source archive from the exact tagged commit.
8. Publish the firmware, application source archive, LGPL component source
   archive, relink kit, SBOM, root license,
   `THIRD_PARTY_NOTICES.md`, all files under `LICENSES/`, collected upstream
   dependency licence/notice files, and `BINARY_RELEASE_LGPL_COMPLIANCE.md`
   together.
9. Verify that download links remain available for at least the period promised
   in the written source offer.
10. Record the release tag, source commit and artifact SHA-256 values in the
   release notes.

A passing automated check is evidence that the repository's mechanical controls
are present. It is not a legal opinion and does not replace review of newly
added dependencies or assets.
