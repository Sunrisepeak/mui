// Outputs, the built-in encoders and the driver.
module openmui.video;

import std;
import openmui.image;
import openmui.audio;
import openmui.trace;

namespace omui::video {

namespace {

std::filesystem::path path_of(std::string_view s) { return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size())); }

std::string utf8(const std::filesystem::path& p) {
    const std::u8string u = p.generic_u8string();
    return {reinterpret_cast<const char*>(u.data()), u.size()};
}

// ── memory ──

class MemoryFile final : public File {
public:
    std::string            name;
    std::vector<std::byte> data;
    std::int64_t           at = 0;

    bool write(std::span<const std::byte> bytes) override {
        const auto end = static_cast<std::size_t>(at) + bytes.size();
        if (end > data.size()) data.resize(end);
        std::ranges::copy(bytes, data.begin() + at);
        at = static_cast<std::int64_t>(end);
        return true;
    }
    std::int64_t position() const noexcept override { return at; }
    bool seek(std::int64_t position) override {
        if (position < 0 || position > static_cast<std::int64_t>(data.size())) return false;
        at = position;
        return true;
    }
};

// ── a directory ──

class DiskFile final : public File {
public:
    std::string           name;
    std::filesystem::path path;
    mutable std::fstream  stream;

    bool write(std::span<const std::byte> bytes) override {
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return static_cast<bool>(stream);
    }
    std::int64_t position() const noexcept override { return static_cast<std::int64_t>(stream.tellp()); }
    bool seek(std::int64_t position) override {
        stream.seekp(position);
        return static_cast<bool>(stream);
    }
};

}  // namespace

struct MemoryOutput::Impl {
    std::vector<std::unique_ptr<MemoryFile>> open;
    std::map<std::string, std::vector<std::byte>, std::less<>> closed;
    std::vector<Written> written;
};

MemoryOutput::MemoryOutput() : impl_(std::make_unique<Impl>()) {}
MemoryOutput::~MemoryOutput() = default;

std::expected<File*, std::string> MemoryOutput::create(std::string_view name) {
    auto f = std::make_unique<MemoryFile>();
    f->name = std::string(name);
    impl_->open.push_back(std::move(f));
    return impl_->open.back().get();
}

std::expected<void, std::string> MemoryOutput::close(File* file) {
    const auto it = std::ranges::find(impl_->open, file, [](const auto& p) { return static_cast<File*>(p.get()); });
    if (it == impl_->open.end()) return std::unexpected("closing a file this output did not open");
    MemoryFile& f = **it;
    std::erase_if(impl_->written, [&](const Written& w) { return w.name == f.name; });
    impl_->written.push_back({f.name, static_cast<std::int64_t>(f.data.size()), hash(std::span<const std::byte>(f.data))});
    impl_->closed[f.name] = std::move(f.data);
    impl_->open.erase(it);
    return {};
}

std::span<const Written> MemoryOutput::written() const noexcept { return impl_->written; }

std::span<const std::byte> MemoryOutput::bytes(std::string_view name) const noexcept {
    const auto it = impl_->closed.find(name);
    return it == impl_->closed.end() ? std::span<const std::byte>{} : std::span<const std::byte>(it->second);
}

struct DirectoryOutput::Impl {
    std::filesystem::path                  directory;
    std::vector<std::unique_ptr<DiskFile>> open;
    std::vector<Written>                   written;
};

DirectoryOutput::DirectoryOutput(std::filesystem::path directory) : impl_(std::make_unique<Impl>()) { impl_->directory = std::move(directory); }
DirectoryOutput::~DirectoryOutput() = default;

const std::filesystem::path& DirectoryOutput::directory() const noexcept { return impl_->directory; }

std::expected<File*, std::string> DirectoryOutput::create(std::string_view name) {
    auto f = std::make_unique<DiskFile>();
    f->name = std::string(name);
    f->path = impl_->directory / path_of(name);
    std::error_code ec;
    if (f->path.has_parent_path()) std::filesystem::create_directories(f->path.parent_path(), ec);
    f->stream.open(f->path, std::ios::in | std::ios::out | std::ios::trunc | std::ios::binary);
    if (!f->stream) return std::unexpected(std::format("cannot write {}", utf8(f->path)));
    impl_->open.push_back(std::move(f));
    return impl_->open.back().get();
}

