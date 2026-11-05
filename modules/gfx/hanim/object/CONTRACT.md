# Object contract v1

This is an implementation-level independent library contract. It does not add
a UI component contract, C ABI binding, OpenLifa schema or future Engine API.

## State and identity

An object reference is `(owner, slot, generation)`. A part reference is
`(object-reference, part-slot, part-generation)`. Owner 0, the invalid slot
sentinel and generation 0 do not refer to objects. An owner's numeric value
is local to its process and never used as an animation random seed.

Creation publishes revision 1. Successful `update` publishes a new immutable
state with revision +1. Revision overflow is refused, not wrapped. Surviving
explicit part keys retain identity; removing a key invalidates its live part
ref. A later same-key insertion cannot resurrect it. Destroyed object slots
increase object generation before reuse, independently of revision/part ids.

All failures returned by a mutation preserve the old state. A `Snapshot` has
its own frozen identity space: it can still resolve its captured refs after
they are stale in a live registry. New-object or new-part generations do not
resolve inside an older capture even if slot and key happen to match.

Object and part allocation use the shared `mlibs.memory` slot storage,
with 64-bit generations initially 1 and retirement at exhaustion. Creation's
candidate reference is prepared before a slot is consumed. Part changes use
a private copy of the identity table: removed keys are released before new
ones are inserted, so full-capacity replacement is independent of input
order. Only successful preparation publishes the table and capture together
on the owning thread; this is failure atomicity, not concurrent mutation.

| Operation | Relevant failures | Mutation on failure |
|---|---|---|
| Registry make | Invalid positive/hard limits, owner identity exhausted | No registry created |
| Contour copy | Invalid limit, segment budget, nonfinite used coordinates, unknown verb | No resource published |
| Create | Registry uninitialized, capacity, invalid graph/data | No slot/identity/revision committed |
| Update | Foreign owner, stale object, invalid graph/data, capacity, revision exhausted | Old snapshot/part refs unchanged |
| Destroy | Foreign owner, stale/invalid object | No other object affected |
| Snapshot get | Foreign owner, different object slot, old object generation, invalid/stale part generation | Read only |
| Require capability | Uninitialized, invalid required version, absent/older capability | Read only |
| Reentrant mutation | Existing write/log callback is active | Refused without recursively logging |

## Owned capture

`Contour::copy` allocates owned immutable storage, not a retained input span.
`ObjectDescription` is preparation input. Published snapshots copy graph keys,
transforms and colors, compile parent/child refs and indexes, and retain strong
immutable contour leases. No snapshot depends on an adapter, registry address,
producer vector, callback, renderer arena or temporary source shape.

A capture retains the already-published graph rather than cloning it every
frame. Multiple captures can share geometry intentionally because its public
API provides no mutable alias. Updating an object never edits an old capture.
Destroying the last owning registry/capture/resource releases its storage.
Borrowed query views are valid only while one of their corresponding owners
is kept alive. Queries return const payloads; rvalue borrowing is prohibited.

Snapshot queries are immutable, allocation-free and can run concurrently.
Capture is O(1); named lookup uses a prepared sorted index, ref lookup a
prepared slot-to-dense index. Mutable registries require owner-thread access
or external synchronization; no hidden process-wide object store or clock
exists. Trace/log infrastructure is shared, but object data and lifecycles are
per registry. Only process-local owner issuance uses an atomic counter.

## Represented domains

This checkpoint stores actual open/closed line, quadratic and cubic contours,
hierarchy, local decomposed 2D transforms and optional solid sRGB fills. A
move-only/degenerate contour can be represented without claiming it is a valid
closed fill or morph endpoint. `PreparedShapeMorph` performs its own stricter
checks through its existing public API.

Capability versions describe available representations, not universal plan
compatibility. There are no image/texture/font/mesh/pixel placeholders or
callbacks that pretend to implement those domains. There is no live host
Binding, playback Handle, rendering, world matrix calculation or automatic
reprepare loop here. An external consumer can explicitly check a registry's
content revision, capture again and prepare a new plan.

Future Engine/adapters must use these public owned identities and captures;
they may not bypass ownership by borrowing a UI DisplayList or private host
buffer. That integration and full A1/Manim coverage remain unimplemented.
