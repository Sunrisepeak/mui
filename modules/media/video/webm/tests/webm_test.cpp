// openmui.video.webm: the sync card encoded into WebM (VP9 and Opus) and
// decoded back — every frame at its time and within a PSNR, the colour
// signalled, the sound exactly the samples that went in (the codec delay and
// the padding taken off), each second's beep still on its flash frame; a
// video-only file's bytes pinned (one thread: libvpx's portable C writes the
// same bytes everywhere), the same bytes twice; what Opus cannot take refused.
import std;
import hanim;
import openmui.video;
import openmui.video.webm;
import openmui.testing;

using namespace omui::testing;
namespace video = omui::video;

namespace {

std::string hex(std::uint64_t h) { return std::format("{:016x}", h); }

struct Encoded {
    std::vector<std::byte> bytes;
    std::uint64_t          hash = 0;
};

Encoded encode(const video::Format& f, video::WebmOptions o = {}) {
    video::Card card(f);
    const auto track = video::card_track(f);
    video::MemoryOutput out;
    auto webm = video::webm_encoder("card.webm", o);
    std::array<video::Encoder*, 1> enc{webm.get()};
    const auto done = video::encode(f, card, track, enc, out);
    check(done.has_value(), done ? "" : done.error());
    if (!done) return {};
    const auto b = out.bytes("card.webm");
    return {{b.begin(), b.end()}, done->files.at(0).hash};
}

double worst_psnr(const video::Format& f, const video::DecodedWebm& d) {
    video::Card card(f);
    double worst = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < d.frames.size(); ++i) worst = std::min(worst, video::psnr(video::to_yuv420(*card.frame(static_cast<std::int64_t>(i))), d.frames[i]).y);
    return worst;
}

// The card's frames from `first` on, asked for from 0 (a chunk's), or their
// negative (a chunk changed by an edit).
class Part final : public video::FrameSource {
public:
    Part(const video::Format& f, std::int64_t first, bool inverted) : card_(f), first_(first), inverted_(inverted) {}
    std::expected<omui::imageio::View, std::string> frame(std::int64_t index) override {
        auto v = card_.frame(first_ + index);
        if (!v || !inverted_) return v;
        pixels_.assign(v->rgba, v->rgba + static_cast<std::size_t>(v->stride) * static_cast<std::size_t>(v->height));
        for (std::size_t i = 0; i < pixels_.size(); i += 4)
            for (std::size_t c = 0; c < 3; ++c) pixels_[i + c] = static_cast<std::uint8_t>(255 - pixels_[i + c]);
        omui::imageio::View out = *v;
        out.rgba = pixels_.data();
        return out;
    }

private:
    video::Card               card_;
    std::int64_t              first_;
    bool                      inverted_;
    std::vector<std::uint8_t> pixels_;
};

struct ChunkRun {
    video::Chunked         done;
    std::vector<std::byte> bytes;
    std::int64_t           draws = 0;
};

// The card in chunks of 30 frames, kept in `dir`; chunk `edited` (−1: none) drawn inverted, its key changed.
ChunkRun chunked(const video::Format& f, const std::filesystem::path& dir, std::int64_t edited = -1, int at_once = 1) {
    ChunkRun run;
    std::mutex counting;
    video::ChunkPlan plan;
    plan.frames = 30;
    plan.at_once = at_once;
    plan.directory = dir;
    plan.key = [&](std::int64_t first, std::int64_t count) {
        return static_cast<std::uint64_t>(first) * 1000003u + static_cast<std::uint64_t>(count) + (first / 30 == edited ? 77u : 0u);
    };
    plan.draw = [&](std::int64_t first, std::int64_t, int) -> std::expected<std::unique_ptr<video::FrameSource>, std::string> {
        std::scoped_lock g(counting);
        ++run.draws;
        return std::make_unique<Part>(f, first, first / 30 == edited);
    };
    video::MemoryOutput out;
    auto done = video::encode_webm_chunked(f, video::card_track(f), plan, out, "chunks.webm");
    check(done.has_value(), done ? "" : done.error());
    if (!done) return run;
    run.done = *done;
    const auto b = out.bytes("chunks.webm");
    run.bytes.assign(b.begin(), b.end());
    return run;
}

}  // namespace

