# mlibs.cts

What a contract's conformance suite is written with, on [mtest](https://github.com/mcpp-community/mtest)'s conformance kind (`mcpplibs.mtest.cts`; convergence plan §5.5, the foundation plan's T0.9). A suite lives with the owner of the contract it checks, as a function that declares its cases against the subject it is given; each implementation runs it in its own tests. The runner, the selection of cases and the reports are mtest's — this package has no runner of its own.

```cpp
import mlibs.cts;
namespace cts = mlibs::cts;

// The owner's suite: a function that declares its cases against a subject.
void declare(Device& device, cts::Against who = {}) {
    if (who.subject.empty()) who.subject = device.caps().device;
    who.has = [&](std::string_view capability) { return has(device, capability); };
    cts::against(std::move(who), [&] {
        cts::declare({.id = "rhi.copy", .contract = "mlibs.rhi@1", .level = cts::Level::exact, .needs = {"copy"},
                      .body = [&](cts::Check& c) { c.that(cts::exact(read(device), want).same, "the bytes"); }});
    });
}

// An implementation's test: the cases are its own (lavapipe/rhi.copy, …).
UTest backend{"rhi.sdl", [] { declare(device, {.subject = "lavapipe", .required = {"compute"}}); }};
```

- **Cases** have a stable id, the contract and version they check (the case's clause), a level — S (semantics: structure, records), A (identical bits), B (within a tolerance the case declares), as the tag `level:S|A|B` — the capabilities they need, and a body that records failures and evidence (`Check`: a failure is mtest's, at the check's place; evidence is a line of the case's output).
- **Three results.** A case passes or fails by its checks. *Unsupported* comes from the subject's capabilities alone — a case whose needs the subject lacks does not run, and is reported skipped, `unsupported: <capability>` — and never counts as passed. A capability the run requires and the subject lacks fails once (`<subject>/requires <capability>`).
- **Comparisons** that say what differs where: `exact` and `within` (a tolerance and a budget of bytes beyond it), `image` (RGBA8 pictures: largest difference, how many pixels beyond, the first), `pinned` (an FNV-1a hash against its pin, recorded as evidence), `sampled` (a process sampled at given times — endpoints, intermediate states, interruptions — against pinned hashes).
- **The preparation contract** (`prepare::declare`): what every engine's `prepare` keeps — accepted the same way twice, refused by kind, a refusal leaving the engine as it was.
- **The world contract** (`world::declare`, against an adapter the world's owner gives; no coordinate in it): a world is a deterministic value — the same steps the same hash; an image restored runs on bit for bit; taken twice it is the same bytes (and, of a world that says `canonical-image`, the same state is the same bytes); an image cut short is refused with the world as it was; a branch runs alike and shares nothing, either way; reading changes nothing. `xengine.cellular`'s 2D world of matter and `xengine.dynamics`' rigid family are held to it; a world of three dimensions will be.
- **The presentation contract** (`world::declare_presentation`; D-178): what shows a world never writes it — its hashes are the same at every step of every ladder and while the ladders move; every ladder has a name; where each stands and why is said back.
- **Counted in a process that is not a test** (`tally`): a tool that answers which cases hold on this device (`xrender cts`) runs the same function and gets the cases by how they ended; a suite's own tests use it to hold that a subject at fault fails the case that names the fault. Nothing else of a runner is here.

For tests and the tools that run a suite: no application depends on it. Benchmarks are not cases (they have their own budgets, `tests/bench`). Its own package rather than a tool of `mlibs.utils` because it takes the test library.

```sh
mcpp test -p mlibs.cts
```
