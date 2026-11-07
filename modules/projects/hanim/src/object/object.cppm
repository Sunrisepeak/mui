// HObject's first owned domain: hierarchical parts and actual vector contours.
export module hanim.object;

import std;
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
[[nodiscard]] std::string_view object_error_name(ObjectError error) noexcept;

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
struct PathImportFailure {
    ObjectError code;
    std::optional<mlibs::geom::PathFailure> path;
    std::size_t contour = std::numeric_limits<std::size_t>::max();
};
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

// Explicit ids are stable host keys, never derived from displayed labels or
// drawing order. They are unique across an object; an empty parent is its root.
struct PartDescription {
    std::string id;
    std::string parent;
    Transform2D local;
    std::vector<Contour> contours;
    std::optional<Rgba> fill;
};
struct ObjectDescription { std::vector<PartDescription> parts; };

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
