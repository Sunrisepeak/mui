# openmui.audio

Sound for openmui and its games (D-86). Sounds are data, and so is music. Voices are mixed into buses, each bus runs through its effects (and, routed, into another bus) into a limited master, and everything is rendered a block at a time with nothing allocated. Every sample is a function of what was played and when, so an offline render is the same bits on every platform: a match's audio can be pinned like its pixels.

```cpp
import openmui.audio;

auto sounds = omui::audio::read_sounds(text, "fight.sound.omui.toml");   // every problem at its line and column
omui::audio::Mixer mixer;                                                 // 48 kHz, 48 voices
const auto punch = mixer.add((*sounds)[0]);
mixer.add_effect("sfx", omui::audio::echo(0.09f, 0.3f, 0.25f, mixer.rate()));
mixer.duck("music", {.key = "sfx", .depth = 0.5f});                       // the music under the hits
mixer.play(punch, /*at sample*/ 4800, /*gain*/ 0.9f, /*pan*/ -0.4f);    // from any thread
mixer.render(block);                                                      // on the audio thread, or offline
```

- **Sounds** (`*.sound.omui.toml`): `[sound.<name>]` tables, each with layers.
  - A layer is a source: a sine (a table made with hanim's math), a triangle, a band-limited square or saw (polyBLEP), or seeded noise.
  - Its pitch glides exponentially from one value to another over its length. Its filter (low, high or band pass) has a cutoff that glides too.
  - An envelope (attack, hold, decay, sustain, release) shapes it, then soft clipping, gain and an equal-power pan.
  - A punch is a burst of low-passed noise over a falling sine; a bell is a sine and its overtone.
  - A layer can also play a recording (`wave = "recording"`, `recording = "<name>"`): a voice or a foley take that the program adds with `Mixer::add_recording` (mono samples at their rate; `read_wav` reads 8-, 16- and 24-bit PCM or 32-bit float, mixing the channels down). It is read between its samples at its `speed` (2: twice as fast, an octave up; a glide too), held for its length unless the file gives an envelope, and filtered, driven and panned like any layer. A fighting game's announcer is the pack's takes, a touch lower, darker and doubled.
  - `write_sounds` writes them so they read back as themselves.
- **The mix** (`Mixer`):
  - Voices start on their exact sample. Past the most at once, the oldest gives way.
  - Buses ("sfx", "music", or any name) have a gain and effects, in order. A bus can be routed into another (`route`) and ducked under another (`duck`: a peak follower on the key, the sidechain).
  - An effect is a plugin (`Effect`). Built in (`effect(EffectSpec)`): low, high and band pass filters, an echo (straight or ping-pong, in seconds or in beats), a reverb (Freeverb: eight damped combs and four allpasses a side, a pre-delay) and a chorus. A program can write its own.
  - A source is a plugin too (`Source`): it adds its own voices into the buses on the mixer's clock. The sequencer is one.
  - The master has a peak limiter (peaks under −1 dBFS); `limiter()` says how hard it has worked.
  - `play` may be called from one thread while another renders: a lock-free queue sits between them.
  - Nothing is allocated while rendering; a sound's tables are made when it is added. Buses, effects and sources are set up before another thread renders.
- **Offline** (`render`, `wav`, `hash`, `levels`): a video's track, or a test's pinned bits.
  - The DSP uses `-ffp-contract=off` and no fast-math. Every transcendental comes from `hanim::dm`, so no platform's libm enters a sample; denormals are flushed by hand, the same everywhere.
  - `levels` measures a mix: peak, RMS, integrated loudness (ITU-R BS.1770: K-weighted, gated), the energy of five bands and the channels' correlation.
  - The test scene's hash is the same on GCC, LLVM and wasm.

## Music

```cpp
auto music = omui::audio::read_music(text, "study.music.omui.toml");     // every problem at its line and column, a wrong note at its token
auto song = omui::audio::Sequencer::attach(*music, mixer);                // its instruments made, its buses under "music"
(*song)->start(mixer.clock());                                            // from any thread: the intro, then the loop
(*song)->set_intensity(0.8f);                                             // from any thread: which layers play, from the next bar
(*song)->finish();                                                        // its outro from the next bar
```

A piece (`*.music.omui.toml`) is written for people and agents to read:

```toml
[music]
name = "study"
tempo = 128
meter = [4, 4]

[patch.zheng]                          # how an instrument sounds: a built-in, shaded
base = "guzheng"
pluck = { decay = 3.6 }

[instrument.lead]
patch = "zheng"
sends = { hall = 0.25 }

[bus.hall]
effects = [{ kind = "reverb", size = 0.82, wet = 1, dry = 0 }]

[pattern.hook]                         # a token a step: a note, `.` a rest, `:n` steps long,
instrument = "lead"                    # `+` a chord, `>` a slide, `!` an accent, `?` a soft one
step = "1/16"
notes = "D5:3! F5? G5:2 A5:6 G5? F5? D5:2 | A5>C6:6! A5? G5? E5:4 D5:2 E5:2"

[pattern.drums]
[pattern.drums.lanes]                  # a lane an instrument, a character a sixteenth
taiko = "X.....x.x...X..."
kick  = "x.......x.....x."

[section.a]
bars = 8
layers = [
    { pattern = "hook" },
    { pattern = "drums", intensity = [0.6, 1] },     # plays while the intensity is in [0.6, 1]
]

[arrangement]
intro = ["intro"]
loop = ["a", "b"]
outro = ["end"]
```

- **Patches** (`[patch.<name>]`, or one of the 28 built-ins by name — `builtin_patches()`), each an engine:
  - `analog`: band-limited saws, pulses (with PWM) or triangles, up to seven in unison, detuned and spread across the stereo field, with a sub an octave down and noise (pads, supersaws, leads, reese and pulse basses).
  - `fm`: two operators, the modulator's index falling from its attack to its sustain, with feedback (e-pianos, bells, plucks, basses).
  - `pluck`: a Karplus–Strong string, its delay tuned to its loop filter's phase delay and read with Hermite interpolation, so it is in tune from A2 to the top and can slide and wobble (koto, guzheng, harp).
  - `drum`: a membrane's falling tone and its modes (1.59, 2.14 ×), noise and six square waves at a cymbal's ratios through the patch's filter, a stick's click, bursts for a clap (kick, snare, hats, toms, taiko, woodblock, crash).
  - Each has an ADSR (curves falling 60 dB in their time), a filter with its own envelope and key tracking, a vibrato that fades in, a slide time and drive. `base` starts a patch from a built-in; `write_music` writes only what differs.
- **Instruments**: a patch with a gain, a pan, a legato, a transposition, a number of voices (past that the oldest is played again, its sound ringing into the new note), sends to the piece's buses, a `gate` rhythm (a sixteenth a step: gated saws), a `choke` group (open and closed hats) and `humanize` (a little softer and later by each note's place in the piece).
- **Buses**: the piece's own (`music.<name>`, under the mixer's "music"); each of the file's under it or another, with effects and ducking (a pad pumping under its kick).
- **Patterns**: notes as text or drum lanes, any step (`"1/16"`, `"1/12"` for triplets), swing. Between two bar lines lie whole bars.
- **Sections and layers**: a layer plays a pattern from bar `at`, `times` over, transposed, with another instrument (a melody doubled), while the intensity is in its range. The game sets the intensity; each bar takes it as it begins.
- **The arrangement**: the intro once, the loop over and over (what rings at its end rings into its start), the outro once `finish` is asked for.
- **Timing**: ticks (3840 a whole note) become samples by an exact fraction, so a bar starts on its sample however long the piece has looped. Nothing is allocated while it plays.
- **Offline**: `render_music(music, seconds, cues, solo)`: intensity cues, instruments alone.

