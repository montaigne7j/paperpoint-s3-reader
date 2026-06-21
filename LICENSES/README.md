# Licence bundle

This directory contains licence texts required for source and firmware
redistribution. `THIRD_PARTY_NOTICES.md` maps components to these files.

PlatformIO dependencies can contain additional nested third-party components.
After a successful build, run:

```text
python scripts/collect_dependency_licenses.py --env gh_release
```

The release workflow stores the collected upstream `LICENSE`, `COPYING`, and
`NOTICE` files under `dist/dependency-licenses/` and includes them in the public
licence bundle. Do not replace those upstream files with summaries.
