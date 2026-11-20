// hanim.timeline:model — tracks of clips in exact time, and what plays when.
export module hanim.timeline:model;

import std;
import hanim.core;
export import mlibs.time;

// Time is mlibs.time's (flicks, rates, exact conversions); the package says it
// unqualified.
namespace hanim::timeline {
using mlibs::time::Flicks;
using mlibs::time::flicks_per_second;
using mlibs::time::Rate;
using mlibs::time::to_seconds;
using mlibs::time::from_seconds;
using mlibs::time::exact_samples;
using mlibs::time::sample_time;
using mlibs::time::sample_at;
}  // namespace hanim::timeline

export namespace hanim::timeline {

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

}  // namespace hanim::timeline
