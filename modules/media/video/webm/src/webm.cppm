// openmui.video.webm — WebM for openmui.video: VP9 video and Opus sound in
// the container every browser plays, as an encoder plugin, and a decoder
// that reads such a file back (round trips, tools, tests).
//
//     auto webm = video::webm_encoder("intro.webm", {.quality = 24, .speed = 5});
//     std::array<video::Encoder*, 1> encoders{webm.get()};
//     auto done = video::encode(format, frames, track, encoders, out);
//
// Pictures go in as openmui.video converts them (Y'CbCr 4:2:0, BT.709,
// limited range) and the file says so (VP9's colour space and range, WebM's
// Colour element: BT.709 matrix, primaries and transfer, broadcast range,
// chroma sited between its pixels). Frame i is stamped at ⌊i·den/num⌋ s in
// nanoseconds; the sound is Opus at 48 kHz in 20 ms packets, its encoder's
// look-ahead given as the track's codec delay and the last packet's padding
// as its discard padding, so a player hears exactly the samples that went in,
// from the first frame's time. Everything is single-threaded unless asked
// otherwise: one thread gives the same video bytes on every platform (libvpx's
// portable C); Opus is floating point, so the sound is compared within a
// tolerance rather than to the bit.
export module openmui.video.webm;

import std;
export import openmui.video;

export namespace omui::video {

struct WebmOptions {
    int  quality = 24;           // VP9 at constant quality: its level, 0 (best, largest) … 63
    int  bitrate = 0;            // kb/s: a target (variable bit rate) instead of a quality; 0: none
    int  speed = 5;              // libvpx's cpu-used: 0 (slowest, smallest) … 9 (fastest)
    bool realtime = false;       // libvpx's real-time mode (with speed 5–9: the fastest encodes)
    bool lossless = false;       // every pixel of the Y'CbCr picture kept
    int  keyframes = 0;          // at most this many frames between key frames (0: libvpx's default)
    int  threads = 1;            // >1: tile columns and row multithreading (the bytes may differ by machine)
    int  audio_bitrate = 128;    // Opus, kb/s
    // The WebM's track UIDs (mkvmuxer's are random unless given): fixed, so
    // the same video gives the same file.
    std::uint64_t video_uid = 1, audio_uid = 2;
};

// A WebM encoder: VP9 for the pictures, Opus for the sound track (when the
// format has one: 48 kHz, one or two channels).
[[nodiscard]] std::unique_ptr<Encoder> webm_encoder(std::string name = "video.webm", WebmOptions options = {});

// A WebM file read back: its pictures (VP9, 8-bit 4:2:0), each one's time,
// and its sound (Opus, interleaved, the codec delay and the discard padding
// taken off: the samples that went in).
struct DecodedWebm {
    std::int32_t              width = 0, height = 0;
    std::vector<Yuv420>       frames;
    std::vector<std::int64_t> frame_ns;
    std::int32_t              rate = 0, channels = 0;
    std::vector<float>        samples;
    std::int64_t              duration_ns = 0;
    std::string               video_codec, audio_codec;
    bool                      bt709 = false, limited_range = false;  // what the Colour element says
};
[[nodiscard]] std::expected<DecodedWebm, std::string> decode_webm(std::span<const std::byte> file);

}  // namespace omui::video
