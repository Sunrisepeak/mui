// Public object animation recipes: prepared once, sampled without an event loop.
export module hanim.library;

import std;
import mlibs.error;
export import hanim.object;

namespace hanim::detail { struct ObjectMorphData; }

export namespace hanim {

struct MorphOptions {
    std::size_t max_parts = 4096, max_contours = 4096, max_cubics = 65536;
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
class TranslationRoute {
public:
    explicit TranslationRoute(MotionPath path) : path_(std::make_shared<const MotionPath>(std::move(path))) {}
    [[nodiscard]] const MotionPath* path() const & noexcept { return path_.get(); }
    const MotionPath* path() const && = delete;
private:
    std::shared_ptr<const MotionPath> path_;
};
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
};
// One atomic animation may target several parts and independent properties.
// Duplicate writes to a part/channel are errors, never implicit last-wins.
using PropertySet = std::vector<PropertyTargets>;
enum class PropertyChannel : std::uint8_t { translation, rotation, scale, fill };
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
};
enum class MorphError : std::uint8_t {
    unprepared, invalid_snapshot, invalid_frame, invalid_options,
    part_budget, contour_budget, cubic_budget, part_set_mismatch,
    hierarchy_mismatch, ordering_mismatch, contour_count_mismatch,
    missing_fill, open_contour, unsupported_topology, shape_failure, invalid_progress,
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
};
struct MorphContourInfo {
    std::uint32_t part = 0;
    std::size_t ordinal = 0, first_cubic = 0, cubic_count = 0;
};
struct FramePart {
    MorphPartInfo info;
    Transform2D local;
    std::optional<Rgba> fill;
};
struct FrameBuffer {
    std::span<FramePart> parts;
    std::span<ShapeCubic> cubics;
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
};
struct FrameRequirements { std::size_t parts = 0, contours = 0, cubics = 0; };

class PreparedObjectMorph {
public:
    PreparedObjectMorph() noexcept = default;
    [[nodiscard]] static std::expected<PreparedObjectMorph, MorphFailure>
        prepare(Snapshot from, Snapshot to, MorphOptions options = {});
    // Deep-copy the current pose into the new preparation. No registry writes
    // or old plan/frame/snapshot chain is retained. current must remain valid
    // and unchanged throughout this call. Its logical from_ref is preserved.
    [[nodiscard]] static std::expected<PreparedObjectMorph, MorphFailure>
        retarget(FrameView current, Snapshot to, MorphOptions options = {});
    // Unary property recipes share the same geometry/effect sampler. There is
    // no synthetic target object or registry mutation; to() is empty for these
    // plans. Current supported representations are the same closed vector
    // contours as object morph, not arbitrary application fields or open paths.
    [[nodiscard]] static std::expected<PreparedObjectMorph, MorphFailure>
        properties(Snapshot from, const PropertyTargets&, MorphOptions options = {});
    [[nodiscard]] static std::expected<PreparedObjectMorph, MorphFailure>
        properties(FrameView current, const PropertyTargets&, MorphOptions options = {});
    [[nodiscard]] static std::expected<PreparedObjectMorph, MorphFailure>
        properties(Snapshot from, std::span<const PropertyTargets>, MorphOptions options = {});
    [[nodiscard]] static std::expected<PreparedObjectMorph, MorphFailure>
        properties(FrameView current, std::span<const PropertyTargets>, MorphOptions options = {});
    // Pure prepared property tracks with independent timing. Input order
    // explicitly orders zero-duration writes at a shared start. Positive
    // overlap on one part/channel is rejected; geometry stays unchanged.
    [[nodiscard]] static std::expected<PreparedObjectMorph, MorphFailure>
        property_timeline(Snapshot from, std::span<const PropertyClip>, PropertyTimelineLimits limits = {});
    [[nodiscard]] static std::expected<PreparedObjectMorph, MorphFailure>
        property_timeline(FrameView current, std::span<const PropertyClip>, PropertyTimelineLimits limits = {});
    [[nodiscard]] std::span<const PropertyChannelInfo> channels() const & noexcept;
    std::span<const PropertyChannelInfo> channels() const && = delete;
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
// Canonical name covers morph, property recipes and prepared channel timelines;
// the original name remains the same type for source compatibility.
using PreparedObjectAnimation = PreparedObjectMorph;

namespace anim {
struct MorphTo { ObjectRef target; MorphOptions options; };
[[nodiscard]] constexpr MorphTo morph_to(ObjectRef target, MorphOptions options = {}) noexcept { return {target, options}; }
}
} // namespace hanim
