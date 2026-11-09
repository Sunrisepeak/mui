# xengine.timeline.audio

An optional adapter from timeline keys and clip fades to the audio engine's sample clock. It owns a frozen, immutable curve; audio and visual/Agent queries read the same values. Neither parent package depends on this adapter.

```cpp
import xengine.audio;
import xengine.timeline.audio;

auto gain = xengine::timeline::GainAutomation::prepare(clip, {.sample_rate = mixer.rate()});
if (!gain) { report(xengine::timeline::gain_error_name(gain.error())); return; }
auto bound = mixer.automate_gain("music", *gain);
if (!bound) { report(bound.error()); return; }
// At a visual timestamp; no audio render or live event is triggered.
float shown = (*gain)->at_sample(24000);
```

The chosen property defaults to `gain`. Its keys use clip-local presentation time, matching `Clip::value`; source `in`/`speed` do not retime them. Clip level and fades multiply the property. Gain is clamped to 0–8, including intentional easing overshoot. Before the clip and at/after its exclusive end, `outside` applies (default 1). Use `outside=0` for a silent audition. Gain sources multiply the bus's static gain after effects/ducking, before routing and the master limiter; sidechain keys retain their existing pre-gain meaning.

Preparation validates exact flick/sample conversion, clip/fade bounds, finite values, property identity and easing. It owns its selected keys so later edits do not mutate DSP. `sample(first, values)` fills an entire block with no allocations, events or clock advancement. Arbitrary and reverse queries reproduce values. Out-of-range integer timestamps return the outside value without overflowing. Same-time keys and hold segments retain hanim's declared cuts.

`Mixer::automate_gain` requires an existing bus and the same sample rate. Failure retains the previous source; null clears it. Its `GainSource` value erases a shared immutable typed owner with one function call per block; the adapter does not inherit from an audio class or add virtual calls. Bind/clear at a quiescent boundary, before another thread renders. This is not yet a live command queue, transport seek, timeline event scheduler or stateful DSP rewind. Renderer frame rate never drives sample generation. Audio remains independent of hanim/timeline; only this selected adapter imports the existing curve implementation.

## Minimal Agent / human SOP

1. Prepare a valid `.timeline.omui.ts` with an explicitly named track, clip and keyed property. Query `GainAutomation::at`/`at_sample` without a GUI to inspect any point. Pure library users need no UI, device or network.
2. Use the existing CLI for a bounded calibration audition:

   ```sh
   mcpp run --locked -p omui.cli --profile release -- audio automation curve.timeline.omui.ts --track music --clip fade --out /tmp/gain --seconds 2 --fps 60
   ```

   It exports actual float WAV, an eight-frame PNG sheet, a lossless WebP process and `.report.omui.json` with sample times, gain, pixel hashes, source snapshot and renderer conditions. The reference sound is a 220 Hz tone at 0.1 gain; it does not load `clip.source` or open an audio device. The readable image is rendered by omui itself without a window. The final frame has one replay interval; JSON timestamps identify the original observations.
3. Compare the WAV at different visual sample rates, inspect transitions and cuts, and listen to the file. Image/hash checks alone do not establish sound quality or realtime latency. Filesystem errors report already-created outputs; export is not an atomic multi-file transaction.
4. Apply the same prepared source to an application's real bus. Signal sampling is read-only; edits require a new prepared owner and an explicit safe bind boundary. Cancellation/undo of the editing task and device transport remain the host's responsibility. The CLI is a local headless tool, not a claim that an independent openas engine provider has shipped.

CTS: `mcpp test --locked -p xengine.timeline.audio` checks exact samples, frozen input, cuts, errors and 30/60/120 fps observers without PCM changes or repeated voices. `xengine.audio` covers signal lifetime/rate binding and block/routing semantics. `omui.cli` generates and verifies the actual files. The existing benchmark suite adds `audio/automated-mix` and a zero-allocation budget. Trace categories remain `audio`, with `timeline.audio.prepare` and `timeline.audio.sample` spans and `audio.automation.invalid` diagnostics for non-finite plugin values.
