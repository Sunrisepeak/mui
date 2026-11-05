# Foundation namespace migration consumer

Run `python3 tools/ci/foundation-migration/run.py` from a checkout with the
project's mcpp toolchain installed. `--mcpp /absolute/path/to/mcpp` pins the
executable when temporary projects would select a different launcher.

The gate packs hash, math, log, trace and testing, then builds six independent
release consumers: source or compiled archives, each with canonical, legacy
or mixed imports and dependency keys. The mixed form must resolve one copy
of each library. Runtime checks cover shared log and trace state as well as
type/function identities. No GUI or display server is required.

Exit zero and the final JSON `ok: true` mean all checks passed. The default
report is `target/foundation-migration/report.omui.json`; its adjacent
`artifacts/` directory contains command logs, dependency graphs and packed
manifests. Use `--output` to select another report and `--keep-project` to
retain generated projects for diagnosis. A failed invocation returns nonzero
and records the failing command; correct its cause and rerun. Temporary
projects own their resolution; the repository lock must stay unchanged.

This is a source compatibility gate after rebuilding, not an ABI or registry
alias test. The fixture deliberately depends on the testing library; separate
production consumers audit its absence. The current mcpp pack output omits
testing's hash dependency, so the archive fixture explicitly supplies both;
single-dependency publication remains unverified. Fresh project timings may
reuse global compiler caches. See the [migration design](../../../.agents/docs/2026-11-05-product-namespace-migration-design.md).
