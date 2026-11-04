# Playback contract v1: first object morph stage

This implementation contract covers owned endpoints, explicit relative time,
the strict library morph and ordered effect parameters. The complete bound
Program/channel model is specified separately and remains implementation work.

| Operation | Observable behavior |
|---|---|
| play | Prepare endpoint snapshots, correspondence, parameter plan and two frame buffers before consuming a slot. Resolve ObjectRefs at preparation; accept explicit external Snapshots through the same player. Source occupancy is exclusive in this first stage. |
| advance | Validate nonnegative finite elapsed time, stage every active playback, publish only if the whole batch samples successfully. Failure consumes no elapsed time and preserves all visible poses/effects. This is owning-thread failure atomicity, not GPU/physics/business rollback. |
| pause/resume | Change running intent without moving the frame. A finished directional endpoint remains finished until seek or reverse makes it active. |
| seek/seek_progress | Clamp finite local time/linear progress; preserve pause intent. Inspection reports both linear and sampled progress. Invalid values preserve visible state. |
| reverse/rate | Finite nonzero rate controls direction and speed. Reverse at a finished forward endpoint enables backward playback. Large finite elapsed/rate values saturate before multiplication overflow. |
| retarget | Deep-prepare the current pose as new source. Preserve current effect values/targets and pause intent; begin a forward segment with the old speed magnitude. New plan revision invalidates old renderer bindings. Failed preparation leaves prior revision, frame and budgets unchanged. |
| cancel | Hold retains current pose; restore samples the exact segment source, bypassing easing. No business state is undone. Both retain source occupancy/resources until release; cancelled playbacks cannot resume/seek. |
| release | Free retained buffers/plans, return cardinality quota and invalidate the playback generation. Other handles are unaffected. |
| inspect/frame/list | Read committed native state without resampling or formatting logs. Too-small list output is rejected before writing. Borrowed frame spans/plan pointers expire at the next Engine mutation or destruction. |

Zero duration samples the target immediately. For retargeted noninstant
segments, time zero is persistently anchored to the captured source, including
arbitrary seek back to zero; authored step curves still jump at positive times.
Ordinary initial play retains core jump-start behavior. Contour-domain eased
progress is bounded to [0,1]; this is not general spring/elastic spatial motion.

The Engine owns its convenient ObjectRegistry. External snapshot endpoints
remain usable after the host/registry disappears. Playback owner issuance
reuses that registry's process-local identity; slot/generation mechanics reuse
openmui.memory. Object/plan revisions, playback generations and time are
separate. Per-engine quotas bound retained part/cubic/parameter counts, including
finished/cancelled playbacks, not total allocator memory or outside leases.

Single owning thread or external synchronization; move only while quiescent.
Log observers can inspect committed state, but mutation reentry is refused
without another recursive log. Preparation and explicit retarget may allocate;
steady native advance/query reuse buffers. The optional renderer, materials,
OpenAS provider/transport and application semantics remain outside this player.
