// The recipe library (L6): entrances, exits, attention cues, idle loops and
// moves, each an ordinary program of relative property writes from the
// actual pose, the group opacity effect, the stroke's trim and the squash —
// what any object (or a part of it) takes from wherever it is.
module hanim.program;
import std;

namespace hanim::anim {
namespace {

constexpr double two_pi = 2 * std::numbers::pi;

// A recipe's options with its own defaults where unset, checked.
struct Timing {
    seconds duration;
    Ease easing;
    std::string part;
};
std::expected<Timing, ProgramFailure> timing(const RecipeOptions& options, double fallback, Ease ease, double divisions = 1) {
    const seconds duration = options.duration.value_or(seconds(fallback));
    const double length = duration.count();
    if (!std::isfinite(length) || length < 0 || (length > 0 && length < std::numeric_limits<double>::denorm_min() * divisions))
        return std::unexpected(ProgramFailure{ProgramError::invalid_time});
    const Ease chosen = options.easing.value_or(ease);
    if (!chosen.valid()) return std::unexpected(ProgramFailure{ProgramError::invalid_ease});
    return Timing{duration, chosen, options.part};
}
Program bad_value() { return invalid_program(ProgramFailure{ProgramError::animation}.because(MorphFailure{MorphError::invalid_property})); }
bool finite(double value) { return std::isfinite(value) && std::abs(value) < 1e12; }
bool finite(Vec2 value) { return finite(value.x) && finite(value.y); }
bool invertible(double value) { return std::isfinite(value) && std::abs(value) > 1e-9 && std::abs(value) < 1e9; }

// A pose written at once: the start of an entrance.
BoundAnimation at_once(const ObjectInput& object, PropertyTargets targets) { return to(object, std::move(targets)).duration(seconds(0)); }
// A timed relative change, faded from `from` to `to` (no fade when equal).
BoundAnimation change(const ObjectInput& object, PropertyTargets targets, const Timing& t, float from = 1, float to_opacity = 1) {
    auto animation = to(object, std::move(targets)).duration(t.duration).easing(t.easing);
    if (from != to_opacity) animation = animation.with_effect(fx::opacity(from, to_opacity));
    return animation;
}

// An entrance: the pose `start` at once, then back to where it was, fading in.
Program enter(const ObjectInput& object, const Timing& t, PropertyTargets start, PropertyTargets back) {
    start.part = t.part;
    back.part = t.part;
    return sequence({Program(at_once(object, std::move(start))), Program(change(object, std::move(back), t, 0, 1))});
}
// An exit: to `away`, fading out.
Program leave(const ObjectInput& object, const Timing& t, PropertyTargets away) {
    away.part = t.part;
    return change(object, std::move(away), t, 1, 0);
}
PropertyTargets offset(Vec2 by) { return {.offset = by}; }
PropertyTargets scaled(Vec2 by) { return {.scale_factor = by}; }
PropertyTargets turned(double by) { return {.rotation_offset = by}; }
PropertyTargets still() { return {.offset = Vec2{0, 0}}; }

// A pose relative to rest as the relative writes that reach it: only what
// the pose changes (nothing: the still pose, an opacity-only course).
PropertyTargets targets_of(const PresencePose& pose) {
    PropertyTargets out;
    if (pose.offset != Vec2{0, 0}) out.offset = pose.offset;
    if (pose.scale != Vec2{1, 1}) out.scale_factor = pose.scale;
    if (pose.rotation != 0) out.rotation_offset = pose.rotation;
    if (!out.offset && !out.scale_factor && !out.rotation_offset) out.offset = Vec2{0, 0};
    return out;
}
// The writes that come back from it.
PropertyTargets back_of(const PresencePose& pose) {
    PropertyTargets out;
    if (pose.offset != Vec2{0, 0}) out.offset = Vec2{-pose.offset.x, -pose.offset.y};
    if (pose.scale != Vec2{1, 1}) out.scale_factor = Vec2{1 / pose.scale.x, 1 / pose.scale.y};
    if (pose.rotation != 0) out.rotation_offset = -pose.rotation;
    if (!out.offset && !out.scale_factor && !out.rotation_offset) out.offset = Vec2{0, 0};
    return out;
}

// A course of hanim.core's played on an object: the one definition of every
// entrance and exit (the UI's transitions play the same courses).
Program play_presence(const ObjectInput& object, std::string_view name, const PresenceOptions& values, const RecipeOptions& options) {
    PresenceOptions chosen = values;
    chosen.duration = options.duration;
    chosen.ease = options.easing;
    const auto made = presence(name, chosen);
    if (!made) {
        switch (made.error()) {
        case PresenceError::invalid_time: return invalid_program(ProgramFailure{ProgramError::invalid_time});
        case PresenceError::invalid_ease: return invalid_program(ProgramFailure{ProgramError::invalid_ease});
        default: return bad_value();
        }
    }
    const Presence& c = *made;
    const Timing t{c.duration, c.ease, options.part};
    if (c.writes) {
        const PartTarget target = part(object, t.part);
        if (c.kind == PresenceKind::exit) return to(target, ch::trim_start = 1.0).duration(t.duration).easing(t.easing);
        return sequence({Program(to(target, ch::trim_end = 0.0).duration(seconds(0))),
                         Program(to(target, ch::trim_end = 1.0).duration(t.duration).easing(t.easing))});
    }
    if (c.drop > 0) {
        PropertyTargets above = offset({0, -c.drop});
        above.part = t.part;
        return sequence({Program(at_once(object, std::move(above))), drop(part(object, t.part), {0, c.drop}, {.duration = t.duration})});
    }
    if (c.kind == PresenceKind::enter) return enter(object, t, targets_of(c.away), back_of(c.away));
    return leave(object, t, targets_of(c.away));
}

}  // namespace

// ── Entrances ── (hanim.core's courses: their poses, paces and fades are defined there)

Program fade_in(ObjectInput object, RecipeOptions options) { return play_presence(object, "fade_in", {}, options); }
Program rise_in(ObjectInput object, double distance, RecipeOptions options) { return play_presence(object, "rise_in", {.distance = distance}, options); }
Program sink_in(ObjectInput object, double distance, RecipeOptions options) { return play_presence(object, "sink_in", {.distance = distance}, options); }
Program slide_in(ObjectInput object, Vec2 from, RecipeOptions options) { return play_presence(object, "slide_in", {.offset = from}, options); }
Program settle_in(ObjectInput object, double scale, RecipeOptions options) { return play_presence(object, "settle_in", {.scale = scale}, options); }
Program pop_in(ObjectInput object, RecipeOptions options) { return play_presence(object, "pop_in", {}, options); }
Program zoom_in(ObjectInput object, RecipeOptions options) { return play_presence(object, "zoom_in", {}, options); }
Program spin_in(ObjectInput object, double radians, RecipeOptions options) { return play_presence(object, "spin_in", {.radians = radians}, options); }
Program unfold_in(ObjectInput object, RecipeOptions options) { return play_presence(object, "unfold_in", {}, options); }
Program write_in(ObjectInput object, RecipeOptions options) { return play_presence(object, "write_in", {}, options); }
Program drop_in(ObjectInput object, double height, RecipeOptions options) { return play_presence(object, "drop_in", {.height = height}, options); }

// ── Exits ──

Program fade_out(ObjectInput object, RecipeOptions options) { return play_presence(object, "fade_out", {}, options); }
Program rise_out(ObjectInput object, double distance, RecipeOptions options) { return play_presence(object, "rise_out", {.distance = distance}, options); }
Program sink_out(ObjectInput object, double distance, RecipeOptions options) { return play_presence(object, "sink_out", {.distance = distance}, options); }
Program slide_out(ObjectInput object, Vec2 to_offset, RecipeOptions options) { return play_presence(object, "slide_out", {.offset = to_offset}, options); }
Program settle_out(ObjectInput object, double scale, RecipeOptions options) { return play_presence(object, "settle_out", {.scale = scale}, options); }
Program pop_out(ObjectInput object, RecipeOptions options) { return play_presence(object, "pop_out", {}, options); }
Program zoom_out(ObjectInput object, RecipeOptions options) { return play_presence(object, "zoom_out", {}, options); }
Program spin_out(ObjectInput object, double radians, RecipeOptions options) { return play_presence(object, "spin_out", {.radians = radians}, options); }
Program fold_out(ObjectInput object, RecipeOptions options) { return play_presence(object, "fold_out", {}, options); }
Program unwrite(ObjectInput object, RecipeOptions options) { return play_presence(object, "unwrite", {}, options); }

// ── Attention ──

Program bounce(ObjectInput object, double height, RecipeOptions options) {
    auto t = timing(options, .8, ease::linear, 4);
    if (!t) return invalid_program(t.error());
    if (!finite(height)) return bad_value();
    // Two hops, the second a third as high: up decelerating, down accelerating, as thrown.
    const double d = t->duration.count();
    const auto leg = [&](double dy, double share, Ease e) {
        PropertyTargets p = offset({0, dy});
        p.part = t->part;
        return Program(to(object, std::move(p)).duration(seconds(d * share)).easing(e));
    };
    return sequence({leg(-height, .3, ease::out_quad), leg(height, .3, ease::in_quad), leg(-height / 3, .2, ease::out_quad), leg(height / 3, .2, ease::in_quad)});
}
Program flash(ObjectInput object, int times, RecipeOptions options) {
    if (times < 1 || times > 100) return bad_value();
    auto t = timing(options, .3 * times, ease::in_out_sine, 2.0 * times);
    if (!t) return invalid_program(t.error());
    std::vector<Program> legs;
    const seconds half = t->duration / (2.0 * times);
    for (int k = 0; k < times; ++k) {
        legs.push_back(to(object, {.offset = Vec2{0, 0}, .part = t->part}).duration(half).easing(t->easing).with_effect(fx::opacity(1, 0)));
        legs.push_back(to(object, {.offset = Vec2{0, 0}, .part = t->part}).duration(half).easing(t->easing).with_effect(fx::opacity(0, 1)));
    }
    return sequence(std::span<const Program>(legs));
}
Program heartbeat(ObjectInput object, double factor, RecipeOptions options) {
    auto t = timing(options, .9, ease::in_out_sine, 6);
    if (!t) return invalid_program(t.error());
    if (!invertible(factor)) return bad_value();
    // Two beats, the second softer, then a rest before the next.
    const double d = t->duration.count(), second = 1 + (factor - 1) * .7;
    const auto beat = [&](double by, double share) {
        return Program(to(object, {.part = t->part, .scale_factor = Vec2{by, by}}).duration(seconds(d * share)).easing(t->easing));
    };
    return sequence({beat(factor, .14), beat(1 / factor, .14), beat(second, .14), beat(1 / second, .28), wait(seconds(d * .3))});
}
Program rubber_band(ObjectInput object, RecipeOptions options) {
    auto t = timing(options, .8, ease::in_out_sine, 4);
    if (!t) return invalid_program(t.error());
    // Its pose's scale through (1.25, .75), (.75, 1.25), (1.15, .85) and back, each step relative to the last.
    const std::array<Vec2, 4> poses{Vec2{1.25, .75}, Vec2{.75, 1.25}, Vec2{1.15, .85}, Vec2{1, 1}};
    const std::array<double, 4> shares{.3, .25, .2, .25};
    std::vector<Program> legs;
    Vec2 was{1, 1};
    for (std::size_t k = 0; k < poses.size(); ++k) {
        legs.push_back(to(object, {.part = t->part, .scale_factor = Vec2{poses[k].x / was.x, poses[k].y / was.y}})
                           .duration(t->duration * shares[k])
                           .easing(t->easing));
        was = poses[k];
    }
    return sequence(std::span<const Program>(legs));
}
Program swing(ObjectInput object, double radians, RecipeOptions options) {
    auto t = timing(options, 1, ease::in_out_sine, 5);
    if (!t) return invalid_program(t.error());
    if (!finite(radians)) return bad_value();
    // To r, -0.66r, 0.4r, -0.2r and rest: each swing less.
    const std::array<double, 5> angles{radians, -.66 * radians, .4 * radians, -.2 * radians, 0};
    std::vector<Program> legs;
    double was = 0;
    for (const double angle : angles) {
        legs.push_back(to(object, {.part = t->part, .rotation_offset = angle - was}).duration(t->duration / 5.0).easing(t->easing));
        was = angle;
    }
    return sequence(std::span<const Program>(legs));
}
Program tada(ObjectInput object, RecipeOptions options) {
    auto t = timing(options, 1, ease::in_out_sine, 7);
    if (!t) return invalid_program(t.error());
    // Shrinks a little turning, grows shaking from side to side, and comes back.
    const double r = .05;
    const seconds step = t->duration / 7.0;
    const auto leg = [&](PropertyTargets p) {
        p.part = t->part;
        return Program(to(object, std::move(p)).duration(step).easing(t->easing));
    };
    return sequence({leg({.rotation_offset = -r, .scale_factor = Vec2{.9, .9}}), leg({.rotation_offset = 2 * r, .scale_factor = Vec2{1.1 / .9, 1.1 / .9}}),
                     leg(turned(-2 * r)), leg(turned(2 * r)), leg(turned(-2 * r)), leg(turned(2 * r)),
                     leg({.rotation_offset = -r, .scale_factor = Vec2{1 / 1.1, 1 / 1.1}})});
}
Program nudge(ObjectInput object, Vec2 by, RecipeOptions options) {
    auto t = timing(options, .5, ease::out_quad, 2);
    if (!t) return invalid_program(t.error());
    if (!finite(by)) return bad_value();
    return sequence({Program(to(object, {.offset = by, .part = t->part}).duration(t->duration * .4).easing(t->easing)),
                     Program(to(object, {.offset = Vec2{-by.x, -by.y}, .part = t->part}).duration(t->duration * .6).easing(ease::in_out_sine))});
}

// ── Idle loops ──

namespace {
std::optional<ProgramFailure> invalid_loop(seconds period, double count) {
    if (!std::isfinite(period.count()) || period.count() <= 0) return ProgramFailure{ProgramError::invalid_time};
    if (!(count >= 1)) return ProgramFailure{ProgramError::invalid_time};
    return {};
}
}  // namespace
Program bob(ObjectInput object, double amplitude, seconds period, double count, RecipeOptions options) {
    if (auto e = invalid_loop(period, count)) return invalid_program(*e);
    auto t = timing({.duration = period, .easing = options.easing, .part = options.part}, period.count(), ease::in_out_sine, 2);
    if (!t) return invalid_program(t.error());
    if (!finite(amplitude)) return bad_value();
    return loop(sequence({Program(to(object, {.offset = Vec2{0, -amplitude}, .part = t->part}).duration(period / 2.0).easing(t->easing)),
                          Program(to(object, {.offset = Vec2{0, amplitude}, .part = t->part}).duration(period / 2.0).easing(t->easing))}),
                count);
}
Program breathe(ObjectInput object, double factor, seconds period, double count, RecipeOptions options) {
    if (auto e = invalid_loop(period, count)) return invalid_program(*e);
    auto t = timing({.duration = period, .easing = options.easing, .part = options.part}, period.count(), ease::in_out_sine, 2);
    if (!t) return invalid_program(t.error());
    if (!invertible(factor)) return bad_value();
    return loop(sequence({Program(to(object, {.part = t->part, .scale_factor = Vec2{factor, factor}}).duration(period / 2.0).easing(t->easing)),
                          Program(to(object, {.part = t->part, .scale_factor = Vec2{1 / factor, 1 / factor}}).duration(period / 2.0).easing(t->easing))}),
                count);
}
Program spin(ObjectInput object, seconds period, double count, RecipeOptions options) {
    if (auto e = invalid_loop(period, count)) return invalid_program(*e);
    auto t = timing({.duration = period, .easing = options.easing, .part = options.part}, period.count(), ease::linear);
    if (!t) return invalid_program(t.error());
    return loop(Program(to(object, {.part = t->part, .rotation_offset = two_pi}).duration(period).easing(t->easing)), count);
}
Program sway(ObjectInput object, double radians, seconds period, double count, RecipeOptions options) {
    if (auto e = invalid_loop(period, count)) return invalid_program(*e);
    auto t = timing({.duration = period, .easing = options.easing, .part = options.part}, period.count(), ease::in_out_sine, 4);
    if (!t) return invalid_program(t.error());
    if (!finite(radians)) return bad_value();
    const auto leg = [&](double by, double share) {
        return Program(to(object, {.part = t->part, .rotation_offset = by}).duration(period * share).easing(t->easing));
    };
    return loop(sequence({leg(radians, .25), leg(-2 * radians, .5), leg(radians, .25)}), count);
}
Program orbit(ObjectInput object, double radius, seconds period, double count, RecipeOptions options) {
    if (auto e = invalid_loop(period, count)) return invalid_program(*e);
    auto t = timing({.duration = period, .easing = options.easing, .part = options.part}, period.count(), ease::linear);
    if (!t) return invalid_program(t.error());
    if (!finite(radius) || radius <= 0) return bad_value();
    // A circle through where it is, about the point `radius` above it, clockwise on the screen.
    const double k = radius * .5522847498307936, r = radius;
    MotionPath circle;
    circle.move_to({0, 0}).cubic_to({k, 0}, {r, -r + k}, {r, -r}).cubic_to({r, -r - k}, {k, -2 * r}, {0, -2 * r})
        .cubic_to({-k, -2 * r}, {-r, -r - k}, {-r, -r}).cubic_to({-r, -r + k}, {-k, 0}, {0, 0});
    return loop(Program(follow_path(object, std::move(circle), {.part = t->part}).duration(period).easing(t->easing)), count);
}
Program blink(ObjectInput object, seconds period, double count, RecipeOptions options) {
    if (auto e = invalid_loop(period, count)) return invalid_program(*e);
    auto t = timing({.duration = period, .easing = options.easing, .part = options.part}, period.count(), ease::in_out_sine, 2);
    if (!t) return invalid_program(t.error());
    return loop(sequence({Program(to(object, {.offset = Vec2{0, 0}, .part = t->part}).duration(period / 2.0).easing(t->easing).with_effect(fx::opacity(1, 0))),
                          Program(to(object, {.offset = Vec2{0, 0}, .part = t->part}).duration(period / 2.0).easing(t->easing).with_effect(fx::opacity(0, 1)))}),
                count);
}

// ── Moves ──

Program arc_to(ObjectInput object, Vec2 target, double bend, RecipeOptions options) {
    auto t = timing(options, .6, ease::in_out_cubic);
    if (!t) return invalid_program(t.error());
    if (!finite(target) || !finite(bend)) return bad_value();
    return to(object, {.position = target, .part = t->part}).duration(t->duration).easing(t->easing).with_effect(fx::translation_path(PointPath::bend(bend)));
}
Program swap(ObjectInput a, ObjectInput b, Vec2 a_to_b, double bend, RecipeOptions options) {
    auto t = timing(options, .6, ease::in_out_cubic);
    if (!t) return invalid_program(t.error());
    if (!finite(a_to_b) || !finite(bend)) return bad_value();
    // Both bend the same way along their own way: they pass each other on either side.
    return parallel({Program(to(a, {.offset = a_to_b, .part = t->part}).duration(t->duration).easing(t->easing).with_effect(fx::translation_path(PointPath::bend(bend)))),
                     Program(to(b, {.offset = Vec2{-a_to_b.x, -a_to_b.y}, .part = t->part}).duration(t->duration).easing(t->easing)
                                 .with_effect(fx::translation_path(PointPath::bend(bend))))});
}
Program reorder(std::span<const ObjectInput> objects, std::span<const Vec2> offsets, seconds step, RecipeOptions options) {
    auto t = timing(options, .5, ease::in_out_cubic);
    if (!t) return invalid_program(t.error());
    if (objects.empty() || objects.size() != offsets.size() || !std::isfinite(step.count()) || step.count() < 0) return bad_value();
    std::vector<Program> moves;
    for (std::size_t k = 0; k < objects.size(); ++k) {
        if (!finite(offsets[k])) return bad_value();
        moves.push_back(to(objects[k], {.offset = offsets[k], .part = t->part}).duration(t->duration).easing(t->easing).with_effect(fx::translation_path(PointPath::bend(.15))));
    }
    return stagger(step, std::span<const Program>(moves));
}

// ── Pixels ──

namespace {
// One pixel's flight: its part, where it goes (relative), how far it turns,
// when it starts (a fraction of the spread: 0 the nearest, 1 the farthest), its ink.
struct Flight {
    std::string id;
    Vec2        away;
    double      turn = 0;
    double      reach = 0;
    Rgba        color;
};
std::expected<std::vector<Flight>, Program> flights(const PixelGrid& grid, const ScatterOptions& options) {
    if (!finite(options.distance) || options.distance < 0 || !finite(options.spin) || !std::isfinite(options.spread) || options.spread < 0 ||
        options.spread >= 1 || (options.from && !finite(*options.from)) || !(std::isfinite(grid.cell) && grid.cell > 0))
        return std::unexpected(bad_value());
    const auto cells = pixel_cells(grid);
    if (!cells) return std::unexpected(bad_value());
    const auto centre_of = [&](const PixelCell& cell) { return Vec2{(cell.x + .5) * grid.cell, (cell.y + .5) * grid.cell}; };
    Vec2 from{};
    if (options.from) from = *options.from;
    else if (!cells->empty()) {
        Vec2 low{1e300, 1e300}, high{-1e300, -1e300};
        for (const PixelCell& cell : *cells) {
            const Vec2 c = centre_of(cell);
            low = {std::min(low.x, c.x), std::min(low.y, c.y)};
            high = {std::max(high.x, c.x), std::max(high.y, c.y)};
        }
        from = {(low.x + high.x) / 2, (low.y + high.y) / 2};
    }
    // The reach: 0 the nearest, 1 the farthest — so the whole flight takes the duration asked for.
    double nearest = 1e300, farthest = 0;
    for (const PixelCell& cell : *cells) {
        const double across = centre_of(cell).x - from.x, down = centre_of(cell).y - from.y, distance = dm::sqrt(across * across + down * down);
        nearest = std::min(nearest, distance);
        farthest = std::max(farthest, distance);
    }
    std::vector<Flight> out;
    out.reserve(cells->size());
    for (const PixelCell& cell : *cells) {
        // Three numbers of the pixel's own, from the seed and where it is.
        const std::uint64_t key = mlibs::math::mix64(options.seed ^ mlibs::math::mix64((static_cast<std::uint64_t>(cell.y) << 32) | static_cast<std::uint32_t>(cell.x)));
        const double wander = mlibs::math::unit24(key), length = mlibs::math::unit24(mlibs::math::mix64(key)),
                     twist = mlibs::math::unit24(mlibs::math::mix64(key ^ mlibs::math::golden));
        const Vec2 c = centre_of(cell);
        const double dx = c.x - from.x, dy = c.y - from.y, near = dm::sqrt(dx * dx + dy * dy);
        const double heading = (near > 1e-9 ? dm::atan2(dy, dx) : wander * two_pi) + (wander - .5) * .9;
        const double far = options.distance * (.6 + .8 * length);
        out.push_back({pixel_id(cell.x, cell.y), {dm::cos(heading) * far, dm::sin(heading) * far}, options.spin * (2 * twist - 1),
                       farthest > nearest ? (near - nearest) / (farthest - nearest) : 0, grid.inks[cell.ink].color});
    }
    return out;
}
Rgba clear(Rgba color) { return {color.r, color.g, color.b, 0}; }
}  // namespace

Program scatter_out(ObjectInput object, const PixelGrid& grid, ScatterOptions options, RecipeOptions recipe) {
    auto t = timing(recipe, .9, ease::out_cubic, 2);
    if (!t) return invalid_program(t.error());
    auto made = flights(grid, options);
    if (!made) return made.error();
    const seconds flight = t->duration * (1 - options.spread);
    std::vector<Program> each;
    each.reserve(made->size());
    for (const Flight& f : *made)
        each.push_back(delay(t->duration * (options.spread * f.reach),
                             to(object, {.offset = f.away, .fill = clear(f.color), .part = f.id, .rotation_offset = f.turn}).duration(flight).easing(t->easing)));
    return parallel(std::span<const Program>(each));
}

Program assemble_in(ObjectInput object, const PixelGrid& grid, ScatterOptions options, RecipeOptions recipe) {
    auto t = timing(recipe, .9, ease::out_cubic, 2);
    if (!t) return invalid_program(t.error());
    auto made = flights(grid, options);
    if (!made) return made.error();
    const seconds flight = t->duration * (1 - options.spread);
    PropertySet away;
    std::vector<Program> back;
    away.reserve(made->size());
    back.reserve(made->size());
    for (const Flight& f : *made) {
        away.push_back({.offset = f.away, .fill = clear(f.color), .part = f.id, .rotation_offset = f.turn});
        back.push_back(delay(t->duration * (options.spread * (1 - f.reach)),
                             to(object, {.offset = Vec2{-f.away.x, -f.away.y}, .fill = f.color, .part = f.id, .rotation_offset = -f.turn})
                                 .duration(flight)
                                 .easing(t->easing)));
    }
    return sequence({Program(to(object, std::move(away)).duration(seconds(0))), parallel(std::span<const Program>(back))});
}

}  // namespace hanim::anim
