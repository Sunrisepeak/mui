# Prepared object animation contract — morph and property recipes

`properties(snapshot, targets)` and `properties(frame, targets)` derive an owned
partial target from the source without registry writes or a synthetic ObjectRef.
They retain source identity/revision; `to()` is empty because no application
target snapshot exists. The root or explicit named part owns the changed local
lanes, all others are retained. Position and offset are exclusive; offset is
relative to the supplied snapshot/frame. `rotation_offset` adds radians to that
source; `scale_factor` multiplies its signed x/y scales. Rotation versus offset
and scale versus factor are mutually exclusive within a target and share the
same conflict keys across targets. Finite computed endpoints are required;
scale sampling remains the existing linear lane between source and product,
with no per-sample multiplication or accumulated history. Appended fields keep
existing aggregate field order; C++ module consumers must rebuild.
Targets validate before publication.
The same closed-vector contour domain and limits below apply. With no vertex
path effect, static cubics are prepared once and copied during sampling;
explicit vertex paths retain the shared geometry sampler. Rotation uses the
existing decomposed Transform2D lane and colors the existing OKLab machinery.
No alternate tween, matching implementation or frame callback is introduced.

A span of PropertyTargets is one atomic set across parts. It must be nonempty
and no larger than four times the part count: translation, rotation, scale and
fill are the currently supported channel groups. Root aliases resolve before
conflict detection. Repeated writes to any one resolved part/channel return
`property-conflict`, even if the values agree; independent fields combine.
Targets use the same source pose, so declaration order cannot make an offset
depend on another entry. Unselected local fields persist; parent inheritance
still applies. This contract does not claim world-space constraints or channel
blending between separate overlapping Program leaves.

## Prepared property timelines

`PreparedObjectAnimation` is the canonical alias of the original prepared type.
`property_timeline(snapshot_or_frame, clips, limits)` retains one unchanged
geometry representation and independent part/channel segments. Source limits
are the intersection of the clip limits; shape correspondence direction/phase
is irrelevant for unchanged geometry. Vertex-path effects are explicitly
unsupported in this timeline form; each translation channel keeps its own path.

Clips have independent start, duration and easing. Unspecified source fields
come from the actual preceding writer of that channel; an explicit library
`PropertyClip::source` supplies only selected local source fields, not its
geometry. This advanced local-source option is distinct from ProgramSource's
legacy whole-object snapshot reset. Repeated channel writes may be serial but
not overlap. Input order explicitly orders instantaneous ties in this flat
library form; Program additionally checks its author dependency graph.

Preparation validates finite representable intervals, source fields, duplicate
writes and channel count, then indexes each channel's ordered segments.
`channels()` exposes immutable timing/easing/path/source metadata; a clip index
refers to its input description (or Program contribution). `timeline_duration()`
is the maximum clip end. `sample(progress)` maps normalized progress to that
duration; `sample_source` bypasses every event/curve for exact restoration.
Sampling checks all active values before publishing any part or cubic. No
mutable history, callback or allocation is used to seek backwards.

The plan is immutable and O(1) copyable. `prepare` owns both endpoint snapshots
and a shared immutable payload of copied ids, endpoint states, contour plans
and ranges. Canonical order is source-root-first DFS in the source's existing
`children` order. Parts can be arbitrarily positioned in the input snapshot
array; parent links and ids are authoritative. Names, parent ids, ordered
children lists must match. Sibling reordering
returns `ordering_mismatch`; no z-order switch or fade is invented.

Contours pair by `MorphOptions::contours`. `align` (the default) measures each
contour exactly (`mlibs.geom::measure`: signed area and centroid), classes it as
an outline or a hole by its winding against the part's largest contour, and
pairs outlines with outlines and holes with holes, cheapest first (centroid
distance over the part's size plus half the log of the area ratio; ties by
ordinal). A contour left over grows from a point (`PreparedShapeMorph::grow`)
or closes into one (`shrink`): an outline at the nearest outline of the other
end, a hole at its own centre; a grown contour runs the way the part's paired
targets were turned. A contour that is a single point (a grown contour's first
frame, continued) is no contour. A part's frame contours are its pairs, then
its closing contours, then its growing ones. `MorphOptions::match` modifies the matching: the two cost weights (finite, ≥ 0)
and pins, a part's source and target contour ordinals paired before any is
weighed (both outlines or both holes, each contour once; otherwise
`invalid_options`). `strict` pairs by ordinal and
refuses differing counts with `contour_count_mismatch`. Both endpoints must
be closed and usable by the public `PreparedShapeMorph`; the original curves
are elevated/subdivided, not replaced with sampled polygons or two drawings.
A part with contours needs a solid fill or a stroke at both ends. Empty groups may omit fill at both ends. Changing
fill presence is unsupported. Per-contour target winding changes must be
uniform within a part, so a compound nonzero fill cannot silently change its
relative winding. Mixed winding reversals return `unsupported_topology`.
This does not certify simplicity, contour nesting, holes, or nonintersecting
intermediates. Core numeric/degeneracy limits and its stated caller simplicity
precondition remain applicable; no full topology analysis is claimed.