int main(int argc, char** argv) {
    Suite s("openmui.video.webm");

    s.test("chunks (D-54's fourth cache layer): drawn and encoded once, found after and never drawn again — the same file; an edited chunk alone drawn again; several at once the same", [] {
        const video::Format f{.width = 160, .height = 90, .timing = {{30, 1}, 48000}, .frames = 90, .channels = 2};
        const auto dir = scratch_dir("webm-chunks");
        const ChunkRun first = chunked(f, dir);
        check(first.done.chunks == 3 && first.done.reused == 0 && first.done.drawn == 90 && first.draws == 3, "every chunk drawn the first time");
        const auto d = video::decode_webm(first.bytes);
        check(d.has_value(), d ? "" : d.error());
        if (!d) return;
        check_eq(d->frames.size(), std::size_t{90});
        check(worst_psnr(f, *d) > 38, std::format("every frame within 38 dB (worst {:.1f})", worst_psnr(f, *d)));
        const auto track = video::card_track(f);
        check_eq(d->samples.size(), track.size());
        const ChunkRun again = chunked(f, dir);
        check(again.done.reused == 3 && again.done.drawn == 0 && again.draws == 0, "the second time, every chunk found: no frame drawn");
        check(again.bytes == first.bytes, "the same file");
        // An edit to the middle chunk's frames: that chunk alone drawn, the file what a run from nothing writes.
        const ChunkRun edited = chunked(f, dir, 1);
        check(edited.done.reused == 2 && edited.done.drawn == 30 && edited.draws == 1, std::format("one chunk drawn again ({} reused)", edited.done.reused));
        const ChunkRun fresh = chunked(f, scratch_dir("webm-chunks-fresh"), 1);
        check(fresh.done.reused == 0 && fresh.bytes == edited.bytes, "the same file as one encoded from nothing");
        check(fresh.bytes != first.bytes, "and not the unedited one");
        // Three chunks at once, each its own encoder: the same bytes.
        const ChunkRun parallel = chunked(f, scratch_dir("webm-chunks-parallel"), -1, 3);
        check(parallel.done.drawn == 90 && parallel.draws == 3 && parallel.bytes == first.bytes, "three at once: the same file");
    });

    s.test("pictures only: every frame back at its time, BT.709 limited range said, the bytes pinned and the same twice", [] {
        const video::Format f{.width = 160, .height = 90, .timing = {{30, 1}, 48000}, .frames = 30, .channels = 0};
        const Encoded a = encode(f);
        check(!a.bytes.empty());
        const auto d = video::decode_webm(a.bytes);
        check(d.has_value(), d ? "" : d.error());
        if (!d) return;
        check_eq(d->frames.size(), std::size_t{30});
        check(d->width == 160 && d->height == 90 && d->video_codec == "V_VP9" && d->audio_codec.empty() && d->bt709 && d->limited_range);
        bool times = d->frame_ns.size() == 30;
        for (std::size_t i = 0; times && i < 30; ++i) times = d->frame_ns[i] == f.timing.time_of(static_cast<std::int64_t>(i), 1'000'000'000) / 1'000'000 * 1'000'000;
        check(times, "each frame at its time (WebM keeps milliseconds)");
        const double psnr = worst_psnr(f, *d);
        std::println("    {} bytes, worst PSNR {:.2f} dB, {}", a.bytes.size(), psnr, hex(a.hash));
        check(psnr > 38, std::format("PSNR {:.2f}", psnr));
        check(encode(f).hash == a.hash, "the same bytes twice");
        check_eq(hex(a.hash), std::string("821cfb7347162cd1"));  // libvpx 1.17.0 and libwebm 1.0.0.32, one thread: every platform
    });

    s.test("lossless: every Y'CbCr sample back", [] {
        const video::Format f{.width = 64, .height = 36, .timing = {{24, 1}, 48000}, .frames = 6, .channels = 0};
        const auto d = video::decode_webm(encode(f, {.lossless = true}).bytes);
        check(d.has_value() && d->frames.size() == 6, d ? std::format("{} frames", d->frames.size()) : d.error());
        if (d) check(std::isinf(worst_psnr(f, *d)), std::format("PSNR {}", worst_psnr(f, *d)));
    });

    s.test("with sound: exactly the samples that went in, each second's beep still on its flash frame", [] {
        for (const hanim::FrameRate fps : {hanim::FrameRate{30, 1}, hanim::FrameRate{30000, 1001}}) {
            const video::Timing t{fps, 48000};
            const video::Format f{.width = 96, .height = 54, .timing = t, .frames = t.frames_in(2.0), .channels = 2};
            const Encoded e = encode(f);
            const auto d = video::decode_webm(e.bytes);
            check(d.has_value(), d ? "" : d.error());
            if (!d) continue;
            check(d->audio_codec == "A_OPUS" && d->rate == 48000 && d->channels == 2);
            check_eq(d->samples.size(), static_cast<std::size_t>(f.samples() * 2));
            check_eq(d->frames.size(), static_cast<std::size_t>(f.frames));
            // Each beep begins within a millisecond of its flash frame's first sample.
            for (std::int64_t i = 0; i < f.frames; ++i) {
                if (!video::card_flash(t, i)) continue;
                const std::int64_t at = t.sample_of(i);
                std::int64_t onset = -1;
                for (std::int64_t k = std::max<std::int64_t>(0, at - 480); k < std::min<std::int64_t>(f.samples(), at + 480) && onset < 0; ++k)
                    if (std::abs(d->samples[static_cast<std::size_t>(2 * k)]) > 0.1f) onset = k;
                check(onset >= at - 48 && onset <= at + 48, std::format("{} fps: the beep of frame {} at sample {}, its frame from {}", video::rate_text(fps), i, onset, at));
            }
            // The tone itself, within what a 128 kb/s Opus keeps.
            const auto original = video::card_track(f);
            double signal = 0, noise = 0;
            for (std::size_t k = 0; k < original.size(); ++k) {
                signal += double(original[k]) * original[k];
                noise += double(original[k] - d->samples[k]) * (original[k] - d->samples[k]);
            }
            const double snr = 10 * std::log10(signal / std::max(noise, 1e-12));
            std::println("    {} fps: {} bytes, sound SNR {:.1f} dB", video::rate_text(fps), e.bytes.size(), snr);
            check(snr > 15, std::format("SNR {:.1f}", snr));
        }
    });

    s.test("what Opus cannot take is refused", [] {
        const video::Format f{.width = 32, .height = 32, .timing = {{30, 1}, 44100}, .frames = 2, .channels = 2};
        video::Card card(f);
        video::MemoryOutput out;
        auto webm = video::webm_encoder("x.webm");
        std::array<video::Encoder*, 1> enc{webm.get()};
        const auto r = video::encode(f, card, {}, enc, out);
        check(!r && r.error().find("48 kHz") != std::string::npos);
        check(!video::decode_webm(std::as_bytes(std::span("not a webm", 10))).has_value());
    });

    return s.run(argc, argv);
}