## omui-music

The command line for people, agents and CI (D-61): JSON out and an exit code.

```sh
cd modules/media/audio
mcpp run --release omui-music -- check piece.music.omui.toml           # problems, or its bars and seconds
mcpp run --release omui-music -- render piece.music.omui.toml --out piece.wav --seconds 60 --intensity 0.3@0,0.9@30 [--solo lead,bass] [--float]
mcpp run --release omui-music -- stats piece.music.omui.toml --seconds 60   # the mix and each instrument alone: loudness, bands, the limiter
mcpp run --release omui-music -- fmt piece.music.omui.toml             # the one way it is written
```

`render` answers the WAV's hash, its levels, how hard the limiter worked and how many times faster than real time it rendered.

The fighting game's stage themes are written this way: [study](../../../showcase/fighting/music/study.music.omui.toml) (墨's) and [street](../../../showcase/fighting/music/street.music.omui.toml) (霓's).

## Observability

A span `audio.render` per render and a counter `audio.voices` per block; a span `audio.music` per block a piece renders, a counter `audio.music.voices` and a mark `audio.music.bar` at each bar; all in category `audio`.

## Packages

Devices and codecs are plugins in their own packages:

| Package | |
|---|---|
| [device](device) | The system's default output through miniaudio, or its null backend for headless runs and tests |

Codecs (WAV, FLAC, Opus) and the Web's output (Web Audio) come next.

`tests/audio_test.cpp` covers sounds and the mixer; `tests/music_test.cpp` covers pieces: the format read and written back, located problems, instruments in tune, effects and ducking, notes on their samples, intensity taking effect at the next bar, the loop going round without a click, nothing allocated while playing, the trace, and a piece's bits pinned (GCC and LLVM).
