// openmui.video:encode — frames and their samples handed to encoders.
//
//     video::Format format{.width = 1280, .height = 720, .timing = {{60, 1}, 48000}, .frames = 600};
//     video::Card card(format);                                  // or a host's FrameSource
//     auto track = video::card_track(format);                    // or a soundtrack()
//     video::DirectoryOutput out("render");
//     auto y4m = video::y4m_encoder("intro.y4m");
//     auto wav = video::wav_encoder("intro.wav");
//     std::array<video::Encoder*, 2> encoders{y4m.get(), wav.get()};
//     auto done = video::encode(format, card, track, encoders, out);   // files, sizes, hashes
//
// The driver asks the source for frame 0, 1, 2 … in order and hands each
// encoder the frame with exactly the samples that play under it (Timing),
// the track cut or filled with silence to the frames' length. Encoders are
// plugins: the built-ins below, or a package's (openmui.video.webm). Outputs
// are where the files go: a directory, or memory (tests, and a tool that
// hashes what it would write).
export module openmui.video:encode;

import std;
export import openmui.image;  // the pictures it takes and gives: imageio::View, imageio::Image
import :timing;

export namespace omui::video {

struct Format {
    std::int32_t width = 0, height = 0;
    Timing       timing;
    std::int64_t frames = 0;
    std::int32_t channels = 2;  // of the sound track; 0: none

    // Every channel's samples: the frames' length exactly.
    [[nodiscard]] std::int64_t samples() const noexcept { return timing.sample_of(frames); }
    [[nodiscard]] double seconds() const noexcept {
        return static_cast<double>(frames) * static_cast<double>(timing.fps.den) / static_cast<double>(timing.fps.num);
    }
};

// Frame `index`, asked for in order from 0; the pixels are borrowed until
// the next call. A source may render (a view stepped on its clock), read
// (PNG files) or draw (the card).
class FrameSource {
public:
    virtual ~FrameSource() = default;
    [[nodiscard]] virtual std::expected<imageio::View, std::string> frame(std::int64_t index) = 0;
};

// One file being written. An encoder may go back (a container's sizes and
// index are written last).
class File {
public:
    virtual ~File() = default;
    virtual bool write(std::span<const std::byte> bytes) = 0;
    [[nodiscard]] virtual std::int64_t position() const noexcept = 0;
    virtual bool seek(std::int64_t position) = 0;
};

// A file written: its name in the output, its size and its FNV-1a hash.
struct Written {
    std::string   name;
    std::int64_t  bytes = 0;
    std::uint64_t hash = 0;
};

// Where an encoder's files go.
class Output {
public:
    virtual ~Output() = default;
    // A new file (one of that name is replaced), open until close().
    [[nodiscard]] virtual std::expected<File*, std::string> create(std::string_view name) = 0;
    virtual std::expected<void, std::string> close(File* file) = 0;
    // The files closed so far, in the order they were.
    [[nodiscard]] virtual std::span<const Written> written() const noexcept = 0;
};

// Files in memory, by name.
class MemoryOutput final : public Output {
public:
    MemoryOutput();
    ~MemoryOutput() override;
    [[nodiscard]] std::expected<File*, std::string> create(std::string_view name) override;
    std::expected<void, std::string> close(File* file) override;
    [[nodiscard]] std::span<const Written> written() const noexcept override;
    // A file's bytes (empty: none of that name).
    [[nodiscard]] std::span<const std::byte> bytes(std::string_view name) const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Files in a directory (made when missing); a name may have directories in it.
class DirectoryOutput final : public Output {
public:
    explicit DirectoryOutput(std::filesystem::path directory);
    ~DirectoryOutput() override;
    [[nodiscard]] std::expected<File*, std::string> create(std::string_view name) override;
    std::expected<void, std::string> close(File* file) override;
    [[nodiscard]] std::span<const Written> written() const noexcept override;
    [[nodiscard]] const std::filesystem::path& directory() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// What turns frames and samples into files: a plugin. `begin` once, then
// `frame` for every frame in order — its pixels and the samples under it
// (interleaved, format.channels of them; none when there is no track) —
// then `finish`.
class Encoder {
public:
    virtual ~Encoder() = default;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    virtual std::expected<void, std::string> begin(const Format& format, Output& out) = 0;
    virtual std::expected<void, std::string> frame(std::int64_t index, const imageio::View& pixels, std::span<const float> samples) = 0;
    virtual std::expected<void, std::string> finish() = 0;
};

// ── built in ────────────────────────────────────────────────────────────

// Y4M (YUV4MPEG2): every frame as raw Y'CbCr 4:2:0 (to_yuv420), what every
// encoder reads (ffmpeg, x264, SVT-AV1, vpxenc). Pictures only.
[[nodiscard]] std::unique_ptr<Encoder> y4m_encoder(std::string name = "video.y4m");
// A PNG a frame (straight alpha kept), named by `pattern` formatted with the
// frame's index ("frame-{:05}.png"). Pictures only.
[[nodiscard]] std::unique_ptr<Encoder> png_encoder(std::string pattern = "frame-{:05}.png");
// The sound track as a WAV: 16-bit PCM, or 32-bit float. Sound only.
[[nodiscard]] std::unique_ptr<Encoder> wav_encoder(std::string name = "audio.wav", bool float32 = false);

// ── the driver ──────────────────────────────────────────────────────────

struct Encoded {
    std::int64_t         frames = 0;
    std::int64_t         samples = 0;  // each channel's
    std::vector<Written> files;        // every file the output holds, in order
};
// `track`: interleaved, format.channels a frame, at format.timing.rate; cut
// or filled with silence to format.samples(). Stops at the first error (the
// frame it happened at, in the message).
[[nodiscard]] std::expected<Encoded, std::string> encode(const Format& format, FrameSource& frames, std::span<const float> track,
                                                         std::span<Encoder* const> encoders, Output& out);

}  // namespace omui::video
