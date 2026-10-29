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

}  // namespace

int main(int argc, char** argv) {
    Suite s("openmui.video.webm");

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