std::expected<void, std::string> DirectoryOutput::close(File* file) {
    const auto it = std::ranges::find(impl_->open, file, [](const auto& p) { return static_cast<File*>(p.get()); });
    if (it == impl_->open.end()) return std::unexpected("closing a file this output did not open");
    DiskFile& f = **it;
    f.stream.flush();
    const bool ok = static_cast<bool>(f.stream);
    // Its hash from what is on the disk now (an encoder may have gone back).
    f.stream.seekg(0);
    std::uint64_t h = 0xcbf29ce484222325ull;
    std::int64_t size = 0;
    std::array<char, 1 << 16> buffer{};
    while (f.stream) {
        f.stream.read(buffer.data(), buffer.size());
        const auto n = f.stream.gcount();
        for (std::streamsize i = 0; i < n; ++i) {
            h ^= static_cast<std::uint8_t>(buffer[static_cast<std::size_t>(i)]);
            h *= 0x100000001b3ull;
        }
        size += n;
    }
    f.stream.close();
    const std::string name = f.name, where = utf8(f.path);
    impl_->open.erase(it);
    if (!ok) return std::unexpected(std::format("cannot write {}", where));
    std::erase_if(impl_->written, [&](const Written& w) { return w.name == name; });
    impl_->written.push_back({name, size, h});
    return {};
}

std::span<const Written> DirectoryOutput::written() const noexcept { return impl_->written; }

namespace {

// ── the built-in encoders ──

class Y4mEncoder final : public Encoder {
public:
    explicit Y4mEncoder(std::string name) : name_(std::move(name)) {}
    std::string_view name() const noexcept override { return "y4m"; }
    std::expected<void, std::string> begin(const Format& format, Output& out) override {
        auto f = out.create(name_);
        if (!f) return std::unexpected(f.error());
        out_ = &out;
        file_ = *f;
        format_ = format;
        const std::string header = y4m_header(format.width, format.height, format.timing.fps);
        if (!file_->write(std::as_bytes(std::span(header)))) return std::unexpected(std::format("cannot write {}", name_));
        return {};
    }
    std::expected<void, std::string> frame(std::int64_t, const imageio::View& pixels, std::span<const float>) override {
        if (pixels.width != format_.width || pixels.height != format_.height)
            return std::unexpected(std::format("a frame of {} × {} in a video of {} × {}", pixels.width, pixels.height, format_.width, format_.height));
        to_yuv420(pixels, picture_);
        constexpr std::string_view mark = "FRAME\n";
        const bool ok = file_->write(std::as_bytes(std::span(mark))) && file_->write(std::as_bytes(std::span(picture_.y))) &&
                        file_->write(std::as_bytes(std::span(picture_.u))) && file_->write(std::as_bytes(std::span(picture_.v)));
        if (!ok) return std::unexpected(std::format("cannot write {}", name_));
        return {};
    }
    std::expected<void, std::string> finish() override { return out_->close(file_); }

private:
    std::string name_;
    Output*     out_ = nullptr;
    File*       file_ = nullptr;
    Format      format_;
    Yuv420      picture_;
};

class PngEncoder final : public Encoder {
public:
    explicit PngEncoder(std::string pattern) : pattern_(std::move(pattern)) {}
    std::string_view name() const noexcept override { return "png"; }
    std::expected<void, std::string> begin(const Format& format, Output& out) override {
        out_ = &out;
        format_ = format;
        if (!frame_name(pattern_, 0)) return std::unexpected(std::format("`{}` does not name a frame (a pattern like \"frame-{{:05}}.png\")", pattern_));
        return {};
    }
    std::expected<void, std::string> frame(std::int64_t index, const imageio::View& pixels, std::span<const float>) override {
        const std::string name = *frame_name(pattern_, index);
        auto png = imageio::encode_png(pixels);
        if (!png) return std::unexpected(png.error());
        auto f = out_->create(name);
        if (!f) return std::unexpected(f.error());
        if (!(*f)->write(std::as_bytes(std::span(*png)))) return std::unexpected(std::format("cannot write {}", name));
        return out_->close(*f);
    }
    std::expected<void, std::string> finish() override { return {}; }

private:
    std::string pattern_;
    Output*     out_ = nullptr;
    Format      format_;
};

class WavEncoder final : public Encoder {
public:
    WavEncoder(std::string name, bool float32) : name_(std::move(name)), float32_(float32) {}
    std::string_view name() const noexcept override { return "wav"; }
    std::expected<void, std::string> begin(const Format& format, Output& out) override {
        if (format.channels <= 0) return std::unexpected("a WAV of a video without a sound track");
        out_ = &out;
        format_ = format;
        samples_.clear();
        samples_.reserve(static_cast<std::size_t>(format.samples() * format.channels));
        return {};
    }
    std::expected<void, std::string> frame(std::int64_t, const imageio::View&, std::span<const float> samples) override {
        samples_.insert(samples_.end(), samples.begin(), samples.end());
        return {};
    }
    std::expected<void, std::string> finish() override {
        const auto w = audio::wav(samples_, format_.timing.rate, float32_, format_.channels);
        auto f = out_->create(name_);
        if (!f) return std::unexpected(f.error());
        if (!(*f)->write(w)) return std::unexpected(std::format("cannot write {}", name_));
        return out_->close(*f);
    }

private:
    std::string        name_;
    bool               float32_;
    Output*            out_ = nullptr;
    Format             format_;
    std::vector<float> samples_;
};

}  // namespace

