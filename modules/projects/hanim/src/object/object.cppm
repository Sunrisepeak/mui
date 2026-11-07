// HObject's first owned domain: hierarchical parts and actual vector contours.
export module hanim.object;

import std;
import mlibs.error;
export import hanim.core;
export import mlibs.geom;
import mlibs.memory;

namespace hanim::detail {
struct ContourData;
struct SnapshotData;
struct RegistryData;
}

export namespace hanim {

inline constexpr std::uint32_t object_contract_version = 1;
inline constexpr std::uint32_t invalid_object_slot = std::numeric_limits<std::uint32_t>::max();

// An object of a registry: the handle every engine's references share
// (mlibs.memory's OwnedHandle), tagged.
struct ObjectTag;
using ObjectRef = mlibs::memory::OwnedHandle<ObjectTag>;
static_assert(ObjectRef::invalid_slot == invalid_object_slot);
// HObject is a thin identity. Its live representation belongs to its registry;
// a frozen representation belongs to a Snapshot, not to the original host.
using HObject = ObjectRef;

struct PartRef {
    ObjectRef object;
    std::uint32_t slot = invalid_object_slot;
    std::uint64_t generation = 0;
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return bool(object) && slot != invalid_object_slot && generation != 0; }
    friend constexpr bool operator==(const PartRef& a, const PartRef& b) noexcept { return a.object == b.object && a.slot == b.slot && a.generation == b.generation; }
};

enum class ObjectError : std::uint8_t {
    uninitialized, invalid_limits, identity_exhausted, version_exhausted,
    invalid_object, foreign_owner, stale_object, different_object,
    invalid_part, stale_part, part_not_found, invalid_id, duplicate_id,
    missing_parent, multiple_roots, cycle, depth_limit, capacity,
    invalid_coordinate, invalid_verb, invalid_transform, invalid_color,
    invalid_resource, unsupported_capability, reentrant_mutation, invalid_path
};
// The codes as the error contract describes them (mlibs.error).
[[nodiscard]] constexpr mlibs::error::Description describe(ObjectError e) noexcept {
    using Kind = mlibs::error::Kind;  // not a using-declaration: that is an ICE in GCC 16
    switch (e) {
        case ObjectError::uninitialized: return {"hanim.object.uninitialized", Kind::invalid_argument};
        case ObjectError::invalid_limits: return {"hanim.object.invalid-limits", Kind::invalid_argument};
        case ObjectError::identity_exhausted: return {"hanim.object.identity-exhausted", Kind::refused};
        case ObjectError::version_exhausted: return {"hanim.object.version-exhausted", Kind::refused};
        case ObjectError::invalid_object: return {"hanim.object.invalid-object", Kind::invalid_argument};
        case ObjectError::foreign_owner: return {"hanim.object.foreign-owner", Kind::invalid_argument};
        case ObjectError::stale_object: return {"hanim.object.stale-object", Kind::stale};
        case ObjectError::different_object: return {"hanim.object.different-object", Kind::invalid_argument};
        case ObjectError::invalid_part: return {"hanim.object.invalid-part", Kind::invalid_argument};
        case ObjectError::stale_part: return {"hanim.object.stale-part", Kind::stale};
        case ObjectError::part_not_found: return {"hanim.object.part-not-found", Kind::not_found};
        case ObjectError::invalid_id: return {"hanim.object.invalid-id", Kind::invalid_argument};
        case ObjectError::duplicate_id: return {"hanim.object.duplicate-id", Kind::invalid_argument};
        case ObjectError::missing_parent: return {"hanim.object.missing-parent", Kind::invalid_argument};
        case ObjectError::multiple_roots: return {"hanim.object.multiple-roots", Kind::invalid_argument};
        case ObjectError::cycle: return {"hanim.object.cycle", Kind::invalid_argument};
        case ObjectError::depth_limit: return {"hanim.object.depth-limit", Kind::refused};
        case ObjectError::capacity: return {"hanim.object.capacity", Kind::refused};
        case ObjectError::invalid_coordinate: return {"hanim.object.invalid-coordinate", Kind::invalid_argument};
        case ObjectError::invalid_verb: return {"hanim.object.invalid-verb", Kind::invalid_argument};
        case ObjectError::invalid_transform: return {"hanim.object.invalid-transform", Kind::invalid_argument};
        case ObjectError::invalid_color: return {"hanim.object.invalid-color", Kind::invalid_argument};
        case ObjectError::invalid_resource: return {"hanim.object.invalid-resource", Kind::invalid_argument};
        case ObjectError::unsupported_capability: return {"hanim.object.unsupported-capability", Kind::refused};
        case ObjectError::reentrant_mutation: return {"hanim.object.reentrant-mutation", Kind::refused};
        case ObjectError::invalid_path: return {"hanim.object.invalid-path", Kind::failed, {}, true};
    }
    return {"hanim.object.?", Kind::internal};
}

