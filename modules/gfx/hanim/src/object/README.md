# hanim.object

Renderer-independent HObject identities and owned contour captures in C++23 modules.
This is the first object foundation, not the complete hanim Engine/player or
animation catalog. It does not implement pixels, images, glyph assets, mesh,
materials, effects, live host callbacks or a UI adapter.

The package lives under `modules/gfx/hanim` so `modules/base/hanim/core` keeps
its strict standard-library plus `openmui.math` dependency boundary. Object
data itself does not depend on a renderer, UI, OpenLifa specification or site.
Its dependencies are `hanim.core`, independent `openmui.log`,
`mlibs.trace` and `mlibs.memory`. Object and part slots reuse memory's
single `SlotStorage` implementation; the registry owns names, graph validation,
owner identities and immutable captures, not a second free-list or generation
algorithm. The complete hanim umbrella migration is separate work.

```cpp
import std;
import hanim.object;

int main() {
    const std::array edges{hanim::ShapeSegment::line({-1, 1}),
                           hanim::ShapeSegment::line({1, 1})};
    auto contour = hanim::Contour::copy({{0, -1}, edges, true});
    auto objects = hanim::ObjectRegistry::make();
    if (!contour || !objects) return 1;
    hanim::ObjectDescription input{{{
        .id = "body", .contours = {*contour}, .fill = hanim::Rgba{1, 0, 0, 1}
    }}};
    auto object = objects->create(input);
    if (!object) return 1;
    auto captured = objects->capture(*object);
    if (!captured) return 1;
    auto body = captured->get(captured->root());
    if (!body) return 1;
    auto geometry = (*body)->contours.front().view();
    if (!geometry) return 1;
    // geometry borrows captured's immutable resource, not input or edges.
    // PreparedShapeMorph::prepare can consume this public ShapeContour.
    return geometry->segments.size() == 2 ? 0 : 1;
}
```

`ObjectRef` (`HObject`) includes a registry owner, slot and 64-bit generation.
`PartRef` also includes that complete object reference and its own part slot
and generation. Owners are unique within a process; refs are not portable
serialized scene IDs. Copying a ref does not keep an object alive. Copying a
`Snapshot` or `Contour` does keep its immutable payload alive.

Parts use explicit stable keys, unique across the object. Their input vector
order controls sibling order, not identity. `update` preserves part refs when
those keys survive reordering/reparenting; deleting and later reintroducing
the same key issues a new part generation. Generations never wrap: exhausted
slots are retired. Object content revisions increase independently. A revision
covers the complete capture; this version has no per-part change tracking.

`Contour::copy` deeply owns the input start, segment records and closed flag.
It accepts open paths and move-only contours. It checks finite coordinates
used by each verb, but does not certify simplicity, nonzero area or a particular
animation's topology. For example, the existing `PreparedShapeMorph` correctly
rejects an open captured contour rather than closing it silently.

`capture` pins the current immutable graph in O(1). Old captures retain names,
hierarchy, local transforms, contour leases and optional straight-alpha sRGB
solid fills after update, object deletion, registry move or registry destruction.
They can be queried concurrently. Payload views returned by `parts`, `get`
and `Contour::view` borrow their owning snapshot/resource. Keep an owner alive;
rvalue borrowing overloads are deleted. The registry never returns a payload
pointer into its mutable slots.

The registry has one owning thread, or requires external synchronization.
Registry mutation and capture are not a concurrent container API. Independent
registries can be used independently, and published immutable captures can be
read across threads. Move a registry only while it is quiescent.

| Limit | Scope and accounting |
|---|---|
| `max_objects` | Registry slot capacity, including any retired exhausted slots |
| `max_parts` | Per-object live parts and part-identity slots |
| `max_contours` | Per-object contour references; sharing a lease still counts each occurrence |
| `max_segments` | Per-object stored `ShapeSegment` records across references; implicit closing edges are not materialized here |
| `max_depth` | Per-object parent chain, with the root at depth 1 |
| Stable id / parent key | At most 128 bytes; id nonempty, parent empty only for the root; no ASCII controls; byte-exact keys, no URI/display-text normalization |

All declared input is validated before publication. Duplicate/missing keys,
multiple roots, cycles, depth/cardinality overflow, invalid resources or
nonfinite/out-of-range data return `std::expected` errors and preserve the
previous snapshot, revision and refs. Limits constrain representation
cardinality, not total RSS or externally retained old captures. Preparation
allocates; allocation exhaustion follows the repository's allocator policy,
not a separate recoverable OOM protocol.

Capabilities report only actual facets with contract version 1:
`hanim.parts`, `hanim.local-transform2d`, and, when present, `hanim.contours`
and `hanim.solid-fill`. Unknown `hanim.pixels`/mesh capabilities are unsupported.
Local transforms are captured exactly; world transform composition, allocation,
clip/mask and presentation geometry are not implemented by this checkpoint.

Mutations and failures use the existing `hanim.object` log category. Trace
spans cover object/contour operations; identity counters split 64-bit values
into exact 32-bit halves. Read-only queries never format logs. `capture` copies
ownership and records fixed-buffer trace events; tracing first warms its
existing per-thread buffer. A CTS measures 2,000 warm capture/name/ref/resource
query iterations with tracing enabled and observes zero new scalar or array
allocations. An independent host adapter captures a triangle and a circle,
then consumes them through the real core `PreparedShapeMorph`; sampling
continues deterministically after producer, registry and endpoint captures
are released. These tests do not imply rendering or an Engine implementation.
Log callbacks may capture committed state; reentrant mutation returns
`reentrant_mutation` without another log.

See [CONTRACT.md](CONTRACT.md) for the lifecycle and failure matrix.
Tests: `mcpp test object/ -p hanim --locked` after workspace registration.
The three test programs contain thirteen cases. Native lifecycle tests also run
four concurrent snapshot readers; the default Emscripten build skips only
that thread phase, retaining all owned-lifetime and synchronous queries.
Web command: `mcpp test object/ -p hanim --target wasm32-emscripten --profile wasm-test --locked`.

The bounded-churn cases also check failed fresh/recycled creation, replacement
of every part key at full capacity, description order independence, and the
next successful identity sequence after rejected updates. The official
`public-motion/object-query-500` release benchmark checks the zero-allocation
query contract in `tests/bench/budgets.bench.omui.json`; it reports CPU query
cost, not a complete animation frame or GPU performance.
