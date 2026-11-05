# Program contract v4: object schedules, selected property sets and image scopes

Program is immutable shared input data. Builders bind owned snapshots and return
new values without changing a registry, starting time, rendering or invoking
application callbacks. Reused values do not share playback cursors. Empty values
and combinations, invalid times/eases/source modes and exceeded preparation
limits are rejected by preparation.

The compiler expands nested sequence, parallel and fixed-step stagger into
bounded object tracks and ordered segments. Each track is identified by the full
logical source ObjectRef, including owner and generation. Target identity does
not rename the track. `sequence(bind(A,B),bind(A,C))` means A follows A→B→C.
`sequence(bind(A,B),bind(B,C))` has separate A and B tracks. Ordinary default
bindings after the first use the prior segment's actual terminal geometry,
transform and fill, deeply captured via the library's existing retarget. A
binding explicitly marked snapshot uses its authored frozen source and can jump.
Default previous bindings must agree with the track's initial source revision;
explicit snapshot mode can deliberately introduce a different frozen revision.

All non-overlapping segments of one object write the strict whole-object domain.
Overlapping writes fail with node/track/segment indices. Shared positive-duration
boundaries are permitted. Multiple instantaneous writes at the same start require
an explicit sequence dependency; parallel instantaneous writers conflict. Child
order is retained for a sequence of instantaneous changes. This first stage does
not claim per-channel blending, group transforms/foreach or repeat/reverse time
expressions. Explicit image scopes are defined below.

Unary leaves may carry `PropertyTargets`: absolute position, relative offset,
rotation, scale and fill, applied to the unique root or one named part. Position
and offset are mutually exclusive; an empty target, unknown part or nonfinite /
invalid color value fails preparation. The source is captured only once; there
is no second target registry object. The target is derived from the actual
segment source after previous-endpoint chaining, preserving all unspecified
fields and descendant local transforms. Explicit snapshot mode still resets
that source. Rotation is a decomposed radian lane, including requested turns.
The same strict whole-object write domain remains: two parallel property
bindings to one object conflict; a single `anim::to` combines multiple fields.

One property leaf can carry a `PropertySet` across multiple named parts.
Its primary target plus additional targets share one time/ease/effect scope.
Each target resolves to the actual part before channel conflicts are checked;
empty root selection and that root's explicit id cannot evade the check.
Different fields on the same part combine; repeated writes to a channel fail,
without implicit order dependence. This is not independent per-channel tracks
or blend support. Local fields omitted from the set preserve the source, while
hierarchical transforms still apply. The old single-target authoring remains
the one-element case of the same compiler and sampler.

`anim::BoundAnimation` is the shared value builder for morph and properties;
`anim::Morph` remains a source-compatible alias. Property and morph leaves can
be mixed in any schedule, including before/after a topology-preserving morph.
Existing Process effects and explicit composite image scopes use the same
preparation and player. Property-only geometry is retained and copied at sample
time when no vertex path is requested; cold preparation still uses the existing
strict contour validation/compiler. This is not the general typed-channel IR.

The compiler retains one immutable preparation per segment and no AST or producer
registry. Preparation limits bound expanded node/depth/track/segment counts and
retained part/cubic/effect-parameter counts across every segment, not allocator
bytes. Track maxima separately describe reusable output buffer capacities.
Geometry matching, deep boundary capture and allocation occur only at prepare.
Failure never returns a partially prepared value or mutates source objects.

Animation-first leaves use typed `ProgramInputs`: each endpoint is either an
ObjectRef resolved by the supplied registry or an owned Snapshot. All endpoints
are resolved during the bounded collection pass before matching starts. Without
a registry, a live reference reports `unresolved_object`; a failed capture
reports `object` with the node index and nested ObjectError. Explicit snapshots
may come from another registry. Old snapshot bindings and new leaves can share
one tree and preserve the same source chaining rules. Registry mutation must
remain externally synchronized during standalone preparation; the Engine entry
also rejects reentrant writes. Preparation never changes the original Program.

Sampling is a pure function of a frozen plan, track and finite local time. Core
time primitives handle segment local time and fills; core easing and existing library
samplers handle values. Before the first start, the exact source is held. Gaps
hold the preceding segment's final values; at a shared start, the later segment
wins. Chained noninstant starts are anchored to their boundary source, including
arbitrary seek back to that time. Authored effect endpoints remain local to each
segment; effects are not silently carried or matched across different chains.
Zero-duration stages immediately take their target. exact_source always samples
the first segment at raw zero, bypassing easing, for cancel/restore.

sample writes only actual segment ranges even when supplied track-max buffers.
Returned FrameView spans borrow the caller buffers and immutable plan; keep both
alive. ProgramSample identifies the active segment so renderers can bind the
correct geometry/material/effect contract. Concurrent sampling uses independent
caller buffers. All reported failures occur before either output is changed.
No script, wall clock, playback state, registry writes or matching is used in the
sampling path. The engine is the single owner of player lifecycle and atomic
multi-track frame publication; Program does not introduce a second player.

Trace/log category: hanim.program. prepare records bounded graph cardinality,
refusals identify indices, and sample records a span without formatting logs.
Process-local ObjectRefs and preparation witnesses are not persistent Agent IDs.

## Composite scope extension

`composite(animation).with_effect(image_effect)` wraps one animation subtree.
It processes the composed output once; a leaf's effects still process that
leaf's output before its containing scopes. Process effects such as vertex
paths remain typed leaf operations. Sequence, parallel and stagger themselves
do not allocate image layers or broadcast effects.

The prepared plan retains author nodes in preorder, with parent, exclusive
subtree end, absolute start/duration and nearest image scope. Segments retain
their author node and scope. Scope records retain their parent, node, timing,
effect easing, ordered effects and parameter range. This is a hierarchy/time
view of the same compiled Program, not a second AST, player or renderer. The
strict whole-object write domain is unchanged; general typed channels and
resource/device scheduling remain separate work.

Scope duration is derived from its subtree. Its easing controls effect
parameters only; it does not warp child clocks. Before its start, hold raw
source parameters; afterwards hold the eased endpoint. Zero duration takes the
target at its start. Cancel/restore selects raw source values. A track's selected
segment determines its scope membership, including held source/endpoints and
gaps. Membership changes at the same exact boundaries as geometry; scopes do
not create extra copies of a track. Changing scopes can change compositing order
and is explicit authored behavior, without an implicit visual crossfade.

The optional Program renderer orders active bindings by author-node preorder,
making each active scope contiguous. Track indices remain stable identity order;
legacy per-track callers retain control of their own drawing order. Empty image
chains are identity scopes and can omit layers; ordinary one-segment tracks use
the same direct order without sorting. All scope parameters count towards
preparation/Engine budgets. Sampling is deterministic, bounded and allocation
free after preparation; errors preserve published frame data.
