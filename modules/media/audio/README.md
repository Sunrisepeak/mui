# openmui.audio

Sound for openmui and its games (D-86). Sounds are data. Voices are mixed into buses, each bus runs through its effects into a limited master, and everything is rendered a block at a time with nothing allocated. Every sample is a function of what was played and when, so an offline render is the same bits on every platform: a match's audio can be pinned like its pixels.

```cpp
import openmui.audio;

auto sounds = omui::audio::read_sounds(text, "fight.sound.omui.toml");   // every problem at its line and column
omui::audio::Mixer mixer;                                                 // 48 kHz, 48 voices
const auto punch = mixer.add((*sounds)[0]);
mixer.add_effect("sfx", omui::audio::echo(0.09f, 0.3f, 0.25f, mixer.rate()));
mixer.play(punch, /*at sample*/ 4800, /*gain*/ 0.9f, /*pan*/ -0.4f);    // from any thread
mixer.render(block);                                                      // on the audio thread, or offline
```

- **Sounds** (`*.sound.omui.toml`): `[sound.<name>]` tables, each with layers.
  - A layer is a source: a sine (a table made with hanim's math), a triangle, a band-limited square or saw (polyBLEP), or seeded noise.
  - Its pitch glides exponentially from one value to another over its length. Its filter (low, high or band pass) has a cutoff that glides too.
  - An envelope (attack, hold, decay, sustain, release) shapes it, then soft clipping, gain and an equal-power pan.
  - A punch is a burst of low-passed noise over a falling sine; a bell is a sine and its overtone.
  - `write_sounds` writes them so they read back as themselves.
- **The mix** (`Mixer`):
  - Voices start on their exact sample. Past the most at once, the oldest gives way.
  - Buses ("sfx", "music", or any name) have a gain and effects, in order. An effect is a plugin (`Effect`): built in are a filter and an echo, or a program writes its own.
  - The master has a peak limiter (peaks under −1 dBFS).
  - `play` may be called from one thread while another renders: a lock-free queue sits between them.
  - Nothing is allocated while rendering; a sound's tables are made when it is added.
- **Offline** (`render`, `wav`, `hash`): a video's track, or a test's pinned bits.
  - The DSP uses `-ffp-contract=off` and no fast-math. Every transcendental comes from `hanim::dm`, so no platform's libm enters a sample.
  - The test scene's hash is the same on GCC, LLVM and wasm.
- **Observability**: a span `audio.render` per render and a counter `audio.voices` per block, both in category `audio`.

Devices and codecs are plugins in their own packages:

| Package | |
|---|---|
| [device](device) | The system's default output through miniaudio, or its null backend for headless runs and tests |

Codecs (WAV, FLAC, Opus) and the Web's output (Web Audio) come next.