// Immutable geometry lease. copy() owns its data, including when the source
// ShapeContour spans a temporary vector. Open/move-only contours are valid
// representations; a particular morph plan decides whether it can use them.
class Contour {
public:
    Contour() noexcept = default;
    [[nodiscard]] static std::expected<Contour, ObjectError> copy(ShapeContour input, std::size_t max_segments = 4096);
    [[nodiscard]] explicit operator bool() const noexcept { return bool(data_); }
    [[nodiscard]] std::size_t segment_count() const noexcept;
    [[nodiscard]] std::expected<ShapeContour, ObjectError> view() const & noexcept;
    std::expected<ShapeContour, ObjectError> view() const && = delete;
private:
    std::shared_ptr<const detail::ContourData> data_;
};

struct PathImportLimits {
    std::size_t max_contours = 4096, max_segments = 65536;
    mlibs::geom::PathLimits source;
};
// Where an import failed: the contour. The path's own error is a cause.
struct PathImportWhere {
    std::size_t contour = std::numeric_limits<std::size_t>::max();
};
[[nodiscard]] std::string describe_where(const PathImportWhere& where);
using PathImportFailure = mlibs::error::Error<ObjectError, PathImportWhere>;
struct ImportedContour {
    Contour geometry;
    // Half-open command/operand ranges in the captured source. For an implicit
    // start after close, the starting coordinate is inherited from the prior
    // contour; first_point addresses the first explicit operand of this range.
    std::size_t first_command=0, end_command=0, first_point=0, point_count=0;
    std::vector<std::size_t> segment_commands;
};
struct ImportedPath { std::vector<ImportedContour> contours; };
// Own the actual line/quadratic/cubic controls and closure without polygonizing
// them or importing a renderer. Source indices preserve provenance; the host
// associates its own stable source id/revision. No input spans are retained.
[[nodiscard]] std::expected<ImportedPath,PathImportFailure>
import_path(mlibs::geom::PathView path,PathImportLimits limits = {});

// A part's outline drawn over its fill: its colour and width, in the part's
// units. How much of each contour it draws is a value of the part, the trim
// channels' (ch::trim_start, trim_end, trim_offset in hanim.library).
struct PartStroke {
    Rgba   color;
    double width = 1;
};
// Which of a part's points a deformer moves, and how much (0 to 1), in the
// part's own space (convergence plan §6.8 ④). Weighed once, on the geometry
// an animation starts from: a soft selection, not a per-frame search.
struct Selector {
    enum class Shape : std::uint8_t { all, near, within };
    Shape  shape = Shape::all;
    Vec2   center{};          // near: full weight here …
    double radius = 0;        // … none from this far
    Vec2   low{}, high{};     // within: full weight in this box …
    double feather = 0;       // … none from this far outside it
    Ease   falloff = ease::css_ease_in_out;  // the weight from the edge (0) inwards (1)

    [[nodiscard]] static Selector everything() noexcept { return {}; }
    [[nodiscard]] static Selector around(Vec2 center, double radius, Ease falloff = ease::css_ease_in_out) noexcept {
        return {.shape = Shape::near, .center = center, .radius = radius, .falloff = falloff};
    }
    [[nodiscard]] static Selector inside(Vec2 low, Vec2 high, double feather, Ease falloff = ease::css_ease_in_out) noexcept {
        return {.shape = Shape::within, .low = low, .high = high, .feather = feather, .falloff = falloff};
    }
    // The weight of `point`: 0 to 1.
    [[nodiscard]] double weight(Vec2 point) const noexcept;
};
// How a deformer moves its selected points by its channel's amount, each in
// proportion to its weight: displace by an offset (Vec2, rest {0, 0}), scale
// about `anchor` by factors (Vec2, rest {1, 1}), rotate about `anchor` by
// radians (double, rest 0).
enum class DeformKind : std::uint8_t { displace, scale, rotate };
// A deformer of a part: its geometry follows one of the part's value
// channels, so whatever animates that channel — a tween, a spring that
// overshoots, a drag — moves the selected points (hanim.library's
// deform::displace, scale, rotate make one from a typed channel).
struct PartDeformer {
    std::string channel;
    DeformKind  kind = DeformKind::displace;
    Selector    select;
    Vec2        anchor{};
};
// Explicit ids are stable host keys, never derived from displayed labels or
// drawing order. They are unique across an object; an empty parent is its root.
// A part with contours has a fill, a stroke or both.
struct PartDescription {
    std::string id;
    std::string parent;
    Transform2D local;
    std::vector<Contour> contours;
    std::optional<Rgba> fill;
    std::optional<PartStroke> stroke;
    std::vector<PartDeformer> deformers;
};
struct ObjectDescription { std::vector<PartDescription> parts; };

