# Foundation namespace migration consumer

Run `python3 tools/ci/foundation-migration/run.py` from a checkout with the
project's mcpp toolchain installed. `--mcpp /absolute/path/to/mcpp` pins the
executable when temporary projects would select a different launcher.

The gate packs hash, math, log, trace, testing, geom, memory, unicode, fuzzy, session, pipeline and image, then builds six independent
release consumers: source or compiled archives, each with canonical, legacy
or mixed imports and dependency keys. The mixed form must resolve one copy
of each library. Runtime checks cover shared log and trace state, geometry, arena/slot ownership, Unicode and
fuzzy consumers as well as type/function identities. Recording files, pipeline resource
specialization/replacement and shared traces, PNG/WebP pixels and animated WebP
mux/demux are exercised through mixed APIs. No GUI or display server is required.

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
transitive dependencies (testing/hash, fuzzy/Unicode, pipeline/trace,
session/os+JSON and image/mwrap+codecs), so the archive fixture explicitly
supplies them. Source fixtures use the libraries' own external edges. The
actual index closure and its one OS backend must match the repository lock
(version, source and identity digest). Current mcpp hashes the namespace, lock
key spelling and version, not artifact contents; the fixture verifies short
and qualified lock spellings through the shared `mlibs.hash` implementation.
Third-party compilation is recorded separately
from the twelve foundation implementations; the latter must never appear in
a packed consumer's compile commands;
single-dependency publication remains unverified. Fresh project timings may
reuse global compiler caches. See the [migration design](../../../.agents/docs/2026-11-05-product-namespace-migration-design.md).
