// Immutable binding expressions and prepared per-object schedules, no clock.
export module hanim.program;
import std;
import mlibs.error;
export import hanim.library;
export import hanim.effect;

namespace hanim::detail { struct ProgramNode; struct ProgramData; }
export namespace hanim {
inline constexpr std::uint32_t program_contract_version = 8;
inline constexpr std::size_t invalid_program_index = std::numeric_limits<std::size_t>::max();
struct SegmentOptions { seconds duration{1}; Ease easing = ease::linear; };
// The first binding always uses its frozen source. Subsequent default bindings
// of that logical ObjectRef use the preceding segment's actual boundary pose.
enum class ProgramSource : std::uint8_t { previous_endpoint, snapshot };
enum class ProgramKind : std::uint8_t { empty, binding, sequence, parallel, stagger, composite, invalid, wait, repeat, timed, marker };
struct CompositeOptions { Ease effect_easing = ease::linear; };
struct CompositeSpec { Effects effects; CompositeOptions options; };
struct ProgramBinding {
    Snapshot from, to;
    Effects effects;
    SegmentOptions options;
    MorphOptions morph;
    ProgramSource source = ProgramSource::previous_endpoint;
    std::optional<PropertyTargets> properties;
    PropertySet additional_properties;
};
// Live inputs resolve together at preparation; explicit snapshots retain their
// captured revision. Neither form owns or mutates the application's registry.
using ObjectInput = std::variant<Snapshot, ObjectRef>;
struct ProgramInputs { ObjectInput from, to; };
enum class ProgramError : std::uint8_t {
    unprepared, invalid_limits, empty_program, invalid_snapshot, invalid_source,
    invalid_time, invalid_ease, node_budget, depth_budget, track_budget,
    segment_budget, part_budget, cubic_budget, effect_parameter_budget,
    source_version_mismatch, binding_conflict, animation, effect,
    invalid_track, output_too_small, unresolved_object, object, scope_budget,
    channel_budget, scope_conflict, invalid_repeat, route_budget, nested_time_map
};
// The codes as the error contract describes them (mlibs.error).
[[nodiscard]] constexpr mlibs::error::Description describe(ProgramError e) noexcept {
    using Kind = mlibs::error::Kind;  // not a using-declaration: that is an ICE in GCC 16
    switch (e) {
        case ProgramError::unprepared: return {"hanim.program.unprepared", Kind::invalid_argument};
        case ProgramError::invalid_limits: return {"hanim.program.invalid-limits", Kind::invalid_argument};
        case ProgramError::empty_program: return {"hanim.program.empty-program", Kind::invalid_argument};
        case ProgramError::invalid_snapshot: return {"hanim.program.invalid-snapshot", Kind::invalid_argument};
        case ProgramError::invalid_source: return {"hanim.program.invalid-source", Kind::invalid_argument};
        case ProgramError::invalid_time: return {"hanim.program.invalid-time", Kind::invalid_argument};
        case ProgramError::invalid_ease: return {"hanim.program.invalid-ease", Kind::invalid_argument};
        case ProgramError::node_budget: return {"hanim.program.node-budget", Kind::refused};
        case ProgramError::depth_budget: return {"hanim.program.depth-budget", Kind::refused};
        case ProgramError::track_budget: return {"hanim.program.track-budget", Kind::refused};
        case ProgramError::segment_budget: return {"hanim.program.segment-budget", Kind::refused};
        case ProgramError::part_budget: return {"hanim.program.part-budget", Kind::refused};
        case ProgramError::cubic_budget: return {"hanim.program.cubic-budget", Kind::refused};
        case ProgramError::effect_parameter_budget: return {"hanim.program.effect-parameter-budget", Kind::refused};
        case ProgramError::source_version_mismatch: return {"hanim.program.source-version-mismatch", Kind::invalid_argument};
        case ProgramError::binding_conflict: return {"hanim.program.binding-conflict", Kind::refused};
        case ProgramError::animation: return {"hanim.program.animation", Kind::failed, {}, true};
        case ProgramError::effect: return {"hanim.program.effect", Kind::failed, {}, true};
        case ProgramError::invalid_track: return {"hanim.program.invalid-track", Kind::invalid_argument};
        case ProgramError::output_too_small: return {"hanim.program.output-too-small", Kind::invalid_argument};
        case ProgramError::unresolved_object: return {"hanim.program.unresolved-object", Kind::not_found};
        case ProgramError::object: return {"hanim.program.object", Kind::failed, {}, true};
        case ProgramError::scope_budget: return {"hanim.program.scope-budget", Kind::refused};
        case ProgramError::channel_budget: return {"hanim.program.channel-budget", Kind::refused};
        case ProgramError::scope_conflict: return {"hanim.program.scope-conflict", Kind::refused};
        case ProgramError::invalid_repeat: return {"hanim.program.invalid-repeat", Kind::invalid_argument};
        case ProgramError::route_budget: return {"hanim.program.route-budget", Kind::refused};
        case ProgramError::nested_time_map: return {"hanim.program.nested-time-map", Kind::refused, "a time map maps a whole program, for now"};
    }
    return {"hanim.program.?", Kind::internal};
}
// Where in a program an error is: its node, track, segment and scope, each
// when it applies. What failed beneath (an animation, an effect, an object)
// is a cause.
struct ProgramWhere {
    std::size_t node = invalid_program_index, track = invalid_program_index, segment = invalid_program_index;
    std::size_t scope = invalid_program_index;
};
[[nodiscard]] std::string describe_where(const ProgramWhere& where);
using ProgramFailure = mlibs::error::Error<ProgramError, ProgramWhere>;
class Program {
public:
    Program() noexcept = default;
    [[nodiscard]] explicit operator bool() const noexcept { return bool(node_); }
    [[nodiscard]] ProgramKind kind() const noexcept;
    [[nodiscard]] std::span<const Program> children() const & noexcept;
    std::span<const Program> children() const && = delete;
    [[nodiscard]] const ProgramBinding* binding() const & noexcept;
    const ProgramBinding* binding() const && = delete;
    // Non-null for deferred authoring. binding() still supplies timing/effects;
    // its from/to stay empty; PreparedProgram owns the resolved snapshots.
    [[nodiscard]] const ProgramInputs* inputs() const & noexcept;
    const ProgramInputs* inputs() const && = delete;
    [[nodiscard]] const CompositeSpec* scope() const & noexcept;
    const CompositeSpec* scope() const && = delete;
    [[nodiscard]] seconds step() const noexcept;
    // Nonzero only for an explicit wait node; never a blocking host sleep.
    [[nodiscard]] seconds wait_duration() const noexcept;
    [[nodiscard]] std::size_t repeat_count() const noexcept;
    // Non-null for a time-mapped node (timed, loop).
    [[nodiscard]] const TimeMap* time_map() const & noexcept;
    const TimeMap* time_map() const && = delete;
    // A marker node's name (empty for every other kind).
    [[nodiscard]] std::string_view marker_name() const & noexcept;
    std::string_view marker_name() const && = delete;
    // Non-null only for an explicitly rejected authoring expression. Its
    // domain cause survives composition; preparation supplies the node index.
    [[nodiscard]] const ProgramFailure* failure() const & noexcept;
    const ProgramFailure* failure() const && = delete;

