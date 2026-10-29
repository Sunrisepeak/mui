# openmui.video.webm

WebM for [openmui.video](..): VP9 video and Opus sound in the container every browser plays, as an encoder plugin (`webm_encoder`), and a decoder that reads such a file back (`decode_webm`) for round trips, tools and tests. The libraries come through mwrap (D-83), imported only in the native unit: `mcpplibs.mwrap.libvpx` (libvpx 1.17.0 and libwebm 1.0.0.32 over `compat.libvpx` and `compat.libwebm`) and `mcpplibs.mwrap.opus` (libopus 1.6.1). All BSD-3-Clause; VP9 and Opus are royalty-free.

```cpp
import openmui.video.webm;
namespace video = omui::video;

auto webm = video::webm_encoder("intro.webm", {.quality = 24, .speed = 5});
std::array<video::Encoder*, 1> encoders{webm.get()};
auto done = video::encode(format, frames, track, encoders, out);   // a .webm any browser plays

auto back = video::decode_webm(bytes);   // its frames (Y'CbCr 4:2:0) and times, its samples
```

- **Pictures**: openmui.video's Y'CbCr 4:2:0 (BT.709, limited range, integer conversion), and the file says so — VP9's colour space and range, and WebM's Colour element (BT.709 matrix, primaries and transfer, broadcast range, chroma sited between its pixels), so ffmpeg and browsers show the colours that went in. Frame *i* is stamped at ⌊*i* · den / num⌋ s.
- **VP9** (`WebmOptions`): constant quality (`quality`, 0 best … 63) or a bit rate (`bitrate`, kb/s); `speed` (libvpx's cpu-used, 0 … 9); `realtime`; `lossless` (every Y'CbCr sample back); `keyframes` (the most frames between two); `threads` (more than one: tile columns and row multithreading).
- **Opus**: the sound track at 48 kHz, mono or stereo, in 20 ms packets at `audio_bitrate` kb/s. The encoder's look-ahead is the track's codec delay and the last packet's padding its discard padding, so a player hears exactly the samples that went in, from the first frame's time (ffmpeg decodes exactly as many). Any other rate is refused.
- **Reproducible**: track UIDs are given (mkvmuxer's own are random), and with one thread libvpx's portable C writes the same video bytes on every platform: the tests pin a video-only file's hash. Opus is floating point, so a file with sound is compared within a tolerance.
- **Decoding**: `decode_webm` reads a WebM's VP9 frames (with their times) and its Opus sound (the codec delay and the discard padding taken off) — what the tests compare and `omui video probe` reports.

`tests/webm_test.cpp`: the sync card in, every frame back at its time above 38 dB, BT.709 limited range said, the video-only bytes pinned and the same twice; lossless every sample back; with sound at 30 and 30000/1001 fps exactly the samples that went in, each second's beep still within a millisecond of its flash frame's first sample, the tone above 15 dB SNR; a 44.1 kHz track refused.

Speed: libvpx's portable C (no SIMD yet: a later revision of compat.libvpx), single-threaded by default; `speed` 6–9, `realtime` and `threads` are the fast paths.
