# mlibs.compose

What a program is made of, chosen at its root: one mechanism for composing the engines, extending them from outside, and leaving out what a program does not need (D-238; [design](../../../.agents/docs/2026-11-18-compose-slots-worlds-design.md)).

**Preview.** Here today: the run time, what manifests declare (lint `compose`), the unit a root's build writes ([`mlibs.build.compose`](../build)) and worlds as features. Owed (design, K4–K5): the composition's snapshot and diagnostics from the resolved graph, worlds derived from an application's declaration, the CI matrix.

## The idea

Leaving a capability out of a program is **not installing it**. The core stays small; everything else is a provider a program's root installs by name. A provider that is not named is not referenced, so the linker leaves it out — and a program without it behaves as a platform that never had it: one state, one path, one refusal that says the way out.

| Concept | What it is | Whose |
|---|---|---|
| **slot** | a typed, versioned contract others may provide at (`Slot<Service>`) | one owner declares it, as a constant |
| **provider** | an implementation of a slot — a table of functions, a stage, a pack of data | any package, inside the repository or out |
| **world** | a named choice of what a program carries (`lean`, `app`, `full`) | a feature of a composition root; an application picks one |
| **composition** | the value saying which providers a program installed, and for an empty slot why and the way out | the program's (`process()`), or a test's or a second world's own |

Three questions are kept apart, each answered in one place by one mechanism of mcpp:

| Question | Answered by | Where |
|---|---|---|
| Does the program carry it? | a feature of the composition root (additive; a world is a feature that `implies` others) | the root's `[features]`, an application's one dependency line |
| Which implementation does this platform have? | target blocks | inside the provider's package |
| How is it obtained — static, shared, the system's? | the build's profile | the workspace |

## Using it

**Own a slot** — the package whose contract it is declares it, reads it where it prepares, and says so in its manifest.

```cpp
import mlibs.compose;

struct Service { std::string_view name; Attach* attach; };          // only what the owner calls
inline constexpr mlibs::compose::Slot<Service> service_slot{{
    .id = "acme.accessibility.service", .summary = "the platform's assistive-technology service",
    .absent = "build with acme.sdk's feature `accessibility-bridge`, or call acme::accessibility::bridge::install()"}};

const Service* service = mlibs::compose::process().get(service_slot);
if (!service) return std::unexpected(mlibs::compose::process().missing(service_slot.info));   // compose.absent: the slot, why, the way out
```

```toml
[package.metadata.openmui.compose]
slots = ["acme.accessibility.service"]
```

**Provide at it** — a package of its own, which holds the platform's choice; it exports one function a root calls.

```cpp
void install() {
    static const Service service{native::name(), &attach};           // made when installed, never at program start
    (void)mlibs::compose::process().install(service_slot, service, service.name);
}
```

```toml
[package.metadata.openmui.compose]
provides = ["acme.accessibility.service"]
```

**Compose a root** — an SDK, or a program that composes itself: its features are the capabilities and the worlds, and its build program writes the unit that installs what this build chose ([`mlibs.build.compose`](../build)).

```toml
[package.metadata.openmui.compose]
root = true

[features]
default = ["full"]
lean = {}
app  = { implies = ["lean", "accessibility-bridge"] }
full = { implies = ["app"] }
accessibility-bridge = {}
```

```cpp
// build.mcpp
mlibs::build::compose::unit({.module = "acme", .imports = {":shown"}, .function = "acme::hosting::install_services",
    .providers = {{.feature = "accessibility-bridge", .package = "acme.accessibility.bridge",
                   .call = "acme::accessibility::bridge::install()", .targets = mlibs::build::compose::Targets::desktop}}});
```

**Choose a world** — an application writes nothing and has the default, or one line; building with mcpp directly and through a front end are the same build.

```toml
acme.sdk = { version = "…", default-features = false, features = ["app"] }                      # a world
acme.sdk = { version = "…", default-features = false, features = ["lean", "accessibility-bridge"] }   # or its own choice
```

Development is always the full world: a development command adds it on the command line (`--features acme.sdk/full`; the command line adds features and takes none away), so the line above constrains only what ships.

## Rules

- **Only a program's root installs.** A library depends on a slot's owner, never on a provider's package, and never on a composition root (features merge over the graph: it would bring the default world back). Nothing installs itself from an initialiser.
- **A provider's table is made when it is installed**, not initialised at program start, and holds only what the owner calls. An initialiser run at start is a root to the linker: the whole provider stays in a program that never installs it (measured on omui's example 01: 18,049 KiB against 16,461 KiB).
- **Absent is unsupported.** A provider left out of the build, one never installed and a platform that has none are one state on one path; the refusal (`compose.absent`) names the slot and the way out.
- **Features only add.** No exclusive pairs, no `no-*` (but for a third-party library's inner switch, in its `compat.*` wrap). A default is taken away in one place: `default-features = false` on the application's line.
- **The three questions stay apart.** A manifest does not choose one set of sources by a feature and by a target's exclusion at once.
- **Slots sit at the grain of a service or a batch**, never a node, a pixel or a sample. An owner reads its slot where it prepares and keeps the pointer; a frozen composition is read without a lock.
- **A slot or a provider earns its place**: it saves 100 KiB or more that the linker cannot collect on its own, or keeps a system dependency, a licence or a platform apart. What nobody calls the linker already leaves out, with no switch (image decoders: no slot).

Which way a provider goes, by what it is:

| Provider | How |
|---|---|
| code, light to build | the root depends on its package always; the unit names it by feature; unnamed, it is collected |
| heavy to build, or with a system dependency or a licence | `[feature-deps.<feature>]`: in the graph only when chosen |
| data (fonts, themes, packs) | a data package by `[feature-deps]`, or `deploy` from the root's build program by feature |
| a third-party library's subsystem | its `compat.*` feature, forwarded to the root |
| layers inside one package | that package's own features (`sources`, `implies`); not a provider |

Lint `compose` holds what can be read: a slot has one owner, is named after it and is in its sources; a provider names a declared slot; only a root depends on a provider; no library depends on a root; a root's unit names declared providers it depends on; no feature sources beside a target's exclusion. The slot contract's cases (`mlibs.cts:slots`) run on each slot in its owner's tests.

## The run time

`Slot<Service>` (id `owner.name`, version, one or many, summary, the way out) · `Composition`: `install`, `get`, `all`, `provider`, `leave_out` (why a slot is empty: left out, or the platform's), `missing` (the refusal), `freeze`, `describe` (what is installed, what is absent and why) · `process()`. A composition is a value: a test, a second world or a tool makes its own. It costs a program 13 KiB once, whatever the number of slots.

Not here: a container that builds objects, services found by a string, providers discovered at start-up, a shape for services — a service's type is its owner's.

## Users

| Slot | Owner | Provider | Root |
|---|---|---|---|
| `omui.accessibility.service` | `omui.accessibility` | `omui.accessibility.bridge` (AccessKit, by platform) | `omui.sdk` (feature `accessibility-bridge`) |

Tested: `tests/compose_test.cpp` — a slot of one and of many, the three absences, freezing, `describe`, two compositions apart; the slot contract in `mlibs.cts`'s tests.
