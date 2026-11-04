# Playback contract: strict morph programs and explicit image scopes

This implementation contract covers owned endpoints, explicit relative time,
strict morph/effect segments, nested Program schedules and common lifecycle.
General typed channels and device/resource composition remain implementation work.

| Operation | Observable behavior |
|---|---|
| play | Normalize the single-object form to PreparedProgram::single, or accept a frozen compiled Program. Prepare two frame buffers per track before consuming a slot. Resolve ObjectRefs at preparation; accept external Snapshots through the same player. Every logical source track is exclusively occupied until release. |
| advance | Validate nonnegative finite elapsed time, stage every active playback, publish only if the whole batch samples successfully. Failure consumes no elapsed time and preserves all visible poses/effects. This is owning-thread failure atomicity, not GPU/physics/business rollback. |
| pause/resume | Change running intent without moving the frame. A finished directional endpoint remains finished until seek or reverse makes it active. |
| seek/seek_progress | Clamp finite local time/linear progress; preserve pause intent. Inspection reports both linear and sampled progress. Invalid values preserve visible state. |
| reverse/rate | Finite nonzero rate controls direction and speed. Reverse at a finished forward endpoint enables backward playback. Large finite elapsed/rate values saturate before multiplication overflow. |
| retarget | For one track, deep-prepare the actual current phase as new source, replacing a sequence if present. Preserve active effect values/targets and pause intent; begin a forward segment with the old speed magnitude. Multi-track-to-one-target replacement is explicitly unsupported. New plan revision invalidates old renderer bindings. Failure preserves prior state/budgets. |
| cancel | Hold retains current pose; restore samples the exact segment source, bypassing easing. No business state is undone. Both retain source occupancy/resources until release; cancelled playbacks cannot resume/seek. |
| release | Free retained buffers/plans, return cardinality quota and invalidate the playback generation. Other handles are unaffected. |
| inspect/frame/list | Read committed native state without resampling or formatting logs. Inspect has global time and counts; sampled progress/object/target exist for one track only. frame(ref,index) identifies the active segment; frame(ref) refuses ambiguous multiple tracks. Too-small list output is rejected before writing. Borrowed spans/pointers expire at the next Engine mutation or destruction. |

Zero duration samples the target immediately. For retargeted noninstant
segments, time zero is persistently anchored to the captured source, including
arbitrary seek back to zero; authored step curves still jump at positive times.
Ordinary initial play retains core jump-start behavior. Contour-domain eased
progress is bounded to [0,1]; this is not general spring/elastic spatial motion.

The Engine owns its convenient ObjectRegistry. External snapshot endpoints
remain usable after the host/registry disappears. Playback owner issuance
reuses that registry's process-local identity; slot/generation mechanics reuse
openmui.memory. Object/plan revisions, playback generations and time are
separate. Engine quotas bound summed track-max output part/cubic/parameter counts,
including finished/cancelled playbacks. Program preparation quotas bound the
retained plans of all segments. Per-segment effect limits cannot be bypassed by
an externally prepared Program. These are not allocator-byte or outside-lease limits.

Single owning thread or external synchronization; move only while quiescent.
Log observers can inspect committed state, but mutation reentry is refused
without another recursive log. Preparation and explicit retarget may allocate;
steady native advance/query reuse buffers. The optional renderer, materials,
OpenAS provider/transport and application semantics remain outside this player.

## Program image scopes

Program-frame queries expose all selected track views plus a flat scope-parameter
view and preparation identity. Geometry and leaf/scope parameters are sampled
into staging buffers and published together. Borrowed views have the same
lifetime as existing EngineFrame views. Normal pause, seek, reverse and cancel
apply to the single program clock. Scope parameter storage counts towards the
existing parameter budget. Retargeting a scoped program is explicitly refused
until a scope-preserving retarget contract exists; it must not discard effects.
