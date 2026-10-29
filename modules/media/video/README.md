# openmui.video

Frames and a sound track encoded on exact frame and sample counts (D-86): the frame pipeline with "present" swapped for "encode". A video is frames from a source and a sound track from `openmui.audio`'s offline render, handed to encoders a frame at a time — each frame with exactly the samples that play under it. Everything here is integer arithmetic or `openmui.audio`'s deterministic mix, so an encode is the same bytes on every platform and its hash can be pinned like a picture's.

```cpp
import openmui.video;
namespace video = omui::video;

const video::Format format{.width = 1280, .height = 720, .timing = {{60, 1}, 48000}, .frames = 600};
video::Card card(format);                          // or a FrameSource of your own: a view on the headless host
const auto track = video::card_track(format);      // or video::soundtrack(description, dir)
video::DirectoryOutput out("render");
auto y4m = video::y4m_encoder("intro.y4m");
auto wav = video::wav_encoder("intro.wav");
std::array<video::Encoder*, 2> encoders{y4m.get(), wav.get()};
auto done = video::encode(format, card, track, encoders, out);   // each file's name, size and hash
```

- **Timing** (`Timing`): a frame rate is a fraction (`hanim::FrameRate`: 24, 30, 60, 30000/1001), a sample rate a whole number, and frame *i* starts on sample ⌊*i* · rate · den / num⌋ — integers, never a float adding up. At 48 kHz a frame at 24, 30 or 60 fps is 2000, 1600 or 800 samples; at 30000/1001 it is 1601 or 1602, five frames exactly 8008. `frame_of_sample`, `time_of` (any time base: nanoseconds for WebM) and `frames_in` answer the other ways round.
- **Pictures** (`Yuv420`): Y′CbCr 4:2:0, BT.709, limited range — what HD video and browsers expect. The conversion is 16-bit fixed point; each 2 × 2 block's chroma comes from the sum of its four pixels (the centred siting of 4:2:0). Black is 16, white 235, every grey has Cb = Cr = 128 exactly; a pixel that is not opaque counts as over black. `to_rgba` goes back (within 2 of the original), `psnr` measures how alike two pictures are, `read_y4m` reads a Y4M file back.
- **Sources** (`FrameSource`): frame 0, 1, 2 … in order, borrowed until the next. Built in: the sync card (`Card`) and PNG files by pattern. A view is a host's: `omui video` renders one on the headless host's clock, a step a frame.
- **Encoders** (`Encoder`): a plugin — `begin`, `frame(index, pixels, samples)` for every frame, `finish`. Built in, with no third-party code but `openmui.image`'s PNG:
  - `y4m_encoder`: Y4M (YUV4MPEG2), raw 4:2:0 frames that every encoder reads (ffmpeg, vpxenc, SVT-AV1, x264), tagged `C420jpeg XCOLORRANGE=LIMITED`;
  - `png_encoder`: a PNG a frame, named by a pattern (`"frame-{:05}.png"`);
  - `wav_encoder`: the sound track, 16-bit PCM or 32-bit float.

  A compressed format is a package of its own that implements `Encoder` (WebM with VP9 and Opus: `openmui.video.webm`, coming once its libraries are published).
- **Outputs** (`Output`): where the files go — `DirectoryOutput`, or `MemoryOutput` (tests; a tool that hashes what it would write). A file can be gone back into (a container writes its sizes and index last); every closed file is reported with its size and FNV-1a hash.
- **The driver** (`encode`): asks the source for each frame and hands each encoder the frame and its samples; the track is cut, or filled with silence, to the frames' length. A span `video.encode` per encode and `video.frame` per frame, a span `video.soundtrack` per sound track, in category `video`.

## The sync card

`Card` is a test signal for a player, an encoder or a pipeline, and the frames tests and tools use when no view is at hand: the seven 75% colour bars and their reversed castellations, a grey ramp, a line that crosses the bars once a second a step a frame (a dropped or repeated frame shows as a jump or a stall), the timecode and the frame number, and a box that turns white for the frame each second begins in. `card_track` beeps (1 kHz, −6 dBFS) for exactly that frame's samples from its first, so on a player that keeps sound and picture together the flash and the beep agree.

## Descriptions

A video as data, `*.video.omui.toml`: read with every problem at its line and column (the nearest right word as a hint), written back as itself (`write_description`).

```toml
doc = "Three seconds of the sync card, with its beeps and a click on frame 30."

[video]
width = 1280
height = 720
fps = 30                     # or "30000/1001"
seconds = 3                  # or frames = 90

[frames]
card = "sync"                # or png = "shots/{:05}.png", or view = "intro.view.omui.json"

[audio]                      # none: no sound track
music = "theme.music.omui.toml"
intensity = [{ at = 0, value = 0.3 }, { at = 2, value = 1 }]
sounds = "ui.sound.omui.toml"
recordings = { voice = "voice.wav" }     # what the sounds' recording layers play
cues = [{ sound = "click", frame = 30 }, { sound = "whoosh", at = 1.25, gain = 0.8, pan = -0.5 }]
beeps = true
```

`soundtrack` renders it offline through `openmui.audio`'s mixer and limiter — the music with its intensity cued, each cue's sound from its exact sample (a cue on a frame from that frame's first sample) — then adds the card's beeps. `frame_source` gives the card or the PNG files.

## omui video

The command line is `omui video` ([tools/cli](../../../tools/cli)), which renders descriptions and view documents alike (a view needs the headless host, which this package does not know): `render` to Y4M (with a WAV beside it), a PNG sequence or a WAV; `still`; `check`; `probe`; `timing`. One JSON object out and an exit code (D-61).

## Tests

`tests/video_test.cpp`: every frame's samples exact at 24, 30 and 60 fps for ten minutes and at 30000/1001 (never drifting); the conversion at black, white, every grey and the primaries, and a round trip; the card's flash and beep on the same frame to the sample at four rates; Y4M read back as the frames that went in; a PNG sequence and a WAV the frames' length, the same bytes in memory and on disk; a short or long track; a description read, its problems located, written back as itself; a sound track's cues on their frames' first samples. The Y4M's bytes, the PNG sequence's and WAV's, and the sound track's samples are pinned.
