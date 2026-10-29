// openmui.video:timing — when each frame starts, in samples, exactly.
//
// A frame rate is a fraction (hanim::FrameRate: 24/1, 30000/1001), a sample
// rate a whole number, so frame i starts on sample ⌊i · rate · den / num⌋:
// integers only, never a float accumulating. At 48 kHz a 24, 30 or 60 fps
// frame is 2000, 1600 or 800 samples; at 30000/1001 frames are 1601 or 1602
// samples, five frames in exactly 8008. Sound and picture can then never
// drift apart, however long the video.
export module openmui.video:timing;

import std;
import hanim;

export namespace omui::video {

// ⌊a / b⌋ for b > 0 (C++ division truncates toward zero).
[[nodiscard]] constexpr std::int64_t floor_div(std::int64_t a, std::int64_t b) noexcept {
    const std::int64_t q = a / b;
    return (a % b != 0 && a < 0) ? q - 1 : q;
}

struct Timing {
    hanim::FrameRate fps{30, 1};
    std::int32_t     rate = 48000;  // samples a second, each channel

    [[nodiscard]] constexpr bool valid() const noexcept { return fps.num > 0 && fps.den > 0 && rate > 0; }

    // The first sample of frame `frame`: ⌊frame · rate · den / num⌋.
    [[nodiscard]] constexpr std::int64_t sample_of(std::int64_t frame) const noexcept {
        return floor_div(frame * rate * fps.den, fps.num);
    }
    // How many samples frame `frame` lasts.
    [[nodiscard]] constexpr std::int64_t samples_in(std::int64_t frame) const noexcept { return sample_of(frame + 1) - sample_of(frame); }
    // The frame a sample plays under: the last one starting at or before it.
    [[nodiscard]] constexpr std::int64_t frame_of_sample(std::int64_t sample) const noexcept {
        // The largest f with f · rate · den ≤ sample · num + (num − 1).
        return floor_div(sample * fps.num + fps.num - 1, static_cast<std::int64_t>(rate) * fps.den);
    }
    // When frame `frame` starts, in units of 1/`per_second` of a second
    // (1'000'000'000: nanoseconds, a WebM timestamp), rounded down; exact
    // where it divides (i · den · per_second / num computed without overflow
    // for any frame a video has).
    [[nodiscard]] constexpr std::int64_t time_of(std::int64_t frame, std::int64_t per_second) const noexcept {
        const std::int64_t a = frame * fps.den;
        return floor_div(a, fps.num) * per_second + floor_div((a - floor_div(a, fps.num) * fps.num) * per_second, fps.num);
    }
    // The frames in `seconds`: as many as start before it ends (3 s at
    // 30000/1001 is 90 frames, the last starting at 2.97 s).
    [[nodiscard]] std::int64_t frames_in(double seconds) const noexcept {
        const double f = seconds * static_cast<double>(fps.num) / static_cast<double>(fps.den);
        const auto whole = static_cast<std::int64_t>(f);
        return static_cast<double>(whole) + 1e-9 < f ? whole + 1 : whole;
    }
    // The frame shown at `seconds`: the last one starting at or before it.
    [[nodiscard]] std::int64_t frame_at(double seconds) const noexcept {
        return static_cast<std::int64_t>(seconds * static_cast<double>(fps.num) / static_cast<double>(fps.den) + 1e-9);
    }
};

// "30", "30000/1001", "29.97" (taken as 30000/1001; likewise 23.976 and 59.94).
[[nodiscard]] std::optional<hanim::FrameRate> parse_rate(std::string_view text) noexcept;
// The shortest text of a rate: "30", "30000/1001".
[[nodiscard]] std::string rate_text(hanim::FrameRate rate);

}  // namespace omui::video
