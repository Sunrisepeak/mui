# Prepared object morph contract — initial recipe

The plan is immutable and O(1) copyable. `prepare` owns both endpoint snapshots
and a shared immutable payload of copied ids, endpoint states, contour plans
and ranges. Canonical order is source-root-first DFS in the source's existing
`children` order. Parts can be arbitrarily positioned in the input snapshot
array; parent links and ids are authoritative. Names, parent ids, ordered
children lists and each part's contour count must match. Sibling reordering
returns `ordering_mismatch`; no z-order switch or fade is invented.

Each contour is paired by ordinal. Both endpoints must be closed and usable
by the public `PreparedShapeMorph`; the original curves are elevated/subdivided,
not replaced with sampled polygons or two drawings. A part with contours needs
solid fills at both ends. Empty groups may omit fill at both ends. Changing
fill presence is unsupported. Per-contour target winding changes must be
uniform within a part, so a compound nonzero fill cannot silently change its
relative winding. Mixed winding reversals return `unsupported_topology`.
This does not certify simplicity, contour nesting, holes, or nonintersecting
intermediates. Core numeric/degeneracy limits and its stated caller simplicity
precondition remain applicable; no full topology analysis is claimed.

The positive preparation budgets have hard maxima: 65,536 parts, 65,536
contours and 1,048,576 output cubics. Defaults are 4,096 / 4,096 / 65,536.
Counts apply per occurrence, including shared geometry. They bound the retained
plan and output counts, not allocator bytes or core preparation scratch. Core
per-contour limits remain `ShapeMorphOptions`' responsibility; the original
options are passed through unchanged, and failures preserve `ShapeMorphError`.
Unused shape options on a group-only plan perform no contour preparation.
Failure returns no partial plan. `MorphFailure.part` uses source DFS index;
`contour` is the ordinal within it, with max-size sentinels when not applicable.

`sample` is a pure function of finite progress and immutable endpoint data;
finite progress clamps uniformly to [0,1]. It performs no matching, ownership
allocation or mutable cache update. Different caller buffers may be sampled
concurrently. All buffer sizes, progress and current transforms are checked
before any output write. Overflowing transform interpolation returns
`nonfinite_transform` with buffers untouched. Private core plans then have
valid progress and exact prechecked spans: unexpected core failure is an
internal invariant violation, not a partial returned frame. Endpoints retain
their exact local transforms/colors and original subdivided contour geometry.
Trace observation uses the common trace facility; its first thread buffer is
prepared when tracing starts on that thread, and warm sampling adds no heap
allocation. No new throughput/latency guarantee is made.

FramePart/cubic payloads borrow caller storage. Contour metadata and id strings
borrow the plan's shared immutable payload, which a retained O(1) plan copy
keeps alive. A FrameView itself is a borrow, not a lease. Buffers must not alias
each other or any input/metadata; moving/destroying their last owner or changing
them while read is unsupported. Rvalue metadata borrowing is prohibited.
`identity()` is the immutable payload's process-local opaque witness, copied
to `FrameView.preparation`. It is neither a serialized object id nor authority.
A renderer binding retains a plan copy to keep the witness alive and prevent
address reuse while checking a frame against that binding.

`retarget(current, target)` requires a valid, unchanged borrowed FrameView for
the duration of the call. It validates its flat ranges, ids, refs, hierarchy,
fill/state and exact closed cubic connectivity, then deep-copies current ids,
geometry and state and uses the same normalization/core preparation path.
It holds no old plan/frame/snapshot chain. Its `from()` snapshot is empty;
`source_object()` / `source_revision()` and `info.from_ref` preserve the logical
source stamp, while `to_ref` identifies the new target. It mutates no registry
business state. New progress zero is the current pose (allowing subdivision
roundoff when correspondence introduces new cuts); C0 does not imply inherited
velocity. Errors leave the caller's previous plan and frame unchanged.

Structured failure includes unprepared/default plans, invalid snapshots or
frames/options, part/contour/cubic budgets, part-set/hierarchy/ordering/count
mismatches, missing fills, open/unsupported contours, core preparation failures,
nonfinite progress, insufficient output and overflowing current transforms.
