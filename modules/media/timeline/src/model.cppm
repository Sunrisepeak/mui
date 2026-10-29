// openmui.timeline:model — tracks of clips in exact time, and what plays when.
export module openmui.timeline:model;

import std;
import hanim;

export namespace omui::timeline {

// ── Time ────────────────────────────────────────────────────────────────

// A flick: 1/705,600,000 of a second. Every common frame rate and sample
// rate divides a second into a whole number of them.
using Flicks = std::int64_t;
inline constexpr Flicks flicks_per_second = 705'600'000;

// Frames a second, as a fraction (30000/1001 for NTSC's 29.97).
struct Rate {
    std::int32_t num = 60, den = 1;
    friend constexpr bool operator==(const Rate&, const Rate&) noexcept = default;
};

// Whether a frame of this rate is a whole number of flicks (every rate a
// timeline may have is).
[[nodiscard]] constexpr bool exact(Rate r) noexcept {
    return r.num > 0 && r.den > 0 && (flicks_per_second * r.den) % r.num == 0;
}
// A frame's length, and frame n's start.
[[nodiscard]] constexpr Flicks frame_length(Rate r) noexcept { return flicks_per_second * r.den / r.num; }
[[nodiscard]] constexpr Flicks frame_time(Rate r, std::int64_t n) noexcept { return n * frame_length(r); }
// The frame a time falls in (a time before zero falls in a negative frame).
[[nodiscard]] constexpr std::int64_t frame_at(Rate r, Flicks t) noexcept {
    const Flicks f = frame_length(r);
    return t >= 0 ? t / f : -((-t + f - 1) / f);
}
// A sample's start at a sample rate that divides a second exactly (44100, 48000, 96000 …).
[[nodiscard]] constexpr bool exact_samples(std::int32_t rate) noexcept { return rate > 0 && flicks_per_second % rate == 0; }
[[nodiscard]] constexpr Flicks sample_time(std::int32_t rate, std::int64_t n) noexcept { return n * (flicks_per_second / rate); }
[[nodiscard]] constexpr std::int64_t sample_at(std::int32_t rate, Flicks t) noexcept {
    const Flicks f = flicks_per_second / rate;
    return t >= 0 ? t / f : -((-t + f - 1) / f);
}
[[nodiscard]] constexpr double to_seconds(Flicks t) noexcept { return static_cast<double>(t) / static_cast<double>(flicks_per_second); }
// The nearest flick to a time in seconds.
[[nodiscard]] Flicks from_seconds(double s) noexcept;

// ── What a timeline holds ───────────────────────────────────────────────

// What a track carries: pictures (videos, views, stills), sound, values that
// animate something (a property's keys alone), or events (cues, markers).
enum class Kind : std::uint8_t { video, audio, animation, events };

// A clip's fades: its level rising from 0 over `in`, falling to 0 over `out`.
struct Fade {
    Flicks      in = 0, out = 0;
    hanim::Ease ease = hanim::ease::linear;
    friend bool operator==(const Fade&, const Fade&) = default;
};

// A property of a clip keyed over the clip's own time (its first frame is 0):
// a picture's opacity or position, a sound's pan, anything its player names.
struct Property {
    std::string               name;
    hanim::Track<double>      keys;
};

// A piece of a source placed on a track: from `at` for `length`, starting
// `in` into the source and playing it at `speed` (2: twice as fast; the
// source's time is in + (t − at) × speed).
struct Clip {
    std::string           name;
    std::string           source;   // what it plays: a file, a view document, a sound's name — its player's to resolve
    Flicks                at = 0, length = 0;
    Flicks                in = 0;
    double                speed = 1;
    float                 level = 1;  // a picture's opacity, a sound's gain
    Fade                  fade;
    std::vector<Property> properties;

    [[nodiscard]] Flicks end() const noexcept { return at + length; }
    // Whether it plays at t (its start in, its end out).
    [[nodiscard]] bool covers(Flicks t) const noexcept { return t >= at && t < end(); }
    // Where in its source it is at t.
    [[nodiscard]] Flicks source_at(Flicks t) const noexcept;
    // Its level at t: `level`, through its fades.
    [[nodiscard]] float level_at(Flicks t) const noexcept;
    // A property's value at t (its keys at the clip's own time), or `otherwise` when it has none of that name.
    [[nodiscard]] double value(std::string_view property, Flicks t, double otherwise = 0) const;
};

struct Track {
    std::string       name;
    Kind              kind = Kind::video;
    bool              muted = false;
    std::vector<Clip> clips;  // by `at`, none overlapping (`check` says when not)
};

struct Marker {
    Flicks      at = 0;
    std::string name;
    friend bool operator==(const Marker&, const Marker&) = default;
};

struct Timeline {
    std::string         name = "timeline";
    Rate                rate;                 // frames a second
    std::int32_t        sample_rate = 48000;  // its sound's
    std::int32_t        width = 1920, height = 1080;
    std::vector<Track>  tracks;               // drawn bottom first; sound mixed
    std::vector<Marker> markers;              // by time

    // Its last clip's end (or last marker's).
    [[nodiscard]] Flicks duration() const noexcept;
    [[nodiscard]] std::optional<Flicks> marker(std::string_view name) const noexcept;
};

// What breaks a timeline's rules: a rate or sample rate that is not a whole
// number of flicks, a clip of no length or a negative start, clips on a track
// out of order or overlapping, a speed of 0. Empty: it holds together.
[[nodiscard]] std::vector<std::string> check(const Timeline& timeline);

// What plays at t: each clip that covers it, by track (bottom first), with
// where in its source and at what level. `out` is cleared and refilled, so a
// player reusing it allocates nothing once it has grown.
struct Playing {
    std::size_t track = 0, clip = 0;
    Flicks      source = 0;
    float       level = 1;
};
void playing(const Timeline& timeline, Flicks t, std::vector<Playing>& out);

}  // namespace omui::timeline
