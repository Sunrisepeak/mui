# mlibs.cts

The conformance framework every contract's suite runs on (convergence plan §5.5). One framework, many suites: a suite lives with the owner of the contract it checks (`src/cts/` under the feature `cts`), and each implementation runs it in its own tests.

```cpp
import mlibs.cts;
namespace cts = mlibs::cts;

cts::Suite<Device> suite{"mlibs.rhi", "1"};
suite.add({.id = "rhi.copy", .contract = "mlibs.rhi@1", .level = cts::Level::exact, .needs = {"copy"},
           .body = [](Device& d, cts::Check& c) { c.that(cts::exact(read(d), want).same, "the bytes"); }});
auto report = suite.run(device, [&](std::string_view cap) { return has(device, cap); },
                        {.required = {"compute"}}, {.subject = "lavapipe", .platform = "linux"});
check(report.ok(), report.text());
```

- **Cases** have a stable id, the contract and version they check, a level — S (semantics: structure, records), A (identical bits), B (within a tolerance the case declares) — the capabilities they need, and a body that records failures and evidence (`Check`).
- **Three results.** A case passes or fails by its checks. *Unsupported* is the suite's verdict from the subject's capabilities alone — a case whose needs the subject lacks does not run — and never counts as passed. A capability the runner's policy requires fails the case when it is missing.
- **Comparisons** that say what differs where: `exact` and `within` (a tolerance and a budget of bytes beyond it), `image` (RGBA8 pictures: largest difference, how many pixels beyond, the first), `pinned` (an FNV-1a hash against its pin, recorded as evidence), `sampled` (a process sampled at given times — endpoints, intermediate states, interruptions — against pinned hashes).
- **Reports**: `text()` for a person, `json()` (format `mlibs.cts@0.1`, files `*.cts.omui.json`) for agents and tools: the suite and version, the subject, platform and toolchain, every case's result, failures, missing capabilities and evidence.

For tests only: no artifact depends on it. Benchmarks are not cases (they have their own budgets, `tests/bench`). Its own package rather than a tool of `mlibs.utils` because the report needs a JSON library.
