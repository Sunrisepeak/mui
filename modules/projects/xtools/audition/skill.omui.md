# audition — skill

How an Agent (or a person) checks that a timeline's gain automation sounds and looks as meant, without a window or speakers. The operation is the catalog's ([`audition.ops.xdef.ts`](audition.ops.xdef.ts)): `xtools audition.automation FILE --track T --clip C --out PREFIX`. One report out; exit 0 done, 1 failed with why, 2 a wrong request.

## Doing

1. Name the track and clip exactly as the timeline does (`--track`, `--clip`); the property is `gain` unless the clip keys another.
2. Run it with `--seconds` covering the clip and `--fps` at the timeline's rate.

## Verifying

- The report's `observations`: the gain at each frame's time follows the keys (holds where a key holds, eases where it eases).
- The contact sheet (`PREFIX.png`) and the animation (`PREFIX.webp`): the playhead and the level move as the curve does.
- The sound (`PREFIX.wav`, its `pcm_hash`) is the same at any `--fps`: the view only reads.
