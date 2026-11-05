# Foundation namespace migration consumer

Run `python3 tools/ci/foundation-migration/run.py` from a checkout with the
project's mcpp toolchain installed. `--mcpp /absolute/path/to/mcpp` pins the
executable when temporary projects would select a different launcher.

The gate packs twenty mlibs libraries: hash, math, log, trace, testing, geom,
memory, unicode, fuzzy, session, pipeline, image, rhi, raster, shader, text,
layout, markdown, i18n and i18n.cldr, plus hanim.core and the three xengine.render,
xengine.render.vector and xengine.render.effect packages (24 archives). It then builds six independent
release consumers: source or compiled archives, each with canonical, legacy
or mixed imports and dependency keys. The mixed form must resolve one copy
of each library. Runtime checks cover shared log and trace state, geometry, arena/slot ownership, Unicode and
fuzzy consumers as well as type/function identities. Recording files, pipeline resource
specialization/replacement and shared traces, PNG/WebP pixels and animated WebP
mux/demux are exercised through mixed APIs. RHI commands and wire words,
raster clips and a PNG round trip, and real Slang compilation to SPIR-V,
WGSL and ESSL are exercised. Viewport reflow, Markdown stream/HTML, shared
locale/CLDR formatting and real Latin/Chinese/Arabic text PNGs complete the
first twenty runtime cases. Three renderer cases verify shared registry identity,
mixed display-list/CPU-renderer intermediate effect pixels, and a real Slang
effect asset compiled and registered through the new/old loader APIs (23 total).
Every import style and repeated run must have identical
text pixels. Fonts are explicit fixture assets, not implicitly bundled by the
text library archive. No GUI or display server is required.

Three further external projects consume the shader build rule with canonical,
legacy or mixed imports. Each runs a real build.mcpp action, compiles its
generated partition and reads the two entry points and three formats.
These are build-time plugin checks, not additional workspace packages.

Exit zero and the final JSON `ok: true` mean all checks passed. The default
report is `target/foundation-migration/report.omui.json`; its adjacent
`artifacts/` directory contains command logs, dependency graphs and packed
manifests and generated shader partitions. Use `--output` to select another report and `--keep-project` to
retain generated projects for diagnosis. A failed invocation returns nonzero
and records the failing command; correct its cause and rerun. Temporary
projects own their resolution; the repository lock must stay unchanged.

This is a source compatibility gate after rebuilding, not an ABI or registry
alias test. The fixture deliberately depends on the testing library; separate
production consumers audit its absence. Current mcpp pack omits local path
dependency edges, so the archive fixture explicitly supplies the dependencies
and a GNU ld rescan group of the installed archives. CLDR -> i18n exposes why
listing direct dependencies alone cannot recover archive order. Installed
manifests and interfaces are not edited; source implementations are never
substituted for archives. This does not certify automatic top-level-only package
installation. Source fixtures use the libraries' own external edges. The
actual index closure and its one OS backend must match the repository lock
(version, source and identity digest). Current mcpp hashes the namespace, lock
key spelling and version, not artifact contents; the fixture verifies short
and qualified lock spellings through the shared `mlibs.hash` implementation.
Third-party compilation is recorded separately from the twenty-four local libraries;
their implementation sources must never appear in a packed consumer's compile
commands. The shader package also has a CLI target: pack explicitly selects
`mlibs-shader`; its compiled CLI entry must be absent from the consumer link.
Outside declared source roots, only the exact shader provisioning, vector
drawing/effect shader and four locale partitions are accepted, with expected
exported module names and hashes. Embed/plan/rules/gen may appear only as
host build-program instances; their sources must not enter target compilation.

Single-dependency publication and relocation of the provisioned Slang tool
path to another machine remain unverified. The current locked host-tool scratch
limitation can be handled with mcpp's `MCPP_TOOL_SHADER_OMUI_SHADER` pointing to
an actual freshly built tool; it must generate the canonical RHI API.
Fresh project timings may
reuse global compiler caches. See the [migration design](../../../.agents/docs/2026-11-05-product-namespace-migration-design.md).
