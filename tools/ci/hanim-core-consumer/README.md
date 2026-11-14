# Independent hanim.core + openmui.log consumer

This fixture is materialized in a fresh temporary directory outside the
repository workspace by `python3 tools/ci/hanim-core-consumer.py`. Its only
manifest dependencies are public `hanim.core` and `openmui.log` paths, without
dev dependencies, UI, specification, testing, site, host or renderer packages.
It imports no compatibility umbrella.

The actual executable prepares a triangle → four-cubic-circle correspondence,
samples one connected contour at six unordered times, compares bulk and
streaming cubics, verifies original endpoint boundaries, and requires repeated
seek results to keep the same bits. A two-channel `Timeline` supplies progress
and translation. A real independent log category writes through a custom sink;
the sink reads the actual history and emits its existing JSON-lines format.
The gate parses eight real log records and the executable's result.

```sh
python3 tools/ci/hanim-core-consumer.py
# Retain the fresh project and write evidence outside the repository:
python3 tools/ci/hanim-core-consumer.py --keep-project --output /tmp/hanim-consumer.report.omui.json
```

`--mcpp PATH` or the `MCPP` environment variable selects the executable. It
uses the installed/default toolchain and runs dev and release, without a
second test runner or a UI test harness. The new temporary project may resolve
its own dependency lock on the initial build; repository locks remain read
only. With mcpp 2026.11.2.2 this entirely local graph requires no lock file,
and subsequent `--locked` runs preserve that absence.

After each real run, `mcpp build --locked --profile <profile> --configure-only`
emits the actual configured graph and compilation database. The gate audits
mcpp's schema-2 `resolution.json`: exactly the consumer, `hanim.core`,
`openmui.math` and `openmui.log`, all path packages. It rejects extra packages
and repository CDB sources outside those three library roots. This is evidence
from actual resolution/building, rather than trusting the fixture manifest.
Reports retain graph snapshots, commands, build logs and parsed program output;
temporary build artifacts are removed unless `--keep-project` was requested.

This is independent consumption of the current core geometry, timeline and
logger APIs. It does not implement or accept HObject, Engine/A1, a renderer,
pixel-domain correspondence, a complete animation catalog, allocation budgets
or physical GPU performance. The separate public object consumer can extend
this evidence when that API is ready.