std::unique_ptr<Encoder> y4m_encoder(std::string name) { return std::make_unique<Y4mEncoder>(std::move(name)); }
std::unique_ptr<Encoder> png_encoder(std::string pattern) { return std::make_unique<PngEncoder>(std::move(pattern)); }
std::unique_ptr<Encoder> wav_encoder(std::string name, bool float32) { return std::make_unique<WavEncoder>(std::move(name), float32); }

std::expected<Encoded, std::string> encode(const Format& format, FrameSource& frames, std::span<const float> track, std::span<Encoder* const> encoders,
                                           Output& out) {
    if (format.width <= 0 || format.height <= 0) return std::unexpected("a video has a size");
    if (!format.timing.valid()) return std::unexpected("a video has a frame rate and a sample rate");
    if (format.frames <= 0) return std::unexpected("a video has frames");
    trace::Span span("video.encode", "video");
    for (Encoder* e : encoders)
        if (auto r = e->begin(format, out); !r) return std::unexpected(std::format("{}: {}", e->name(), r.error()));
    const auto channels = static_cast<std::size_t>(std::max(0, format.channels));
    std::vector<float> silence;
    for (std::int64_t i = 0; i < format.frames; ++i) {
        trace::Span frame_span("video.frame", "video");
        auto pixels = frames.frame(i);
        if (!pixels) return std::unexpected(std::format("frame {}: {}", i, pixels.error()));
        // The samples under this frame; past the track's end, silence.
        const auto first = static_cast<std::size_t>(format.timing.sample_of(i)) * channels;
        const auto count = static_cast<std::size_t>(format.timing.samples_in(i)) * channels;
        std::span<const float> samples;
        if (channels > 0) {
            if (first + count <= track.size()) samples = track.subspan(first, count);
            else {
                silence.assign(count, 0.0f);
                if (first < track.size()) std::ranges::copy(track.subspan(first), silence.begin());
                samples = silence;
            }
        }
        for (Encoder* e : encoders)
            if (auto r = e->frame(i, *pixels, samples); !r) return std::unexpected(std::format("{}: frame {}: {}", e->name(), i, r.error()));
    }
    for (Encoder* e : encoders)
        if (auto r = e->finish(); !r) return std::unexpected(std::format("{}: {}", e->name(), r.error()));
    Encoded done;
    done.frames = format.frames;
    done.samples = channels > 0 ? format.samples() : 0;
    done.files.assign(out.written().begin(), out.written().end());
    return done;
}

}  // namespace omui::video
