# xtools.audition

A timeline's gain automation heard and seen, offline: one clip's keyed gain drives a real audio bus under a reference tone, and a headless omui view shows the curve, the playhead and the level frame by frame.

```sh
xtools audition.automation curve.timeline.xdef.ts --track music --clip fade --out out/fade [--property gain] [--seconds 2] [--fps 60]
```

It writes `out/fade.wav` (the bus's sound: 220 Hz at 0.1 gain under the curve), `out/fade.png` (eight frames as a contact sheet), `out/fade.webp` (every frame, lossless) and `out/fade.report.xdef.json` (the rate, samples, the PCM's hash, each frame's time, gain and pixel hash, the timeline as read). No window and no audio device are opened, and the view only reads: the bus's clock is checked unchanged by it.

It is a composition of three projects' parts — [hanim.timeline](../../hanim/timeline)'s clip and keys, [xengine.audio](../../xengine/kernel/audio)'s mixer and gain automation, an omui view on the headless host — which no one of them owns, so it is the toolchain whole's (D-204; moved here from omui's command line, convergence plan RC3.3). The subject is `audition` (its catalog [`audition.ops.xdef.ts`](audition.ops.xdef.ts)); how an Agent uses it is [`skill.omui.md`](skill.omui.md).

## Experimental (release 0.1, D-199)

Labelled experimental in 0.1: it ships, and its top goal stands.

- **Target**: audition any automated property of any track (gain, pan, a filter's cut-off) against the track's own sounds, beside the timeline it came from.
- **Now**: one clip's keyed gain on a real bus under a reference tone; the view's frames, sheet, animation and report; the sound the same at any observation rate.
- **Gap**: one property (gain), a reference tone rather than the track's sounds, whole seconds 1–10.
- **Limits**: a bounded tool, not a resampler or a scene player; sample rates 8–192 kHz.
- **Graduation to preview**: the track's own sounds and any automated property, reached from the timeline editor.

Tests (`tests/audition_test.cpp`): the same sound at three observation rates (the view never changes what is heard), the frames' times and gains as the curve says, a missing clip and a rate that is not one refused.
