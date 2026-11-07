// hanim.core:presence — how something comes in or goes out, defined once (the
// L6 entrances and exits; the motion vocabulary's courses): the pose at its
// far end relative to where it rests, its own pace, and the part of the
// course its opacity takes.
//
// An object recipe plays a course over its duration and ease (hanim's
// `rise_in(object, 24)`); a UI transition plays it over the transition's own
// curve, for a drawing piece that appears or vanishes (hanim.visual). The
// names are the specification's (registry/motion.toml `[[presence]]`).
//
//     auto rise = hanim::presence("rise_in", {.distance = 8});
//     const PresencePose pose = rise->pose(0.5);   // half-way up
//     const double opacity = rise->opacity(0.5);
export module hanim.core:presence;

import std;
import :time;
import :ease;
import :value;

export namespace hanim {

enum class PresenceKind : std::uint8_t { enter, exit };

// A pose relative to rest: moved by `offset`, scaled by `scale` and turned
// by `rotation` (radians) about the course's anchor.
struct PresencePose {
    Vec2   offset{0, 0};
    Vec2   scale{1, 1};
    double rotation = 0;
    friend constexpr bool operator==(const PresencePose&, const PresencePose&) = default;
};

struct Presence {
    std::string name;
    PresenceKind  kind = PresenceKind::enter;
    PresencePose  away;  // enter: where it comes from; exit: where it goes
    seconds     duration{0.3};
    Ease        ease;
    // The opacity's window on the progress: an entrance turns opaque over
    // [fade_from, 1], an exit transparent over [0, fade_to]. A course that
    // does not fade (writing, dropping) stays opaque.
    bool        fades = true;
    double      fade_from = 0, fade_to = 1;
    // Instead of a pose: strokes drawn along their length (an entrance) or
    // undrawn (an exit); a fall from `drop` above, bouncing.
    bool        writes = false;
    double      drop = 0;
    Vec2        anchor{0.5, 0.5};  // what it scales and turns about, a fraction of its bounds

    // The pose `progress` through it (progress already eased by whoever
    // plays it): an entrance from `away` to rest, an exit from rest to `away`.
    [[nodiscard]] PresencePose pose(double progress) const noexcept;
    // The opacity `progress` through it, linear within its window.
    [[nodiscard]] double opacity(double progress) const noexcept;
};

// What a course's own values become (none: its defaults).
struct PresenceOptions {
    std::optional<double>  distance;  // rise_* and sink_*
    std::optional<Vec2>    offset;    // slide_*
    std::optional<double>  scale;     // settle_*, pop_*, zoom_*
    std::optional<double>  radians;   // spin_*
    std::optional<double>  height;    // drop_in
    std::optional<double>  fade;      // the window's inner bound: fade_from of an entrance, fade_to of an exit
    std::optional<seconds> duration;
    std::optional<Ease>    ease;
};

enum class PresenceError : std::uint8_t { unknown_presence, invalid_value, invalid_time, invalid_ease };
[[nodiscard]] std::string_view describe(PresenceError error) noexcept;

// A course by its name, with `options` over its defaults.
[[nodiscard]] std::expected<Presence, PresenceError> presence(std::string_view name, const PresenceOptions& options = {});
// Every course's name, entrances first.
[[nodiscard]] std::span<const std::string_view> presence_names() noexcept;
// Two courses of one kind at once (a rise that also settles): offsets and
// rotations added, scales multiplied, the later fade window, the longer
// duration, `first`'s ease and anchor.
[[nodiscard]] std::expected<Presence, PresenceError> together(const Presence& first, const Presence& second);

}  // namespace hanim