A part's deformers (`PartDescription::deformers`, made by `deform::displace`,
`scale`, `rotate`) come from the source end. Each weighs its points once
(`Selector`: everything, around a centre, inside a feathered box; the falloff
an `Ease`) on the geometry the plan starts from, and on every property frame
moves them by its value channel's amount (its rest without one), in
proportion to the weight; amounts are clamped to ±1e100. A spring writing the
channel overshoots the geometry with it. A deformer naming a built-in channel
is refused (`invalid_property`); a malformed selector is refused at object
creation (`invalid_part`). A deformed part's frame info carries its geometry
before the deformers (`MorphPartInfo::base`, borrowed from the plan), and an
animation continued from that frame starts from it, so nothing is deformed
twice. Morph plans carry no values, so their geometry is not deformed.

A property write's `blend` makes it the channel's base (`replace`: one writer
at a time, refused when two overlap) or a layer over it: `add` (weight × its
value), `multiply` (scale and value channels: × (1 + weight × (value − 1))) or
`mix` (towards its value by weight). An add or multiply layer runs from its
identity (zero, one), a mix layer from where the base is when it starts; each
layer holds its end after its clip and combines over the base in author
(clip) order. Colours only mix; positions and turns do not multiply; routes
do not layer; a nonfinite weight is refused (`invalid_property`). A layered
write always takes the timeline path, even alone.

The positive preparation budgets have hard maxima: 65,536 parts, 65,536
contours and 1,048,576 output cubics. Defaults are 4,096 / 4,096 / 65,536.
Counts apply per occurrence, including shared geometry. They bound the retained
plan and output counts, not allocator bytes or core preparation scratch. Core
per-contour limits remain `ShapeMorphOptions`' responsibility; the original
options are passed through unchanged, and failures preserve `ShapeMorphError`.
Unused shape options on a group-only plan perform no contour preparation.
Process-path controls are validated even on a group-only plan; invalid
`shape.point_path` or `translation_path` returns `invalid_options` before
capturing/compiling shapes. Other shape-only budgets keep their prior policy.
Failure returns no partial plan. A `MorphFailure` (an `mlibs::error::Error`) says
where in `where`: `part` uses source DFS index; `contour` is the ordinal within
it, with max-size sentinels when not applicable. A shape's own error is a cause.

`sample` is a pure function of finite progress and immutable endpoint data;
finite progress clamps uniformly to [0,1]. It performs no matching, ownership
allocation or mutable cache update. Different caller buffers may be sampled
concurrently. All buffer sizes, progress and current transforms are checked
before any output write. Overflowing transform interpolation returns
`nonfinite_transform` with buffers untouched. Private core plans then have
valid progress and exact prechecked spans: unexpected core failure is an
internal invariant violation, not a partial returned frame. Endpoints retain
their exact local transforms/colors and original subdivided contour geometry.
With a nonzero `shape.point_path`, each matched point follows the public
chord-relative cubic operator after correspondence. `translation_path` applies
to each part's local tx/ty in the bound morph payload. It does not replace
parent composition or become a world-space path. Both are pure process
effects, independent from ordered renderer effects; default zero controls
preserve the direct interpolation path. Point controls in ±1e6 and core
coordinate limits ensure representable double shape samples. Modified
translations participate in the same preflight/failure-atomic check above.
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


## Immutable translation routes

`TranslationRoute` freezes an owning MotionPath value without accepting a
caller-owned mutable shared pointer. PropertyTargets::route selects translation
exclusively against position/offset, while other channels/parts remain independent.
The same property timeline evaluator samples the route's distance table relative
to the actual source position, preserving exact selected-channel endpoints.
Single route bindings lower to that same timeline; a geometry PointPath modifier
is unsupported in both single and grouped route-property execution.

Preparation requires a nonempty finite continuous path and checks route segment
budgets before retaining it. A zero-length continuous path is allowed. Every
selected route counts its segments, including repeated logical uses of shared
immutable data; `route_segment_count()` exposes this retained count. No route
storage enters the caller's mutable geometry output. Warm sampling validates
results before publishing geometry as with other channels. PointPath translation
modifiers add their endpoint-chord displacement, while image effects remain a
separate stage. Existing fixed 32-sample arc-length approximation and double
precision limits apply; no tangent rotation or adaptive accuracy is implied.
