module hanim.core;

import std;

namespace hanim {

namespace {

bool finite(double value) noexcept { return std::isfinite(value) && std::abs(value) < 1e12; }
bool finite(Vec2 value) noexcept { return finite(value.x) && finite(value.y); }

constexpr std::array<std::string_view, 21> names{
    "fade_in",  "rise_in",  "sink_in",  "slide_in",  "settle_in",  "pop_in",  "zoom_in",  "spin_in",  "unfold_in", "write_in", "drop_in",
    "fade_out", "rise_out", "sink_out", "slide_out", "settle_out", "pop_out", "zoom_out", "spin_out", "fold_out",  "unwrite"};

double mixd(double from, double to, double t) noexcept { return from + (to - from) * t; }
Vec2 both(double value) noexcept { return {value, value}; }

}  // namespace

std::string_view describe(PresenceError error) noexcept {
    switch (error) {
    case PresenceError::unknown_presence: return "no course of that name";
    case PresenceError::invalid_value: return "a course's value is not finite (or a drop not above)";
    case PresenceError::invalid_time: return "a course's duration is not a finite time, 0 or more";
    case PresenceError::invalid_ease: return "a course's ease is not valid";
    }
    return "unknown";
}

std::span<const std::string_view> presence_names() noexcept { return names; }

PresencePose Presence::pose(double progress) const noexcept {
    // An entrance runs from away to rest, an exit from rest to away.
    const double t = kind == PresenceKind::enter ? 1 - progress : progress;
    return {{mixd(0, away.offset.x, t), mixd(0, away.offset.y, t)}, {mixd(1, away.scale.x, t), mixd(1, away.scale.y, t)}, mixd(0, away.rotation, t)};
}

double Presence::opacity(double progress) const noexcept {
    if (!fades) return 1;
    if (kind == PresenceKind::enter) return window(progress, fade_from, 1);
    return 1 - window(progress, 0, fade_to);
}

std::expected<Presence, PresenceError> presence(std::string_view name, const PresenceOptions& options) {
    Presence c;
    c.name = std::string(name);
    // The defaults, an entry a course: its kind, pose, pace and what it draws instead of moving.
    const auto entrance = [&](PresencePose away, double duration, Ease ease) {
        c.kind = PresenceKind::enter, c.away = away, c.duration = seconds(duration), c.ease = ease;
    };
    const auto exit = [&](PresencePose away, double duration, Ease ease) {
        c.kind = PresenceKind::exit, c.away = away, c.duration = seconds(duration), c.ease = ease;
    };
    const double distance = options.distance.value_or(24);
    if (name == "fade_in") entrance({}, .3, ease::out_quad);
    else if (name == "rise_in") entrance({.offset = {0, distance}}, .4, ease::out_cubic);
    else if (name == "sink_in") entrance({.offset = {0, -distance}}, .4, ease::out_cubic);
    else if (name == "slide_in") entrance({.offset = options.offset.value_or(Vec2{-40, 0})}, .45, ease::out_cubic);
    else if (name == "settle_in") entrance({.scale = both(options.scale.value_or(.96))}, .25, ease::out_cubic);
    else if (name == "pop_in") entrance({.scale = both(options.scale.value_or(.3))}, .45, ease::out_back);
    else if (name == "zoom_in") entrance({.scale = both(options.scale.value_or(1.4))}, .4, ease::out_cubic);
    else if (name == "spin_in") entrance({.scale = {.6, .6}, .rotation = options.radians.value_or(-1.2)}, .5, ease::out_cubic);
    else if (name == "unfold_in") entrance({.scale = {1, .05}}, .4, ease::out_cubic);
    else if (name == "write_in") entrance({}, .6, ease::in_out_cubic), c.writes = true, c.fades = false;
    else if (name == "drop_in") entrance({}, 1, ease::linear), c.drop = options.height.value_or(160), c.fades = false;
    else if (name == "fade_out") exit({}, .25, ease::in_quad);
    else if (name == "rise_out") exit({.offset = {0, -distance}}, .35, ease::in_cubic);
    else if (name == "sink_out") exit({.offset = {0, distance}}, .35, ease::in_cubic);
    else if (name == "slide_out") exit({.offset = options.offset.value_or(Vec2{40, 0})}, .4, ease::in_cubic);
    else if (name == "settle_out") exit({.scale = both(options.scale.value_or(.96))}, .2, ease::in_cubic);
    else if (name == "pop_out") exit({.scale = both(options.scale.value_or(.3))}, .35, ease::in_back);
    else if (name == "zoom_out") exit({.scale = both(options.scale.value_or(1.4))}, .35, ease::in_cubic);
    else if (name == "spin_out") exit({.scale = {.6, .6}, .rotation = options.radians.value_or(1.2)}, .45, ease::in_cubic);
    else if (name == "fold_out") exit({.scale = {1, .05}}, .35, ease::in_cubic);
    else if (name == "unwrite") exit({}, .5, ease::in_out_cubic), c.writes = true, c.fades = false;
    else return std::unexpected(PresenceError::unknown_presence);
    if (options.duration) c.duration = *options.duration;
    if (options.ease) c.ease = *options.ease;
    if (options.fade) (c.kind == PresenceKind::enter ? c.fade_from : c.fade_to) = *options.fade;
    // As a recipe's options are checked: a time, an ease, finite values.
    const double length = c.duration.count();
    if (!std::isfinite(length) || length < 0 || (length > 0 && length < std::numeric_limits<double>::denorm_min()))
        return std::unexpected(PresenceError::invalid_time);
    if (!c.ease.valid()) return std::unexpected(PresenceError::invalid_ease);
    if (!finite(c.away.offset) || !finite(c.away.scale) || !finite(c.away.rotation) || !std::isfinite(c.fade_from) || !std::isfinite(c.fade_to) ||
        (name == "drop_in" && !(finite(c.drop) && c.drop > 0)))
        return std::unexpected(PresenceError::invalid_value);
    return c;
}

std::expected<Presence, PresenceError> together(const Presence& first, const Presence& second) {
    if (first.kind != second.kind) return std::unexpected(PresenceError::invalid_value);
    Presence out = first;
    out.name = first.name + "+" + second.name;
    out.away.offset = {first.away.offset.x + second.away.offset.x, first.away.offset.y + second.away.offset.y};
    out.away.scale = {first.away.scale.x * second.away.scale.x, first.away.scale.y * second.away.scale.y};
    out.away.rotation = first.away.rotation + second.away.rotation;
    out.duration = std::max(first.duration, second.duration);
    out.fades = first.fades || second.fades;
    out.fade_from = std::max(first.fade_from, second.fade_from);
    out.fade_to = std::min(first.fade_to, second.fade_to);
    out.writes = first.writes || second.writes;
    out.drop = std::max(first.drop, second.drop);
    return out;
}

}  // namespace hanim