// One part made from a path (convergence plan §6.8 ①: the glyph and path
// adapter): its id, its parent (empty: the object's root), where it stands
// (`local`: a glyph's origin and size), its path, fill and stroke. A glyph's outline
// from mlibs.text (`outlines(paragraph, fonts)`) is such a path, in ems.
struct PathPart {
    std::string               id;
    std::string               parent;
    Transform2D               local;
    mlibs::geom::PathView     path;
    std::optional<Rgba>       fill;
    std::optional<PartStroke> stroke;
};
// An object of a root `root` and one part a path, each path's contours its
// part's (an empty path, a space, gives a part without contours). Every part
// keeps its id; the paths' commands are imported as they are (import_path).
[[nodiscard]] std::expected<ObjectDescription, PathImportFailure>
object_from_paths(std::string root, std::span<const PathPart> parts, PathImportLimits limits = {});

// max_objects bounds registry slots; all other limits apply per object.
// Referenced contours/segments count per occurrence, including shared assets.
struct ObjectLimits {
    std::size_t max_objects = 1024;
    std::size_t max_parts = 4096;
    std::size_t max_contours = 4096;
    std::size_t max_segments = 65536;
    std::size_t max_depth = 256;
};

struct Capability { std::string_view id; std::uint32_t version = 0; };
struct CapturedPart {
    PartRef ref;
    std::string id;
    std::optional<PartRef> parent;
    std::vector<PartRef> children;
    Transform2D local;
    std::vector<Contour> contours;
    std::optional<Rgba> fill;
    std::optional<PartStroke> stroke;
    std::vector<PartDeformer> deformers;
};

class ObjectRegistry;
class Snapshot {
public:
    Snapshot() noexcept = default;
    [[nodiscard]] explicit operator bool() const noexcept { return bool(data_); }
    [[nodiscard]] ObjectRef object() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] PartRef root() const noexcept;
    // All borrowed payload views require this owning Snapshot to stay alive.
    [[nodiscard]] std::span<const CapturedPart> parts() const & noexcept;
    std::span<const CapturedPart> parts() const && = delete;
    [[nodiscard]] std::expected<PartRef, ObjectError> find(std::string_view id) const noexcept;
    [[nodiscard]] std::expected<const CapturedPart*, ObjectError> get(PartRef part) const & noexcept;
    std::expected<const CapturedPart*, ObjectError> get(PartRef) const && = delete;
    [[nodiscard]] std::span<const Capability> capabilities() const & noexcept;
    std::span<const Capability> capabilities() const && = delete;
    [[nodiscard]] std::uint32_t capability_version(std::string_view id) const noexcept;
    [[nodiscard]] std::expected<void, ObjectError> require_capability(std::string_view id, std::uint32_t version = 1) const noexcept;
private:
    friend class ObjectRegistry;
    explicit Snapshot(std::shared_ptr<const detail::SnapshotData> data) noexcept : data_(std::move(data)) {}
    std::shared_ptr<const detail::SnapshotData> data_;
};

// Single owning thread (or externally synchronized). Snapshots/Contours may
// be queried concurrently after publication; a registry is not a concurrent
// mutation container. Move it only while quiescent. No clocks or event loop.
class ObjectRegistry {
public:
    ObjectRegistry() noexcept;
    ~ObjectRegistry();
    ObjectRegistry(ObjectRegistry&&) noexcept;
    ObjectRegistry& operator=(ObjectRegistry&&) noexcept;
    ObjectRegistry(const ObjectRegistry&) = delete;
    ObjectRegistry& operator=(const ObjectRegistry&) = delete;
    [[nodiscard]] static std::expected<ObjectRegistry, ObjectError> make(ObjectLimits limits = {});
    [[nodiscard]] std::uint64_t owner() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool contains(ObjectRef object) const noexcept;
    [[nodiscard]] bool contains(PartRef part) const noexcept;
    [[nodiscard]] std::expected<ObjectRef, ObjectError> create(const ObjectDescription& description);
    [[nodiscard]] std::expected<std::uint64_t, ObjectError> update(ObjectRef object, const ObjectDescription& description);
    [[nodiscard]] std::expected<void, ObjectError> destroy(ObjectRef object);
    [[nodiscard]] std::expected<Snapshot, ObjectError> capture(ObjectRef object) const noexcept;
    [[nodiscard]] std::expected<std::uint64_t, ObjectError> revision(ObjectRef object) const noexcept;
    [[nodiscard]] std::expected<PartRef, ObjectError> part(ObjectRef object, std::string_view id) const noexcept;
private:
    std::unique_ptr<detail::RegistryData> data_;
    bool writing_ = false;
};

} // namespace hanim
