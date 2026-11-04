# hanim.engine

Owned object playback and public native controls, independent of UI, renderer,
window, networking or scripts. The first supported animation is the library's
strict named-part contour/transform/fill morph; ordered effects share its
bounded eased progress. This is not the complete animation catalog, pixel or
rig domains, general channel/effect composition or full Engine acceptance.

`Engine::play(object, anim::morph_to(target), effects, options)` captures owned
endpoints from its registry. External hosts can instead publish two public
Snapshots to the same player. Sampling never calls a business setter. Every
source object has one binding: repeated play reports `binding_conflict`; use
retarget for a continuous replacement, release for an explicit new binding.
The convenience call normalizes to `PreparedProgram::single`; the engine also
accepts `play(PreparedProgram)` from nested sequence/parallel/stagger expressions.
Both use the same clock, buffers, controls, slots and publication path.

`advance(elapsed)` is a deterministic native batch boundary. Two caller-hidden
buffers per object track allow all active poses/effects to prepare before any
visible frame/time is committed. Preparation allocates; advance and queries
reuse that storage. Slots and generations use the shared memory implementation.
Finished and cancelled handles retain their poses/resources and budgets until
explicit release. Engine totals bound the sum of each track's maximum mutable
part/cubic/effect-parameter capacities. ProgramLimits separately bound retained
preparations across all segments. Neither is a byte/RSS or external-lease limit.
Prepared programs also obey the engine's per-segment effect limits.

Pause/resume preserve time; seek clamps finite local time/progress without
changing pause intent; reverse changes the sign of the rate (and can restart a
finished forward segment). Retarget deep-captures the current pose through the
library for a single track (including a sequence), keeps the active segment's
current effect values and targets, and starts a new forward
segment with the prior rate's magnitude and pause intent. Failed preparation
does not replace the old plan. `cancel(hold)` ends playback at its current pose;
`cancel(restore)` returns every track to its first segment's raw source.
Neither undoes model state. Multi-track-to-single-target retarget reports
`unsupported_operation`; selective track/channel replacement remains future work.
Cancelled bindings retain source occupancy until release. Restore bypasses
easing to sample the exact source. Retargeted noninstant segments anchor time
zero to the captured pose consistently for initial display and arbitrary seek;
authored steps still jump at subsequent positive times. Ordinary initial play
keeps the core curve's jump-start semantics.

The contour sampler/effect plans use bounded progress: eased overshoot is
explicitly clamped for this domain. This is not spatial spring/elastic motion.
Zero duration immediately samples the target. A finite nonzero playback rate
and finite nonnegative elapsed time are required; huge values saturate at the
direction's endpoint without overflowing their multiplication.

Frame spans and plan pointers borrow the engine until its next mutation.
Immutable plan copies retain their preparation, not mutable frame buffers.
Single owning thread/external synchronization; move only while quiescent.
`inspect` reports track/segment counts and whole-program normalized time. A
single track also has its active segment's optional sampled progress and target;
multi-track programs have no invented common eased progress or object/target.
`frame(ref)` reports `ambiguous_frame` for multiple tracks; use `frame(ref,index)`.
Frame geometry progress and segment index identify the actual current segment.
Prebind each segment's optional renderer before playback; use the returned
segment index when recording, and rebind the plan after explicit retarget. The
renderer owns material mixing; automatic material-history retargeting and UI
presentation/hit mapping are subsequent integration work.

Public inspect/list/pause/seek/reverse/rate/retarget/cancel operations are
native control primitives for GUI/CLI/Agent adapters. The all-engine openas
provider/transport is not implemented by this package. Trace category and log
category are `hanim.engine`; queries never format logs, advance records a batch
span. Log callbacks may query committed state, but reentrant mutation is refused.

See the [play API composition design](../../../../.agents/docs/2026-11-04-hanim-play-api-design.md)
for the target Object/Animation/Effect/Config scopes. The current compiler supports
nested sequence/parallel/stagger and prior-boundary sources on strict whole-object
tracks. Same-object overlap is rejected. General channel blending, effect/group
scopes, complete domains and UI migration remain open.

The [external CLI consumer](../../../../tools/ci/hanim-play-consumer/README.md)
uses these public operations and the optional renderer without UI/window
dependencies, outputs actual PNG regions and JSON provenance, and validates
crop pixels and geometric retarget continuity. The package's visual CTS also
exports three real frames and a region through the same native renderer.
