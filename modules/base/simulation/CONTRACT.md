# mlibs.simulation contract, v0.1

| Operation | Contract |
|---|---|
| `Clock(rate, catch_up)` | Finite rates below 1 become 1 Hz; non-finite rates become 1 Hz. Catch-up is at least 1. Signed finite time origins are supported. |
| `advance(now)` | Non-finite input is ignored. First finite call establishes the origin. Backwards elapsed time contributes zero. Non-finite/non-positive scale pauses while updating the origin. Excess stalls are capped before integer conversion and not owed later. |
| `reset(now)` | Finite input resets the fractional remainder and origin; non-finite input leaves them unchanged. |
| `Timeline(initial, keep, every)` | Cold allocation of owned snapshot/input banks. `every` is at least 1; `keep` is at least `every`. Caller must choose finite, feasible storage counts whose arithmetic fits `int64_t`/`size_t`; there is no untrusted-file parser, allocator budget or checked factory yet. |
| `step(input)` | Runs the model exactly once, records input, advances one frame and snapshots at the chosen interval. Stepping after seek discards that branch's future, while preserving the eviction floor; overwritten past frames never become reachable again. Model failure/rollback is not synthesized; choose an infallible prepared step or handle model status in owned State. |
| `seek(frame)` | Reconstructs a retained frame, including after reverse/forward queries; false outside `[earliest(), latest()]` without changing state. Domain step must not produce external side effects. |
| `input(frame)` | Borrowed input for a retained transition: caller precondition `earliest() <= frame < latest()`. Not an arbitrary-index query. |
| Lifetime/threading | Single-writer host, no implicit synchronization. State references are invalidated semantically by step/seek; readers need a stable snapshot/publication boundary. No guarantee of prebuilt ABI compatibility with the former game-owned implementation; rebuild module consumers. |

Inputs are recorded by value; plain-data pointers/handles do not acquire ownership. Any referenced resource must retain the same meaning/version throughout replay, or be represented by recorded owned data/IDs resolved by the model. Zero-allocation behavior requires fixed-capacity State copies and step implementations. Variable-topology models may allocate and must disclose that cost. No general persistence, sparse storage, task scheduler, GPU checkpoint or scientifically validated solver is implied by this package.