    // Time, on the expression itself (L4): `sequence(a, wait(100ms), b).loop(3).alternate()`.
    // loop replays the course (forever by default); alternate and reverse
    // turn a loop's courses (or this one) backwards; rate scales its time.
    // Each gives a new program; this one is unchanged (delay(d, p) waits first).
    [[nodiscard]] Program loop(double times = std::numeric_limits<double>::infinity()) const;
    [[nodiscard]] Program alternate() const;
    [[nodiscard]] Program reverse() const;
    [[nodiscard]] Program rate(double rate) const;
private:
    friend Program invalid_program(ProgramFailure);
    friend Program bind(Snapshot, Snapshot, Effects, SegmentOptions, MorphOptions, ProgramSource);
    friend Program bind(ProgramInputs, Effects, SegmentOptions, MorphOptions, ProgramSource, std::optional<PropertyTargets>, PropertySet);
    friend Program wait(seconds);
    friend Program repeat(std::size_t, Program);
    friend Program timed(Program, TimeMap);
    friend Program marker(std::string);
    friend Program sequence(std::span<const Program>);
    friend Program parallel(std::span<const Program>);
    friend Program stagger(seconds, std::span<const Program>);
    friend Program composite(Program, Effects, CompositeOptions);
    explicit Program(std::shared_ptr<const detail::ProgramNode> node) noexcept : node_(std::move(node)) {}
    std::shared_ptr<const detail::ProgramNode> node_;
};
[[nodiscard]] Program invalid_program(ProgramFailure);
[[nodiscard]] Program bind(Snapshot from, Snapshot to, Effects effects = {}, SegmentOptions options = {},
                           MorphOptions morph = {}, ProgramSource source = ProgramSource::previous_endpoint);
[[nodiscard]] Program bind(ProgramInputs inputs, Effects effects = {}, SegmentOptions options = {},
                           MorphOptions morph = {}, ProgramSource source = ProgramSource::previous_endpoint,
                           std::optional<PropertyTargets> properties = {}, PropertySet additional_properties = {});
// A pure timeline interval: no object, channel, callback or host clock.
[[nodiscard]] Program wait(seconds duration);
// Finite serial repetitions. The immutable descriptor retains only one child.
// Each is prepared from where the last ended (a relative move goes on).
[[nodiscard]] Program repeat(std::size_t count, Program child);
// Time mapping (L4): the child prepared once and played through hanim.core's
// TimeMap — rate, trim, repeats (fractional or forever), alternate, reverse,
// fill. A repeat here replays the same course (a relative move starts over).
// Inside a program whole courses — an integral repeat, alternate, reverse —
// unroll into replicas of the first; rate, trim, a length, fractional or
// endless repeats and other fills map a whole program only
// (ProgramError::nested_time_map).
[[nodiscard]] Program timed(Program child, TimeMap map);
// `times` plays (forever by default), every other one backwards when `alternate`.
[[nodiscard]] inline Program loop(Program child, double times = std::numeric_limits<double>::infinity(), bool alternate = false) {
    return timed(std::move(child), TimeMap{.repeat = times, .alternate = alternate});
}
// A named instant (a beat, a cue): no object, no duration; PreparedProgram::markers().
[[nodiscard]] Program marker(std::string name);
[[nodiscard]] Program sequence(std::span<const Program> children);
[[nodiscard]] Program sequence(std::initializer_list<Program> children);
[[nodiscard]] inline Program delay(seconds duration, Program child) {
    return sequence({wait(duration), std::move(child)});
}
[[nodiscard]] Program parallel(std::span<const Program> children);
[[nodiscard]] Program parallel(std::initializer_list<Program> children);
[[nodiscard]] Program stagger(seconds step, std::span<const Program> children);
[[nodiscard]] Program stagger(seconds step, std::initializer_list<Program> children);
[[nodiscard]] Program composite(Program child, Effects effects, CompositeOptions options = {});

namespace anim {
// A reusable leaf value. Process effects are distinct from image scopes.
class BoundAnimation {
public:
    BoundAnimation(ObjectInput from, ObjectInput to, MorphOptions options = {})
        : inputs_{std::move(from), std::move(to)} { binding_.morph = options; }
    // Constrained deduction keeps BoundAnimation(source, {}) an empty ObjectInput.
    template<class Values> requires std::same_as<std::remove_cvref_t<Values>, PropertyTargets>
    BoundAnimation(ObjectInput object, Values&& values, MorphOptions options = {})
        : inputs_{object, std::move(object)} { binding_.morph = options; binding_.properties = std::forward<Values>(values); }
    template<class Targets> requires std::same_as<std::remove_cvref_t<Targets>, PropertySet>
    BoundAnimation(ObjectInput object, Targets&& values, MorphOptions options = {})
        : inputs_{object, std::move(object)} {
        binding_.morph=options;
        // An empty property set is still a property animation, refused by
        // preparation rather than accidentally interpreted as a morph.
        binding_.properties=values.empty()?PropertyTargets{}:values.front();
        if(!values.empty())binding_.additional_properties.assign(values.begin()+1,values.end());
    }
    [[nodiscard]] BoundAnimation duration(seconds value) const {
        auto result = *this; result.binding_.options.duration = value; return result;
    }
    [[nodiscard]] BoundAnimation easing(Ease value) const {
        auto result = *this; result.binding_.options.easing = value; return result;
    }
    // A layer over its channels' base (Blend): `breathe.blend(Blend::add)`.
    [[nodiscard]] BoundAnimation blend(Blend mode, double weight = 1) const {
        auto result = *this;
        if (result.binding_.properties) { result.binding_.properties->blend = mode; result.binding_.properties->weight = weight; }
        for (auto& targets : result.binding_.additional_properties) { targets.blend = mode; targets.weight = weight; }
        return result;
    }
    // A spring's course (L1): its curve, for as long as it takes to settle.
    [[nodiscard]] BoundAnimation spring(SpringParams params) const {
        auto result = *this;
        result.binding_.options.easing = spring_ease(params);
        result.binding_.options.duration = seconds(result.binding_.options.easing.settle());
        return result;
    }
    [[nodiscard]] BoundAnimation with_effect(EffectSpec value) const {
        auto result = *this; result.binding_.effects.push_back(std::move(value)); return result;
    }
    [[nodiscard]] BoundAnimation with_effects(std::span<const EffectSpec> values) const {
        auto result = *this; result.binding_.effects.insert(result.binding_.effects.end(), values.begin(), values.end()); return result;
    }
    // Duration and easing at once (an engine's PlayOptions).
    [[nodiscard]] BoundAnimation options(SegmentOptions value) const {
        auto result = *this; result.binding_.options = value; return result;
    }
    // Deduction keeps legacy with_effect({}) unambiguous (EffectSpec).
    template<class Process> requires (std::same_as<Process, fx::VertexPath> || std::same_as<Process, fx::TranslationPath>)
    [[nodiscard]] BoundAnimation with_effect(Process value) const {
        auto result = *this;
        if constexpr (std::same_as<Process, fx::VertexPath>)
            result.binding_.morph.shape.point_path = result.binding_.morph.shape.point_path.added(value.path);
        else result.binding_.morph.translation_path = result.binding_.morph.translation_path.added(value.path);
        return result;
    }
    [[nodiscard]] BoundAnimation source(ProgramSource value) const {
        auto result = *this; result.binding_.source = value; return result;
    }
    [[nodiscard]] const ProgramBinding& binding() const & noexcept { return binding_; }
    const ProgramBinding& binding() const && = delete;
    [[nodiscard]] operator Program() const {
        return bind(inputs_, binding_.effects, binding_.options, binding_.morph, binding_.source, binding_.properties, binding_.additional_properties);
    }
private:
    ProgramInputs inputs_;
    ProgramBinding binding_;
};
[[nodiscard]] inline BoundAnimation morph(ObjectInput from, ObjectInput to, MorphOptions options = {}) {
    return BoundAnimation{std::move(from), std::move(to), options};
}
[[nodiscard]] inline BoundAnimation to(ObjectInput object, PropertyTargets values, MorphOptions options = {}) {
    return BoundAnimation{std::move(object), std::move(values), options};
}
template<class Targets> requires std::same_as<std::remove_cvref_t<Targets>, PropertySet>
[[nodiscard]] BoundAnimation to(ObjectInput object, Targets&& values, MorphOptions options = {}) {
    return BoundAnimation{std::move(object),std::forward<Targets>(values),options};
}
[[nodiscard]] inline BoundAnimation move_to(ObjectInput object, Vec2 position) {
    return to(std::move(object), {.position=position});
}
[[nodiscard]] inline BoundAnimation move_by(ObjectInput object, Vec2 offset) {
    return to(std::move(object), {.offset=offset});
}
[[nodiscard]] inline BoundAnimation rotate_to(ObjectInput object, double radians) {
    return to(std::move(object), {.rotation=radians});
}
[[nodiscard]] inline BoundAnimation rotate_by(ObjectInput object, double radians) {
    return to(std::move(object), {.rotation_offset=radians});
}
[[nodiscard]] inline BoundAnimation scale_to(ObjectInput object, Vec2 scale) {
    return to(std::move(object), {.scale=scale});
}
[[nodiscard]] inline BoundAnimation scale_by(ObjectInput object, Vec2 factor) {
    return to(std::move(object), {.scale_factor=factor});
}
[[nodiscard]] inline BoundAnimation fill_to(ObjectInput object, Rgba fill) {
    return to(std::move(object), {.fill=fill});
}
// Typed writes (L2): `to(card, ch::position = {120, 40}, glow = 1.0)` sets
// channels; `by(card, ch::rotation = .5)` moves them from where they are
// (position and value channels add, scale multiplies). `part(card, "icon")`
// targets one part.
struct PartTarget { ObjectInput object; std::string part; };
namespace writes {
template <class T>
void lower(PropertyTargets& t, const ch::Write<T>& w, bool relative) {
    const ch::Id& id = w.channel.id;
    if constexpr (std::same_as<T, Vec2>) {
        if (id == ch::position.id) { (relative ? t.offset : t.position) = w.value; return; }
        if (id == ch::scale.id) { (relative ? t.scale_factor : t.scale) = w.value; return; }
    }
    if constexpr (std::same_as<T, double>)
        if (id == ch::rotation.id) { (relative ? t.rotation_offset : t.rotation) = w.value; return; }
    if constexpr (std::same_as<T, Rgba>)
        if (id == ch::fill.id && !relative) { t.fill = w.value; return; }
    // Any other channel (a relative fill too: preparation refuses it).
    t.values.push_back(w.channel.value(w.value, relative));
}
template <class... T>
[[nodiscard]] PropertyTargets targets(std::string part, bool relative, const ch::Write<T>&... w) {
    PropertyTargets t;
    t.part = std::move(part);
    (lower(t, w, relative), ...);
    return t;
}
}  // namespace writes
template <class... T> requires (sizeof...(T) > 0)
[[nodiscard]] BoundAnimation to(ObjectInput object, const ch::Write<T>&... w) {
    return BoundAnimation{std::move(object), writes::targets({}, false, w...)};
}
template <class... T> requires (sizeof...(T) > 0)
[[nodiscard]] BoundAnimation to(PartTarget target, const ch::Write<T>&... w) {
    return BoundAnimation{std::move(target.object), writes::targets(std::move(target.part), false, w...)};
}
template <class... T> requires (sizeof...(T) > 0)
[[nodiscard]] BoundAnimation by(ObjectInput object, const ch::Write<T>&... w) {
    return BoundAnimation{std::move(object), writes::targets({}, true, w...)};
}
template <class... T> requires (sizeof...(T) > 0)
[[nodiscard]] BoundAnimation by(PartTarget target, const ch::Write<T>&... w) {
    return BoundAnimation{std::move(target.object), writes::targets(std::move(target.part), true, w...)};
}
// `spring(card, springs::snappy, ch::position = {0, 0})`: to(…), at a spring's pace.
template <class... T> requires (sizeof...(T) > 0)
[[nodiscard]] BoundAnimation spring(ObjectInput object, SpringParams params, const ch::Write<T>&... w) {
    return to(std::move(object), w...).spring(params);
}
template <class... T> requires (sizeof...(T) > 0)
[[nodiscard]] BoundAnimation spring(PartTarget target, SpringParams params, const ch::Write<T>&... w) {
    return to(std::move(target), w...).spring(params);
}
struct FollowPathOptions { std::string part; };
[[nodiscard]] inline BoundAnimation follow_path(ObjectInput object, TranslationRoute route, FollowPathOptions options = {}) {
    return to(std::move(object),{.part=std::move(options.part),.route=std::move(route)});
}
[[nodiscard]] inline BoundAnimation follow_path(ObjectInput object, MotionPath path, FollowPathOptions options = {}) {
    return follow_path(std::move(object),TranslationRoute(std::move(path)),std::move(options));
}
// The recipe library (L6, plan §6.2): reusable animations that lower to
// ordinary programs — relative property writes from the actual pose, the
// group opacity effect, the stroke's trim and the squash channels — so any
// object (or one part of it) takes them from wherever it is, in any
// composition. No clock, callback or separate runtime. Every recipe takes
// the same options: its duration and ease (each recipe's own when unset)
// and the part it moves (the whole object when empty).
struct RecipeOptions {
    std::optional<seconds> duration;
    std::optional<Ease> easing;
    std::string part;
};
// Attention: a short cue that ends where it began.
[[nodiscard]] Program pulse(ObjectInput object, Vec2 factor = {1.08,1.08}, RecipeOptions = {});
[[nodiscard]] Program shake(ObjectInput object, Vec2 amplitude = {8,0}, RecipeOptions = {});
[[nodiscard]] Program wobble(ObjectInput object, double radians = .12, RecipeOptions = {});
// Timed translation waypoints, relative to the actual pose at this recipe's
// start. Each point's easing/path describes the incoming leg. The first leg
// starts at offset zero/time zero; times must be finite and strictly increase.
struct MoveWaypoint {
    seconds at;
    Vec2 offset;
    Ease easing = ease::linear;
    PointPath path;
};
struct MoveThroughOptions { std::string part; };
[[nodiscard]] Program move_through(ObjectInput, std::span<const MoveWaypoint>, MoveThroughOptions = {});
[[nodiscard]] Program move_through(ObjectInput, std::initializer_list<MoveWaypoint>, MoveThroughOptions = {});
class Composite {
public:
    explicit Composite(Program child) : child_(std::move(child)) {}
    [[nodiscard]] Composite with_effect(EffectSpec value) const {
        auto result = *this; result.spec_.effects.push_back(std::move(value)); return result;
    }
    // This affects image parameters, never the timing of child animations.
    [[nodiscard]] Composite effect_easing(Ease value) const {
        auto result = *this; result.spec_.options.effect_easing = value; return result;
    }
    [[nodiscard]] operator Program() const { return composite(child_, spec_.effects, spec_.options); }
private:
    Program child_;
    CompositeSpec spec_;
};
}
[[nodiscard]] inline anim::Composite composite(Program child) { return anim::Composite{std::move(child)}; }
[[nodiscard]] inline anim::PartTarget part(ObjectInput object, std::string id) { return {std::move(object), std::move(id)}; }
namespace anim {
// A drop (plan §6.8 ⑨): the part falls by `offset` under gravity and bounces
// (Ease::fall), squashing as it lands and stretching as it speeds towards the
// ground, the less each bounce — written to its ch::squash, which a
// deform::squash deformer turns into geometry about its contact point. Any
// part of any object: made of the curve, a channel and its layers.
struct DropOptions {
    double  restitution = 0.45;
    int     bounces = 2;
    seconds duration{1};   // the fall's whole course, landing for good at its end (the last squash rounds after it, on a spring as quick as the drop)
    double  squash = 0.2;   // the first landing's flattening, a fraction
    double  stretch = 0.08; // the lengthening just before it
    double  contact = 0.1;  // time on the ground per landing, in first falls at the first's speed
};
[[nodiscard]] Program drop(PartTarget target, Vec2 offset, DropOptions options = {});

// Entrances: from a pose of its own to where it is (it ends as it was).
// Each fades in with the group opacity effect but write_in and drop_in.
// Entrances and exits are hanim.core's courses (`hanim::course`), played
// on the object: their poses, paces and fades are defined there once.
[[nodiscard]] Program fade_in(ObjectInput object, RecipeOptions = {});
[[nodiscard]] Program rise_in(ObjectInput object, double distance = 24, RecipeOptions = {});   // from below
[[nodiscard]] Program sink_in(ObjectInput object, double distance = 24, RecipeOptions = {});   // from above
[[nodiscard]] Program slide_in(ObjectInput object, Vec2 from = {-40, 0}, RecipeOptions = {});  // from that offset
[[nodiscard]] Program settle_in(ObjectInput object, double scale = .96, RecipeOptions = {}); // from a little smaller, no overshoot (a UI's quiet entrance)
[[nodiscard]] Program pop_in(ObjectInput object, RecipeOptions = {});    // from small, overshooting
[[nodiscard]] Program zoom_in(ObjectInput object, RecipeOptions = {});   // from large
[[nodiscard]] Program spin_in(ObjectInput object, double radians = -1.2, RecipeOptions = {});
[[nodiscard]] Program unfold_in(ObjectInput object, RecipeOptions = {}); // its height from nothing
// Its strokes drawn along their length (the part's trim_end from 0): a part with a stroke.
[[nodiscard]] Program write_in(ObjectInput object, RecipeOptions = {});
// Dropped from `height` above onto where it is, bouncing (drop): a part with deform::squash for the squash.
[[nodiscard]] Program drop_in(ObjectInput object, double height = 160, RecipeOptions = {});
// Exits: from where it is to a pose of its own, faded out (it is left there, hidden).
[[nodiscard]] Program fade_out(ObjectInput object, RecipeOptions = {});
[[nodiscard]] Program rise_out(ObjectInput object, double distance = 24, RecipeOptions = {});
[[nodiscard]] Program sink_out(ObjectInput object, double distance = 24, RecipeOptions = {});
[[nodiscard]] Program slide_out(ObjectInput object, Vec2 to = {40, 0}, RecipeOptions = {});
[[nodiscard]] Program settle_out(ObjectInput object, double scale = .96, RecipeOptions = {});
[[nodiscard]] Program pop_out(ObjectInput object, RecipeOptions = {});
[[nodiscard]] Program zoom_out(ObjectInput object, RecipeOptions = {});
[[nodiscard]] Program spin_out(ObjectInput object, double radians = 1.2, RecipeOptions = {});
[[nodiscard]] Program fold_out(ObjectInput object, RecipeOptions = {});
// Its strokes undrawn from their start (the part's trim_start to 1).
[[nodiscard]] Program unwrite(ObjectInput object, RecipeOptions = {});
// More attention cues.
[[nodiscard]] Program bounce(ObjectInput object, double height = 28, RecipeOptions = {});  // two hops, the second lower
[[nodiscard]] Program flash(ObjectInput object, int times = 2, RecipeOptions = {});         // gone and back, `times`
[[nodiscard]] Program heartbeat(ObjectInput object, double factor = 1.15, RecipeOptions = {});
[[nodiscard]] Program rubber_band(ObjectInput object, RecipeOptions = {});                 // squashed one way, the other, settling
[[nodiscard]] Program swing(ObjectInput object, double radians = .26, RecipeOptions = {}); // swinging, each way less
[[nodiscard]] Program tada(ObjectInput object, RecipeOptions = {});                        // shrinks, grows shaking, back
[[nodiscard]] Program nudge(ObjectInput object, Vec2 by = {8, 0}, RecipeOptions = {});     // a push and back
// Idle loops: `count` courses (forever when infinite), each one `period` long, ending where they began.
[[nodiscard]] Program bob(ObjectInput object, double amplitude = 6, seconds period = seconds(2.4),
                          double count = std::numeric_limits<double>::infinity(), RecipeOptions = {});
[[nodiscard]] Program breathe(ObjectInput object, double factor = 1.04, seconds period = seconds(3),
                              double count = std::numeric_limits<double>::infinity(), RecipeOptions = {});
[[nodiscard]] Program spin(ObjectInput object, seconds period = seconds(1.2),
                           double count = std::numeric_limits<double>::infinity(), RecipeOptions = {});
[[nodiscard]] Program sway(ObjectInput object, double radians = .08, seconds period = seconds(2.4),
                           double count = std::numeric_limits<double>::infinity(), RecipeOptions = {});
[[nodiscard]] Program orbit(ObjectInput object, double radius = 16, seconds period = seconds(2),
                            double count = std::numeric_limits<double>::infinity(), RecipeOptions = {});
[[nodiscard]] Program blink(ObjectInput object, seconds period = seconds(1),
                            double count = std::numeric_limits<double>::infinity(), RecipeOptions = {});
// Moves: along an arc (`bend` of the way's length to its side), two objects
// exchanging places, several moving to new places one after another.
[[nodiscard]] Program arc_to(ObjectInput object, Vec2 to, double bend = .3, RecipeOptions = {});
[[nodiscard]] Program swap(ObjectInput a, ObjectInput b, Vec2 a_to_b, double bend = .4, RecipeOptions = {});
[[nodiscard]] Program reorder(std::span<const ObjectInput> objects, std::span<const Vec2> offsets,
                              seconds step = seconds(.05), RecipeOptions = {});
// Pixels (the site plan's C1): a pixel picture's pixels (pixel_object's
// `pixels` form) flying apart from `from` — each its own way, turning and
// fading, the nearest first — and coming back together, the farthest first.
// The same seed, the same flight: a scatter played back is the assembly.
struct ScatterOptions {
    std::uint64_t       seed = 1;
    double              distance = 48;  // how far a pixel flies, about (the object's units)
    double              spin = 1.6;     // radians a pixel turns, at most
    std::optional<Vec2> from;           // where they fly from: the picture's centre when unset
    double              spread = .5;    // of the duration: how much later the last starts than the first
};
[[nodiscard]] Program scatter_out(ObjectInput object, const PixelGrid& grid, ScatterOptions = {}, RecipeOptions = {});
[[nodiscard]] Program assemble_in(ObjectInput object, const PixelGrid& grid, ScatterOptions = {}, RecipeOptions = {});
}  // namespace anim
template<class T> concept Animation = std::same_as<std::remove_cvref_t<T>, Program> ||
                                    std::same_as<std::remove_cvref_t<T>, anim::BoundAnimation> ||
                                    std::same_as<std::remove_cvref_t<T>, anim::Composite>;
template<Animation... T> requires (sizeof...(T) > 0)
[[nodiscard]] Program sequence(const T&... values) { return sequence({Program(values)...}); }
template<Animation... T> requires (sizeof...(T) > 0)
[[nodiscard]] Program parallel(const T&... values) { return parallel({Program(values)...}); }
template<Animation... T> requires (sizeof...(T) > 0)
[[nodiscard]] Program stagger(seconds step, const T&... values) { return stagger(step, {Program(values)...}); }
// One program's budget (core design §10.2); the totals bound the sum over
// its segments (an engine bounds the sum over its playbacks besides).
struct ProgramLimits {
    std::size_t max_nodes = 4096, max_depth = 64, max_tracks = 1024, max_segments = 4096;
    // Retained preparation counts across all segments, not bytes or AST input.
    std::size_t max_total_parts = 65536, max_total_cubics = 1048576, max_total_effect_parameters = 65536;
    EffectLimits effects;
    std::size_t max_scopes = 1024;
    std::size_t max_total_property_channels = 65536;
    std::size_t max_total_route_segments = 65536;
};
// A marker's place in the program (for a time-mapped program, in its course's own time).
struct ProgramMarker {
    std::string name;
    seconds     time{0};
    std::size_t node = 0;
};
struct ProgramContribution {
    std::size_t node = 0;
    std::string path;
    seconds start{0};
    SegmentOptions options;
};
struct PreparedSegment {
    std::size_t track = 0, node = 0;
    std::string path;
    seconds start{0};
    SegmentOptions options;
    PreparedObjectAnimation animation;
    PreparedEffects effects;
    bool anchor_source = false;
    std::size_t scope = invalid_program_index;
    // A course a time map replays backwards (a later iteration of an
    // alternating loop inside a program): its progress mirrored.
    bool reversed = false;
    // Nonempty only when overlapping property leaves compile to one object
    // output. Original author nodes/times remain inspectable; clip indices in
    // animation.channels() address this array, not a rewritten author tree.
    std::vector<ProgramContribution> contributions;
};
// Author preorder; [node, subtree_end) is contiguous. Timing is absolute.
struct ProgramNodeInfo {
    ProgramKind kind = ProgramKind::empty;
    std::size_t parent = invalid_program_index, subtree_end = 0;
    seconds start{0}, duration{0};
    std::size_t scope = invalid_program_index, track = invalid_program_index, segment = invalid_program_index;
    std::size_t contribution = invalid_program_index;
    // Repeat nodes record the authored count; each expanded child root records
    // its zero-based iteration. Descendants use their ordinary parent links.
    std::size_t repeat_count = 0, iteration = invalid_program_index;
};
struct PreparedScope {
    std::size_t node = 0, parent = invalid_program_index, depth = 1, parameter_offset = 0;
    seconds start{0}, duration{0};
    Ease effect_easing = ease::linear;
    PreparedEffects effects;
};
struct ProgramTrack {
    ObjectRef object;
    std::uint64_t source_revision = 0;
    FrameRequirements requirements;
    std::size_t max_effect_parameters = 0, first_segment = 0, segment_count = 0;
};
struct ProgramSample {
    FrameView geometry;
    std::size_t segment_index = 0;
    double linear_progress = 0, sample_progress = 0;
    std::size_t effect_parameters = 0;
};
struct ProgramTrackView {
    FrameView geometry;
    std::size_t segment_index = 0;
    std::span<const float> effect_values;
};
struct ProgramFrameView {
    std::span<const ProgramTrackView> tracks;
    std::span<const float> scope_effect_values;
    seconds position{0};
    const void* preparation = nullptr;
};
class PreparedProgram {
public:
    PreparedProgram() noexcept = default;
    [[nodiscard]] static std::expected<PreparedProgram, ProgramFailure> prepare(const Program&, ProgramLimits limits = {});
    // Constrained deduction preserves legacy prepare(program, {}) calls:
    // braces must remain ProgramLimits, not an uninitialized ObjectRegistry.
    template<class Registry> requires std::same_as<std::remove_cvref_t<Registry>, ObjectRegistry>
    [[nodiscard]] static std::expected<PreparedProgram, ProgramFailure>
    prepare(const Program& program, Registry&& registry, ProgramLimits limits = {}) {
        return prepare_with(program, &registry, limits);
    }
    // Normalization path for the existing strict morph API and current-pose
    // retarget plans whose from() Snapshot can deliberately be empty.
    [[nodiscard]] static std::expected<PreparedProgram, ProgramFailure> single(
        PreparedObjectAnimation, PreparedEffects, SegmentOptions options = {}, bool anchor_source = false, ProgramLimits limits = {});
    [[nodiscard]] explicit operator bool() const noexcept { return bool(data_); }
    // Its whole length in time: for a time-mapped program the mapped length
    // (infinite when it loops forever); natural_duration() is one course.
    [[nodiscard]] seconds duration() const noexcept;
    [[nodiscard]] seconds natural_duration() const noexcept;
    [[nodiscard]] const TimeMap* time_map() const & noexcept;
    const TimeMap* time_map() const && = delete;
    [[nodiscard]] std::span<const ProgramMarker> markers() const & noexcept;
    std::span<const ProgramMarker> markers() const && = delete;
    [[nodiscard]] std::size_t route_segment_count() const noexcept;
    [[nodiscard]] std::span<const ProgramTrack> tracks() const & noexcept;
    std::span<const ProgramTrack> tracks() const && = delete;
    [[nodiscard]] std::span<const PreparedSegment> segments() const & noexcept;
    std::span<const PreparedSegment> segments() const && = delete;
    [[nodiscard]] std::span<const ProgramNodeInfo> nodes() const & noexcept;
    std::span<const ProgramNodeInfo> nodes() const && = delete;
    [[nodiscard]] std::span<const PreparedScope> scopes() const & noexcept;
    std::span<const PreparedScope> scopes() const && = delete;
    [[nodiscard]] std::size_t scope_parameter_count() const noexcept;
    [[nodiscard]] const void* identity() const noexcept { return data_.get(); }
    [[nodiscard]] std::expected<void, ProgramFailure> sample_scopes(
        seconds time, std::span<float> output, bool exact_source = false) const noexcept;
    // Finite local time clamps per track. Gaps hold the preceding segment;
    // before the first start, hold its exact source. Later starts win at a
    // shared boundary. exact_source selects the first segment and raw zero.
    // Buffers may be track maxima; returned spans contain actual segment data.
    // Failures preserve both output buffers. No matching, mutation or allocation.
    [[nodiscard]] std::expected<ProgramSample, ProgramFailure> sample(
        std::size_t track, seconds time, FrameBuffer geometry, std::span<float> effects,
        bool exact_source = false) const noexcept;
private:
    [[nodiscard]] static std::expected<PreparedProgram, ProgramFailure>
        prepare_with(const Program&, const ObjectRegistry*, ProgramLimits);
    std::shared_ptr<const detail::ProgramData> data_;
};
} // namespace hanim
