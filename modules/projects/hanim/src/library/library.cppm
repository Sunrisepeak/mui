// Public object animation recipes: prepared once, sampled without an event loop.
export module hanim.library;

import std;
import mlibs.error;
export import hanim.object;

namespace hanim::detail { struct ObjectMorphData; }

export namespace hanim {

// How a part's contours correspond when its two ends differ (convergence
// plan §6.8 ③). align pairs outlines with outlines and holes with holes
// (by winding against the part's largest contour), nearest and closest in
// size first; a contour left over grows from a point or closes into one: an
// outline from (into) the nearest outline of the other end, so "一" splits
// into "二", a hole from (into) its own centre. strict pairs them in order
// and refuses differing counts.
enum class ContourPairing : std::uint8_t { align, strict };
// Match modifiers (plan §6.8 ⑤): how align weighs a pair — centroid distance
// over the part's size, the log of the area ratio — and pairs pinned before
// any is weighed: part `part`'s source contour `from` with target contour
// `to` (ordinals; both outlines or both holes).
struct ContourPin {
    std::string part;
    std::size_t from = 0, to = 0;
};
struct MatchRules {
    double distance = 1, size = .5;  // finite, ≥ 0
    std::vector<ContourPin> pins;
};
struct MorphOptions {
    std::size_t max_parts = 4096, max_contours = 4096, max_cubics = 65536;
    ContourPairing contours = ContourPairing::align;
    MatchRules match;
    ShapeMorphOptions shape;
    PointPath translation_path; // each part's local tx/ty, not its geometry or parent
    std::size_t max_route_segments = 4096;
};
// A typed partial target for one part. Empty part selects the object's unique
// root, never a magic id. Positions are in the parent's coordinate system;
// rotation is an explicit radian value (including full turns), scale is about
// the part's local origin. Unspecified properties and descendants stay intact.
// position and offset are exclusive. offset uses the actual segment source,
// including a preceding animation's endpoint or a retarget's current pose.
// Owns an immutable copy of the existing arc-length path, cheaply reusable in
// descriptions. No caller-held mutable shared pointer can alias this storage.
// ── Channels (L2): what an animation writes, typed ─────────────────────
//
//     anim::to(card, ch::position = {120, 40}, ch::fill = Rgba::bytes(0x33, 0x55, 0xff))
//     inline constexpr auto glow = ch::define<double>("app.glow");   // a channel of the application's
//     anim::to(card, glow = 1.0)                                     // sampled into FrameView::values
//
// position, rotation, scale and fill write a part's transform and fill; any
// other channel is a value of the part, interpolated in its lanes (hanim.core's
// lanes<T>), its own until the consumer reads it.
namespace ch {
inline constexpr std::size_t max_lanes = 8;
// A channel's identity: its name and how many lanes its values have.
struct Id {
    std::string_view name;
    std::uint8_t lanes = 1;
    friend constexpr bool operator==(const Id& a, const Id& b) noexcept { return a.name == b.name && a.lanes == b.lanes; }
};
using Lanes = std::array<double, max_lanes>;
// A write to a channel, in its lanes: absolute (to) or relative (by: added
// to where the channel is); `rest` is its value before anything writes it.
struct Value {
    Id    id;
    Lanes lanes{}, rest{};
    bool  relative = false;
};
template <Animatable T> struct Channel;
template <Animatable T>
struct Write {
    Channel<T> channel;
    T          value;
};
template <Animatable T>
struct Channel {
    Id id;
    T  rest{};
    // `ch::position = {120, 40}`: a write, not an assignment to the channel.
    constexpr Write<T> operator=(T value) const noexcept { return {*this, value}; }
    [[nodiscard]] Value value(const T& v, bool relative = false) const {
        Value out{id, {}, {}, relative};
        const auto a = lanes<T>::to(v), r = lanes<T>::to(rest);
        std::ranges::copy(a, out.lanes.begin());
        std::ranges::copy(r, out.rest.begin());
        return out;
    }
};
template <Animatable T> requires (lanes<T>::count >= 1 && static_cast<std::size_t>(lanes<T>::count) <= max_lanes)
[[nodiscard]] consteval Channel<T> define(std::string_view name, T rest = {}) {
    return {{name, static_cast<std::uint8_t>(lanes<T>::count)}, rest};
}
inline constexpr Channel<Vec2>   position = define<Vec2>("position");
inline constexpr Channel<double> rotation = define<double>("rotation");
inline constexpr Channel<Vec2>   scale    = define<Vec2>("scale", {1, 1});
inline constexpr Channel<Rgba>   fill     = define<Rgba>("fill");
// How much of each contour a part's stroke draws, as fractions of its length:
// from trim_start to trim_end, both moved on by trim_offset (wrapping round a
// closed contour). Values of the part, as any channel of the application's:
// `anim::to(glyph, ch::trim_offset = 1)` runs a stretch once round.
inline constexpr Channel<double> trim_start  = define<double>("trim.start");
inline constexpr Channel<double> trim_end    = define<double>("trim.end", 1);
inline constexpr Channel<double> trim_offset = define<double>("trim.offset");
// The channels a part's own fields carry: no Value may name them.
[[nodiscard]] constexpr bool builtin(std::string_view name) noexcept {
    return name == "position" || name == "rotation" || name == "scale" || name == "fill";
}
}  // namespace ch

// ── Deformers (L2, plan §6.8 ④): a part's geometry follows a channel ──
//
//     inline constexpr auto pull = ch::define<Vec2>("glyph.pull");
//     part.deformers = {deform::displace(Selector::inside({10, 40}, {50, 48}, 6), pull)};
//     anim::to(part(a, "A"), pull = Vec2{0, 12})            // the crossbar pulled down …
//     anim::to(part(a, "A"), pull = Vec2{}).spring(springs::bouncy)   // … and let go: it overshoots
//
// The amount is any animation's value of the channel (its rest without one);
// weights are frozen on the geometry the animation starts from. Amounts are
// clamped to ±1e100, so a deformed point stays finite.
namespace deform {
[[nodiscard]] inline PartDeformer displace(Selector select, const ch::Channel<Vec2>& offset) {
    return {std::string(offset.id.name), DeformKind::displace, select, {}};
}
[[nodiscard]] inline PartDeformer scale(Selector select, const ch::Channel<Vec2>& factors, Vec2 anchor) {
    return {std::string(factors.id.name), DeformKind::scale, select, anchor};
}
[[nodiscard]] inline PartDeformer rotate(Selector select, const ch::Channel<double>& radians, Vec2 anchor) {
    return {std::string(radians.id.name), DeformKind::rotate, select, anchor};
}
}  // namespace deform

class TranslationRoute {
public:
    explicit TranslationRoute(MotionPath path) : path_(std::make_shared<const MotionPath>(std::move(path))) {}
    [[nodiscard]] const MotionPath* path() const & noexcept { return path_.get(); }
    const MotionPath* path() const && = delete;
private:
    std::shared_ptr<const MotionPath> path_;
};
// How a write meets the others on its channel (plan §6.8 ⑥, layers). replace
// is the base: one writer at a time, each going on from the last. The others
// are layers over it, in author order, each its own course from an identity
// (add: zero; multiply: one; mix: where the channel is when it starts):
// add adds weight × its value, multiply scales by 1 + weight × (value − 1)
// (scale and value channels), mix moves the channel towards its value by
// weight. A breath (scale added) goes on over a drag (position replaced).
enum class Blend : std::uint8_t { replace, add, multiply, mix };
struct PropertyTargets {
    std::optional<Vec2> position, offset;
    std::optional<double> rotation;
    std::optional<Vec2> scale;
    std::optional<Rgba> fill;
    std::string part;
    // Relative operations use the actual segment/channel source. Absolute
    // and relative forms of the same channel are mutually exclusive.
    std::optional<double> rotation_offset;
    std::optional<Vec2> scale_factor;
    // Relative to the actual source position: path(u) - path(0).
    std::optional<TranslationRoute> route;
    // The part's other channels (an application's): one write a channel.
    std::vector<ch::Value> values;
    Blend blend = Blend::replace;
    double weight = 1;  // a layer's, finite
};
// One atomic animation may target several parts and independent properties.
// Duplicate writes to a part/channel are errors, never implicit last-wins.
using PropertySet = std::vector<PropertyTargets>;
enum class PropertyChannel : std::uint8_t { translation, rotation, scale, fill, value };
[[nodiscard]] std::string_view property_channel_name(PropertyChannel) noexcept;
struct PropertyClip {
    PropertySet targets;
    seconds start{0}, duration{1};
    Ease easing = ease::linear;
    MorphOptions options;
    // Omitted: derive this clip from the actual prior channel output. An
    // explicit snapshot supplies only the selected local source properties.
    std::optional<Snapshot> source;
    bool anchor_source = false;
};
struct PropertyTimelineLimits { std::size_t max_clips = 4096, max_channels = 65536; };
struct PropertyChannelInfo {
    std::size_t part = 0, clip = 0;
    PropertyChannel channel = PropertyChannel::translation;
    seconds start{0}, duration{0};
    bool anchor_source = false;
    Ease easing = ease::linear;
    PointPath translation_path;
    bool source_snapshot = false;
    std::optional<TranslationRoute> route;
    Vec2 route_origin;
    // A value channel's slot (PreparedObjectAnimation::value_slots()).
    std::size_t value_slot = std::numeric_limits<std::size_t>::max();
    // 0: the base; a layer's is 1 + its clip, combined in that order.
    std::size_t layer = 0;
    Blend blend = Blend::replace;
    double weight = 1;
};
// Where a part's value channel lands in FrameView::values: [offset, offset + lanes);
// rest is its value before this animation writes it (its channel's rest, or
// where the frame it continues from had it).
struct ValueSlot {
    std::size_t part = 0;
    ch::Id      id;
    std::size_t offset = 0;
    ch::Lanes   rest{};
};
enum class MorphError : std::uint8_t {
    unprepared, invalid_snapshot, invalid_frame, invalid_options,
    part_budget, contour_budget, cubic_budget, part_set_mismatch,
    hierarchy_mismatch, ordering_mismatch, contour_count_mismatch,
    missing_fill, missing_stroke, open_contour, unsupported_topology, shape_failure, invalid_progress,
    output_too_small, nonfinite_transform, invalid_property, unknown_part, property_conflict,
    invalid_time, invalid_ease, channel_budget, unsupported_property_effect, route_budget
};
// The codes as the error contract describes them (mlibs.error).
[[nodiscard]] constexpr mlibs::error::Description describe(MorphError e) noexcept {
    using Kind = mlibs::error::Kind;  // not a using-declaration: that is an ICE in GCC 16
    switch (e) {
        case MorphError::unprepared: return {"hanim.morph.unprepared", Kind::invalid_argument};
        case MorphError::invalid_snapshot: return {"hanim.morph.invalid-snapshot", Kind::invalid_argument};
        case MorphError::invalid_frame: return {"hanim.morph.invalid-frame", Kind::invalid_argument};
        case MorphError::invalid_options: return {"hanim.morph.invalid-options", Kind::invalid_argument};
        case MorphError::part_budget: return {"hanim.morph.part-budget", Kind::refused};
        case MorphError::contour_budget: return {"hanim.morph.contour-budget", Kind::refused};
        case MorphError::cubic_budget: return {"hanim.morph.cubic-budget", Kind::refused};
        case MorphError::part_set_mismatch: return {"hanim.morph.part-set-mismatch", Kind::invalid_argument};
        case MorphError::hierarchy_mismatch: return {"hanim.morph.hierarchy-mismatch", Kind::invalid_argument};
        case MorphError::ordering_mismatch: return {"hanim.morph.ordering-mismatch", Kind::invalid_argument};
        case MorphError::contour_count_mismatch: return {"hanim.morph.contour-count-mismatch", Kind::invalid_argument};
        case MorphError::missing_fill: return {"hanim.morph.missing-fill", Kind::invalid_argument};
        case MorphError::missing_stroke: return {"hanim.morph.missing-stroke", Kind::invalid_argument};
        case MorphError::open_contour: return {"hanim.morph.open-contour", Kind::invalid_argument};
        case MorphError::unsupported_topology: return {"hanim.morph.unsupported-topology", Kind::refused};
        case MorphError::shape_failure: return {"hanim.morph.shape-failure", Kind::failed, {}, true};
        case MorphError::invalid_progress: return {"hanim.morph.invalid-progress", Kind::invalid_argument};
        case MorphError::output_too_small: return {"hanim.morph.output-too-small", Kind::invalid_argument};
        case MorphError::nonfinite_transform: return {"hanim.morph.nonfinite-transform", Kind::invalid_argument};
        case MorphError::invalid_property: return {"hanim.morph.invalid-property", Kind::invalid_argument};
        case MorphError::unknown_part: return {"hanim.morph.unknown-part", Kind::not_found};
        case MorphError::property_conflict: return {"hanim.morph.property-conflict", Kind::refused};
        case MorphError::invalid_time: return {"hanim.morph.invalid-time", Kind::invalid_argument};
        case MorphError::invalid_ease: return {"hanim.morph.invalid-ease", Kind::invalid_argument};
        case MorphError::channel_budget: return {"hanim.morph.channel-budget", Kind::refused};
        case MorphError::unsupported_property_effect: return {"hanim.morph.unsupported-property-effect", Kind::refused};
        case MorphError::route_budget: return {"hanim.morph.route-budget", Kind::refused};
    }
    return {"hanim.morph.?", Kind::internal};
}
// Where in a morph an error is: the part, the contour, the clip and the
// channel, each when it applies. A shape's own error is a cause beneath.
struct MorphWhere {
    std::size_t part = std::numeric_limits<std::size_t>::max();
    std::size_t contour = std::numeric_limits<std::size_t>::max();
    std::size_t clip = std::numeric_limits<std::size_t>::max();
    std::optional<PropertyChannel> channel;
};
[[nodiscard]] std::string describe_where(const MorphWhere& where);
using MorphFailure = mlibs::error::Error<MorphError, MorphWhere>;
struct MorphPartInfo {
    std::string_view id;
    PartRef from_ref, to_ref;
    std::uint32_t parent = invalid_object_slot;
    std::size_t first_contour = 0, contour_count = 0;
    std::span<const PartDeformer> deformers;  // the source end's, borrowed from the plan
    // The part's cubics before its deformers (when it has any), borrowed from
    // the plan: what an animation continued from this frame starts from.
    std::span<const ShapeCubic> base;
};
struct MorphContourInfo {
    std::uint32_t part = 0;
    std::size_t ordinal = 0, first_cubic = 0, cubic_count = 0;
};
struct FramePart {
    MorphPartInfo info;
    Transform2D local;
    std::optional<Rgba> fill;
    std::optional<PartStroke> stroke;
};
struct FrameBuffer {
    std::span<FramePart> parts;
    std::span<ShapeCubic> cubics;
    std::span<double> values = {};  // value channels' lanes (FrameRequirements::values)
};
// parts/cubics borrow the caller buffer; contours and ids borrow a retained,
// unmoved plan (or its O(1) copy). A FrameView itself is not a lifetime lease.
struct FrameView {
    std::span<const FramePart> parts;
    std::span<const MorphContourInfo> contours;
    std::span<const ShapeCubic> cubics;
    double progress = 0;
    ObjectRef source_object;
    std::uint64_t source_revision = 0;
    const void* preparation = nullptr; // opaque process-local witness, never an Agent/serialized id
    // Value channels: their lanes, laid out as value_slots says.
    std::span<const double> values = {};
    std::span<const ValueSlot> value_slots = {};
    // The value of `channel` on part `part` (its lanes), when it has one.
    [[nodiscard]] std::optional<std::span<const double>> value(std::size_t part, ch::Id channel) const noexcept {
        for (const auto& slot : value_slots)
            if (slot.part == part && slot.id == channel) return values.subspan(slot.offset, slot.id.lanes);
        return std::nullopt;
    }
    template <Animatable T>
    [[nodiscard]] std::optional<T> value(std::size_t part, const ch::Channel<T>& channel) const noexcept {
        const auto lanes_of = value(part, channel.id);
        if (!lanes_of) return std::nullopt;
        std::array<double, lanes<T>::count> a{};
        std::ranges::copy(*lanes_of, a.begin());
        return lanes<T>::from(a);
    }
};
struct FrameRequirements { std::size_t parts = 0, contours = 0, cubics = 0, values = 0; };

class PreparedObjectAnimation {
public:
    PreparedObjectAnimation() noexcept = default;
    [[nodiscard]] static std::expected<PreparedObjectAnimation, MorphFailure>
        prepare(Snapshot from, Snapshot to, MorphOptions options = {});
    // Deep-copy the current pose into the new preparation. No registry writes
    // or old plan/frame/snapshot chain is retained. current must remain valid
    // and unchanged throughout this call. Its logical from_ref is preserved.
    [[nodiscard]] static std::expected<PreparedObjectAnimation, MorphFailure>
        retarget(FrameView current, Snapshot to, MorphOptions options = {});
    // Unary property recipes share the same geometry/effect sampler. There is
    // no synthetic target object or registry mutation; to() is empty for these
    // plans. Current supported representations are the same closed vector
    // contours as object morph, not arbitrary application fields or open paths.
    [[nodiscard]] static std::expected<PreparedObjectAnimation, MorphFailure>
        properties(Snapshot from, const PropertyTargets&, MorphOptions options = {});
    [[nodiscard]] static std::expected<PreparedObjectAnimation, MorphFailure>
        properties(FrameView current, const PropertyTargets&, MorphOptions options = {});
    [[nodiscard]] static std::expected<PreparedObjectAnimation, MorphFailure>
        properties(Snapshot from, std::span<const PropertyTargets>, MorphOptions options = {});
    [[nodiscard]] static std::expected<PreparedObjectAnimation, MorphFailure>
        properties(FrameView current, std::span<const PropertyTargets>, MorphOptions options = {});
    // Pure prepared property tracks with independent timing. Input order
    // explicitly orders zero-duration writes at a shared start. Positive
    // overlap on one part/channel is rejected; geometry stays unchanged.
    [[nodiscard]] static std::expected<PreparedObjectAnimation, MorphFailure>
        property_timeline(Snapshot from, std::span<const PropertyClip>, PropertyTimelineLimits limits = {});
    [[nodiscard]] static std::expected<PreparedObjectAnimation, MorphFailure>
        property_timeline(FrameView current, std::span<const PropertyClip>, PropertyTimelineLimits limits = {});
    [[nodiscard]] std::span<const PropertyChannelInfo> channels() const & noexcept;
    std::span<const PropertyChannelInfo> channels() const && = delete;
    [[nodiscard]] std::span<const ValueSlot> value_slots() const & noexcept;
    std::span<const ValueSlot> value_slots() const && = delete;
    [[nodiscard]] seconds timeline_duration() const noexcept;
    [[nodiscard]] explicit operator bool() const noexcept { return bool(data_); }
    [[nodiscard]] FrameRequirements requirements() const noexcept;
    [[nodiscard]] std::size_t route_segment_count() const noexcept;
    [[nodiscard]] std::span<const MorphPartInfo> parts() const & noexcept;
    std::span<const MorphPartInfo> parts() const && = delete;
    [[nodiscard]] std::span<const MorphContourInfo> contours() const & noexcept;
    std::span<const MorphContourInfo> contours() const && = delete;
    [[nodiscard]] const Snapshot& from() const & noexcept { return from_; }
    const Snapshot& from() const && = delete;
    [[nodiscard]] const Snapshot& to() const & noexcept { return to_; }
    const Snapshot& to() const && = delete;
    [[nodiscard]] ObjectRef source_object() const noexcept { return data_ ? source_ : ObjectRef{}; }
    [[nodiscard]] std::uint64_t source_revision() const noexcept { return data_ ? revision_ : 0; }
    [[nodiscard]] const void* identity() const noexcept { return data_.get(); }
    // Finite progress clamps uniformly to [0,1]. All returned failures occur
    // before touching output. No matching/allocation/mutable sampling cache.
    [[nodiscard]] std::expected<FrameView, MorphFailure> sample(double progress, FrameBuffer output) const noexcept;
    // Restore bypasses every channel curve, including instantaneous / jump-
    // start clips. Ordinary sample(0) still follows their authored semantics.
    [[nodiscard]] std::expected<FrameView, MorphFailure> sample_source(FrameBuffer output) const noexcept;
private:
    [[nodiscard]] std::expected<FrameView, MorphFailure> sample_at(double, FrameBuffer, bool exact_source) const noexcept;
    Snapshot from_, to_;
    ObjectRef source_;
    std::uint64_t revision_ = 0;
    std::shared_ptr<const detail::ObjectMorphData> data_;
};

} // namespace hanim
