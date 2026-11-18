# mlibs.compose

What a program is made of, chosen at its root: slots, providers and compositions — one mechanism for composing the engines, extending them from outside, and leaving out what a program does not need (D-238, [design](../../../.agents/docs/2026-11-18-compose-slots-worlds-design.md)).

**Preview.** The run time is here; the declared data (`compose` kind), the build's evaluation of worlds and the lints are tasks K2–K5 of the design.

| Concept | What it is |
|---|---|
| slot | a typed, versioned contract others may provide at; one owner declares it as a constant (`Slot<Service>`) |
| provider | an implementation of a slot — a table of functions, a stage, a pack of data — in any package |
| composition | the value saying which providers a program installed, and for an empty slot why and the way out |

```cpp
import mlibs.compose;

// the owner
struct Service { std::string_view name; Attach* attach; };
inline constexpr mlibs::compose::Slot<Service> service_slot{{.id = "acme.service", .summary = "…", .absent = "depend on acme.bridge and call acme::bridge::install()"}};

// a provider, called by the program's root
void install() {
    static const Service service{"native", &attach};   // made when installed
    (void)mlibs::compose::process().install(service_slot, service, "native");
}

// the owner, where it prepares
const Service* service = mlibs::compose::process().get(service_slot);
if (!service) return std::unexpected(mlibs::compose::process().missing(service_slot.info));
```

Rules a provider and an owner keep:

- **Only a program's root installs.** A library depends on a slot's owner, never on a provider; nothing installs itself from an initialiser.
- **A provider's table is made when it is installed**, not initialised at program start, and holds only what the owner calls. An initialiser run at start is a root to the linker: the whole provider would stay in a program that never installs it (measured: example 01, 18,049 KiB against 16,461 KiB).
- **Absent is unsupported.** A provider left out of the build, one never installed and a platform that has none are one state on one path; the failure (`compose.absent`, refused) names the slot and the way out.
- **Slots sit at the grain of a service or a batch**, never a node, a pixel or a sample. An owner reads its slot where it prepares and keeps the pointer; a frozen composition is read without a lock.
- **A composition is a value.** `process()` is the program's; a test, a second world or a tool makes its own.

Not here: a container that builds objects, services found by a string, providers discovered at start-up, a shape for services — a service's type is its owner's.

Tested: `tests/compose_test.cpp` — a slot of one and of many, the three absences, freezing, `describe`, two compositions apart.
