// mlibs.time — one set of time types and exact conversions for every engine
// (convergence plan §5.2). Clocks stay their domain's (a time source is not a
// stepper): this says what a time is and how one becomes another, exactly.
//
//   seconds  continuous time (std::chrono::duration<double>): curves, springs
//   Flicks   exact time (int64, 1/705,600,000 s): edits, frames, samples —
//            every common frame rate and sample rate is a whole number of them
//   Rate     frames a second as a ratio (60/1, 30000/1001): a frame's start in
//            flicks (exact) and in seconds (one correctly rounded division,
//            the same double everywhere), the frame a time falls in
export module mlibs.time;

import std;

export namespace mlibs::time {

using seconds = std::chrono::duration<double>;

using Flicks = std::int64_t;
inline constexpr Flicks flicks_per_second = 705'600'000;

[[nodiscard]] constexpr double to_seconds(Flicks t) noexcept { return static_cast<double>(t) / static_cast<double>(flicks_per_second); }
// The nearest flick to a time in seconds.
[[nodiscard]] Flicks from_seconds(double s) noexcept;

struct Rate {
    std::int64_t num = 60;
    std::int64_t den = 1;

    // Whether a frame is a whole number of flicks (every rate a timeline may
    // have is: 24, 25, 30, 48, 50, 60, 120, the NTSC ones …).
    [[nodiscard]] constexpr bool exact() const noexcept { return num > 0 && den > 0 && (flicks_per_second * den) % num == 0; }
    // A frame's length, and frame n's start, in flicks.
    [[nodiscard]] constexpr Flicks frame_length() const noexcept { return flicks_per_second * den / num; }
    [[nodiscard]] constexpr Flicks frame_time(std::int64_t n) const noexcept { return n * frame_length(); }
    // The frame a time falls in (a time before zero falls in a negative frame).
    [[nodiscard]] constexpr std::int64_t frame_at(Flicks t) const noexcept {
        const Flicks f = frame_length();
        return t >= 0 ? t / f : -((-t + f - 1) / f);
    }

    // Frame n's start in seconds: an integer product and one correctly
    // rounded division, so the same double on every platform.
    [[nodiscard]] constexpr seconds time_of(std::int64_t frame) const noexcept {
        return seconds(static_cast<double>(frame * den) / static_cast<double>(num));
    }
    // The frame shown at t: the last that starts at or before it (a frame's
    // own start maps back to it).
    [[nodiscard]] std::int64_t frame_at(seconds t) const noexcept;
    [[nodiscard]] constexpr seconds frame_duration() const noexcept { return seconds(static_cast<double>(den) / static_cast<double>(num)); }
    [[nodiscard]] constexpr double fps() const noexcept { return static_cast<double>(num) / static_cast<double>(den); }

    // Written out: GCC 16.1 crashes on a defaulted comparison here once an importer uses it.
    friend constexpr bool operator==(const Rate& a, const Rate& b) noexcept { return a.num == b.num && a.den == b.den; }
};

// Samples at a rate that divides a second exactly (44100, 48000, 96000 …).
[[nodiscard]] constexpr bool exact_samples(std::int64_t rate) noexcept { return rate > 0 && flicks_per_second % rate == 0; }
[[nodiscard]] constexpr Flicks sample_time(std::int64_t rate, std::int64_t n) noexcept { return n * (flicks_per_second / rate); }
[[nodiscard]] constexpr std::int64_t sample_at(std::int64_t rate, Flicks t) noexcept {
    const Flicks f = flicks_per_second / rate;
    return t >= 0 ? t / f : -((-t + f - 1) / f);
}

}  // namespace mlibs::time
