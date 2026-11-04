# Program contract v1: strict whole-object schedules

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
not claim per-channel blending, group transforms/foreach, repeat/reverse time
expressions or effect isolation scopes.

The compiler retains one immutable preparation per segment and no AST or producer
registry. Preparation limits bound expanded node/depth/track/segment counts and
retained part/cubic/effect-parameter counts across every segment, not allocator
bytes. Track maxima separately describe reusable output buffer capacities.
Geometry matching, deep boundary capture and allocation occur only at prepare.
Failure never returns a partially prepared value or mutates source objects.

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
