// Public object animation recipes: prepared once, sampled without an event loop.
export module hanim.library;

import std;
export import hanim.object;

namespace hanim::detail { struct ObjectMorphData; }

export namespace hanim {

struct MorphOptions {
    std::size_t max_parts = 4096, max_contours = 4096, max_cubics = 65536;
    ShapeMorphOptions shape;
    PointPath translation_path; // each part's local tx/ty, not its geometry or parent
};
// A typed partial target for one part. Empty part selects the object's unique
// root, never a magic id. Positions are in the parent's coordinate system;
// rotation is an explicit radian value (including full turns), scale is about
// the part's local origin. Unspecified properties and descendants stay intact.
// position and offset are exclusive. offset uses the actual segment source,
// including a preceding animation's endpoint or a retarget's current pose.
struct PropertyTargets {
    std::optional<Vec2> position, offset;
    std::optional<double> rotation;
    std::optional<Vec2> scale;
    std::optional<Rgba> fill;
    std::string part;
};
// One atomic animation may target several parts and independent properties.
// Duplicate writes to a part/channel are errors, never implicit last-wins.
using PropertySet = std::vector<PropertyTargets>;
enum class MorphError : std::uint8_t {
    unprepared, invalid_snapshot, invalid_frame, invalid_options,
    part_budget, contour_budget, cubic_budget, part_set_mismatch,
    hierarchy_mismatch, ordering_mismatch, contour_count_mismatch,
    missing_fill, open_contour, unsupported_topology, shape_failure, invalid_progress,
    output_too_small, nonfinite_transform, invalid_property, unknown_part, property_conflict
};
[[nodiscard]] std::string_view morph_error_name(MorphError error) noexcept;
struct MorphFailure {
    MorphError code = MorphError::unprepared;
    std::size_t part = std::numeric_limits<std::size_t>::max();
    std::size_t contour = std::numeric_limits<std::size_t>::max();
    std::optional<ShapeMorphError> shape;
};
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
    [[nodiscard]] explicit operator bool() const noexcept { return bool(data_); }
    [[nodiscard]] FrameRequirements requirements() const noexcept;
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
private:
    Snapshot from_, to_;
    ObjectRef source_;
    std::uint64_t revision_ = 0;
    std::shared_ptr<const detail::ObjectMorphData> data_;
};

namespace anim {
struct MorphTo { ObjectRef target; MorphOptions options; };
[[nodiscard]] constexpr MorphTo morph_to(ObjectRef target, MorphOptions options = {}) noexcept { return {target, options}; }
}
} // namespace hanim
