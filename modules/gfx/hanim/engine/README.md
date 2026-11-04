# hanim.engine

Owned object playback and public native controls, independent of UI, renderer,
window, networking or scripts. The first supported animation is the library's
strict named-part contour/transform/fill morph; ordered effects share its
bounded eased progress. This is not the complete animation catalog, pixel or
rig domains, clip composition graph or full Engine product acceptance.

`Engine::play(object, anim::morph_to(target), effects, options)` captures owned
endpoints from its registry. External hosts can instead publish two public
Snapshots to the same player. Sampling never calls a business setter. Every
source object has one binding: repeated play reports `binding_conflict`; use
retarget for a continuous replacement, release for an explicit new binding.

`advance(elapsed)` is a deterministic native batch boundary. Two caller-hidden
buffers per playback allow all active poses/effects to prepare before any
visible frame/time is committed. Preparation allocates; advance and queries
reuse that storage. Slots and generations use the shared memory implementation.
Finished and cancelled handles retain their poses/resources and budgets until
explicit release. Totals bound retained part/cubic/effect-parameter cardinality,
not bytes, old external snapshots or preparation's temporary memory.

Pause/resume preserve time; seek clamps finite local time/progress without
changing pause intent; reverse changes the sign of the rate (and can restart a
finished forward segment). Retarget deep-captures the current pose through the
library, keeps the current effect values and targets, starts a new forward
segment with the prior rate's magnitude and pause intent. Failed preparation
does not replace the old plan. `cancel(hold)` ends playback at its current pose;
`cancel(restore)` returns to this segment's source. Neither undoes model state.
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
Use frame's plan revision to rebind an optional renderer after retarget. The
renderer owns material mixing; automatic material-history retargeting and UI
presentation/hit mapping are subsequent integration work.

Public inspect/list/pause/seek/reverse/rate/retarget/cancel operations are
native control primitives for GUI/CLI/Agent adapters. The all-engine openas
provider/transport is not implemented by this package. Trace category and log
category are `hanim.engine`; queries never format logs, advance records a batch
span. Log callbacks may query committed state, but reentrant mutation is refused.

See the [play API composition design](../../../../.agents/docs/2026-11-04-hanim-play-api-design.md)
for the target Object/Animation/Effect/Config scopes and bound Program compiler.
The current single-source binding policy is the strict first morph stage,
not the final per-channel conflict/blend model. Nested Program playback,
sequence/parallel object bindings, complete domains and UI migration remain open.

The [external CLI consumer](../../../../tools/ci/hanim-play-consumer/README.md)
uses these public operations and the optional renderer without UI/window
dependencies, outputs actual PNG regions and JSON provenance, and validates
crop pixels and geometric retarget continuity. The package's visual CTS also
exports three real frames and a region through the same native renderer.
