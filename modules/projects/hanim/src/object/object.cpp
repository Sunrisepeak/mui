module hanim.object;

import std;
import mlibs.log;
import mlibs.trace;
import mlibs.memory;

namespace hanim::detail {
struct ContourData { Vec2 start; std::vector<ShapeSegment> segments; bool closed = true; };
struct SnapshotData {
    ObjectRef object;
    std::uint64_t revision = 0;
    PartRef root;
    std::vector<CapturedPart> parts;
    std::vector<std::uint32_t> by_slot, by_name;
    std::array<Capability, 4> capabilities;
    std::size_t capability_count = 0;
};
struct PartTag;
struct ObjectTag;
using PartKey = mlibs::memory::SlotKey<PartTag>;
using ObjectKey = mlibs::memory::SlotKey<ObjectTag>;
using PartIdentities = mlibs::memory::SlotStorage<std::string, PartKey>;
struct ObjectSlot {
    std::shared_ptr<const SnapshotData> current;
    PartIdentities parts;
};
struct RegistryData {
    std::uint64_t owner = 0;
    ObjectLimits limits;
    mlibs::memory::SlotStorage<ObjectSlot, ObjectKey> objects;
    RegistryData(std::uint64_t identity, ObjectLimits budget) : owner(identity), limits(budget), objects(budget.max_objects) {}
};
}

namespace hanim {
namespace {
constexpr mlibs::log::Log object_log{"hanim.object"};
constexpr auto exhausted = std::numeric_limits<std::uint64_t>::max();
std::atomic<std::uint64_t> next_owner{1};

bool finite(Vec2 value) noexcept { return std::isfinite(value.x) && std::isfinite(value.y); }
bool finite(const Transform2D& value) noexcept {
    return std::isfinite(value.tx) && std::isfinite(value.ty) && std::isfinite(value.sx) && std::isfinite(value.sy) &&
        std::isfinite(value.rotate) && std::isfinite(value.skew_x);
}
bool valid_color(Rgba value) noexcept {
    for (float lane : {value.r, value.g, value.b, value.a}) if (!std::isfinite(lane) || lane < 0 || lane > 1) return false;
    return true;
}
bool valid_id(std::string_view id) noexcept {
    return !id.empty() && id.size() <= 128 && std::ranges::none_of(id, [](unsigned char c) { return c < 32 || c == 127; });
}
bool valid_limits(ObjectLimits limits) noexcept {
    return limits.max_objects && limits.max_objects <= (1u << 20) && limits.max_parts && limits.max_parts <= (1u << 16) &&
        limits.max_contours && limits.max_contours <= (1u << 16) && limits.max_segments && limits.max_segments <= (1u << 20) &&
        limits.max_depth && limits.max_depth <= 1024;
}
void trace_ref(ObjectRef ref) noexcept {
    // Split identity counters: every uint32 is exactly representable by the
    // trace's double value, unlike a whole uint64 owner/generation.
    mlibs::trace::count("owner.high", static_cast<double>(ref.owner >> 32), "hanim.object");
    mlibs::trace::count("owner.low", static_cast<double>(static_cast<std::uint32_t>(ref.owner)), "hanim.object");
    mlibs::trace::count("object.slot", static_cast<double>(ref.slot), "hanim.object");
    mlibs::trace::count("generation.high", static_cast<double>(ref.generation >> 32), "hanim.object");
    mlibs::trace::count("generation.low", static_cast<double>(static_cast<std::uint32_t>(ref.generation)), "hanim.object");
}
void failed(std::string_view operation, ObjectError error, ObjectRef ref = {}) {
    object_log.warn("{} refused: {} (owner={}, slot={}, generation={})", operation, describe(error).name, ref.owner, ref.slot, ref.generation);
    mlibs::trace::mark("refused", "hanim.object");
}
struct WriteScope {
    bool& writing;
    explicit WriteScope(bool& value) noexcept : writing(value) { writing = true; }
    ~WriteScope() { writing = false; }
};
std::expected<detail::ObjectSlot*, ObjectError> resolve(detail::RegistryData* data, ObjectRef ref) noexcept {
    if (!data) return std::unexpected(ObjectError::uninitialized);
    if (!ref) return std::unexpected(ObjectError::invalid_object);
    if (ref.owner != data->owner) return std::unexpected(ObjectError::foreign_owner);
    if (ref.slot >= data->objects.slot_count()) return std::unexpected(ObjectError::invalid_object);
    const auto slot = data->objects.get({ref.slot, ref.generation});
    if (!slot) return std::unexpected(ObjectError::stale_object);
    return slot;
}
std::expected<PartRef, ObjectError> find_part(const detail::SnapshotData& data, std::string_view id) noexcept {
    const auto it = std::lower_bound(data.by_name.begin(), data.by_name.end(), id,
        [&](std::uint32_t index, std::string_view key) { return data.parts[index].id.compare(key) < 0; });
    if (it == data.by_name.end() || data.parts[*it].id != id) return std::unexpected(ObjectError::part_not_found);
    return data.parts[*it].ref;
}
std::expected<const CapturedPart*, ObjectError> get_part(const detail::SnapshotData& data, PartRef ref) noexcept {
    if (!ref) return std::unexpected(ObjectError::invalid_part);
    if (ref.object.owner != data.object.owner) return std::unexpected(ObjectError::foreign_owner);
    if (ref.object.slot != data.object.slot) return std::unexpected(ObjectError::different_object);
    if (ref.object.generation != data.object.generation) return std::unexpected(ObjectError::stale_object);
    if (ref.slot >= data.by_slot.size()) return std::unexpected(ObjectError::invalid_part);
    const auto dense = data.by_slot[ref.slot];
    if (dense == invalid_object_slot || data.parts[dense].ref.generation != ref.generation) return std::unexpected(ObjectError::stale_part);
    return &data.parts[dense];
}
struct PreparedObject {
    std::shared_ptr<const detail::SnapshotData> snapshot;
    detail::PartIdentities identities;
};
std::expected<PreparedObject, ObjectError> prepare(ObjectRef object, std::uint64_t revision, const ObjectDescription& description,
    ObjectLimits limits, const detail::ObjectSlot* previous) {
    const auto count = description.parts.size();
    if (!count) return std::unexpected(ObjectError::invalid_part);
    if (count > limits.max_parts) return std::unexpected(ObjectError::capacity);
    std::map<std::string_view, std::uint32_t, std::less<>> names;
    std::vector<std::uint32_t> parents(count, invalid_object_slot);
    std::size_t contours = 0, segments = 0, root_count = 0;
    std::uint32_t root = 0;
    bool any_fill = false;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& part = description.parts[i];
        if (!valid_id(part.id) || (!part.parent.empty() && !valid_id(part.parent))) return std::unexpected(ObjectError::invalid_id);
        if (!names.emplace(part.id, static_cast<std::uint32_t>(i)).second) return std::unexpected(ObjectError::duplicate_id);
        if (!finite(part.local)) return std::unexpected(ObjectError::invalid_transform);
        if (part.fill && !valid_color(*part.fill)) return std::unexpected(ObjectError::invalid_color);
        any_fill |= part.fill.has_value();
        if (part.contours.size() > limits.max_contours - contours) return std::unexpected(ObjectError::capacity);
        contours += part.contours.size();
        for (const auto& contour : part.contours) {
            if (!contour) return std::unexpected(ObjectError::invalid_resource);
            if (contour.segment_count() > limits.max_segments - segments) return std::unexpected(ObjectError::capacity);
            segments += contour.segment_count();
        }
    }
    for (std::size_t i = 0; i < count; ++i) {
        const auto& parent = description.parts[i].parent;
        if (parent.empty()) { ++root_count; root = static_cast<std::uint32_t>(i); continue; }
        const auto it = names.find(parent);
        if (it == names.end()) return std::unexpected(ObjectError::missing_parent);
        parents[i] = it->second;
    }
    if (!root_count) return std::unexpected(ObjectError::cycle);
    if (root_count != 1) return std::unexpected(ObjectError::multiple_roots);
    std::vector<std::uint8_t> state(count, 0);
    std::vector<std::size_t> depths(count, 0);
    std::vector<std::uint32_t> walk;
    walk.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto current = static_cast<std::uint32_t>(i);
        while (current != invalid_object_slot && state[current] != 2) {
            if (state[current] == 1) return std::unexpected(ObjectError::cycle);
            state[current] = 1; walk.push_back(current); current = parents[current];
        }
        auto depth = current == invalid_object_slot ? 0 : depths[current];
        while (!walk.empty()) {
            const auto index = walk.back(); walk.pop_back();
            if (++depth > limits.max_depth) return std::unexpected(ObjectError::depth_limit);
            depths[index] = depth; state[index] = 2;
        }
    }

    PreparedObject result{.identities = previous ? previous->parts : detail::PartIdentities(limits.max_parts)};
    std::map<std::string_view, detail::PartKey, std::less<>> old_names;
    if (previous) previous->parts.for_each([&](detail::PartKey key, const std::string& id) {
        if (names.contains(id)) old_names.emplace(id, key);
        else result.identities.erase(key);
    });
    auto data = std::make_shared<detail::SnapshotData>();
    data->object = object; data->revision = revision;
    data->parts.reserve(count); data->by_name.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& source = description.parts[i];
        detail::PartKey key;
        if (const auto old = old_names.find(source.id); old != old_names.end()) key = old->second;
        else {
            const auto inserted = result.identities.insert(source.id);
            if (!inserted) return std::unexpected(ObjectError::capacity);
            key = *inserted;
        }
        data->parts.push_back({.ref = {object, key.index, key.generation}, .id = source.id, .local = source.local,
            .contours = source.contours, .fill = source.fill});
        data->by_name.push_back(static_cast<std::uint32_t>(i));
    }
    data->by_slot.assign(result.identities.slot_count(), invalid_object_slot);
    for (std::size_t i = 0; i < count; ++i) {
        auto& part = data->parts[i]; data->by_slot[part.ref.slot] = static_cast<std::uint32_t>(i);
        if (parents[i] != invalid_object_slot) {
            part.parent = data->parts[parents[i]].ref;
            data->parts[parents[i]].children.push_back(part.ref);
        }
    }
    data->root = data->parts[root].ref;
    std::ranges::sort(data->by_name, [&](auto a, auto b) { return data->parts[a].id < data->parts[b].id; });
    data->capabilities[data->capability_count++] = {"hanim.parts", 1};
    data->capabilities[data->capability_count++] = {"hanim.local-transform2d", 1};
    if (contours) data->capabilities[data->capability_count++] = {"hanim.contours", 1};
    if (any_fill) data->capabilities[data->capability_count++] = {"hanim.solid-fill", 1};
    // Publish only const ownership; the mutable construction alias ends here.
    result.snapshot = std::move(data);
    return result;
}
}

std::expected<Contour, ObjectError> Contour::copy(ShapeContour input, std::size_t max_segments) {
    mlibs::trace::Span span("contour.copy", "hanim.object");
    auto refuse = [](ObjectError error) -> std::expected<Contour, ObjectError> { failed("contour.copy", error); return std::unexpected(error); };
    if (!max_segments || max_segments > 65536) return refuse(ObjectError::invalid_limits);
    if (input.segments.size() > max_segments) return refuse(ObjectError::capacity);
    if (!finite(input.start)) return refuse(ObjectError::invalid_coordinate);
    for (const auto& segment : input.segments) {
        if (segment.verb != ShapeVerb::line && segment.verb != ShapeVerb::quadratic && segment.verb != ShapeVerb::cubic) return refuse(ObjectError::invalid_verb);
        if (!finite(segment.end) || (segment.verb != ShapeVerb::line && !finite(segment.control1)) ||
            (segment.verb == ShapeVerb::cubic && !finite(segment.control2))) return refuse(ObjectError::invalid_coordinate);
    }
    Contour result;
    result.data_ = std::make_shared<const detail::ContourData>(detail::ContourData{input.start, {input.segments.begin(), input.segments.end()}, input.closed});
    mlibs::trace::count("contour.segments", static_cast<double>(input.segments.size()), "hanim.object");
    return result;
}
std::size_t Contour::segment_count() const noexcept { return data_ ? data_->segments.size() : 0; }
std::expected<ShapeContour, ObjectError> Contour::view() const & noexcept {
    if (!data_) return std::unexpected(ObjectError::invalid_resource);
    return ShapeContour{data_->start, data_->segments, data_->closed};
}

ObjectRef Snapshot::object() const noexcept { return data_ ? data_->object : ObjectRef{}; }
std::uint64_t Snapshot::revision() const noexcept { return data_ ? data_->revision : 0; }
PartRef Snapshot::root() const noexcept { return data_ ? data_->root : PartRef{}; }
std::span<const CapturedPart> Snapshot::parts() const & noexcept { return data_ ? std::span<const CapturedPart>(data_->parts) : std::span<const CapturedPart>{}; }
std::expected<PartRef, ObjectError> Snapshot::find(std::string_view id) const noexcept {
    if (!data_) return std::unexpected(ObjectError::uninitialized);
    return find_part(*data_, id);
}
std::expected<const CapturedPart*, ObjectError> Snapshot::get(PartRef ref) const & noexcept {
    if (!data_) return std::unexpected(ObjectError::uninitialized);
    return get_part(*data_, ref);
}
std::span<const Capability> Snapshot::capabilities() const & noexcept {
    return data_ ? std::span(data_->capabilities).first(data_->capability_count) : std::span<const Capability>{};
}
std::uint32_t Snapshot::capability_version(std::string_view id) const noexcept {
    for (const auto& capability : capabilities()) if (capability.id == id) return capability.version;
    return 0;
}
std::expected<void, ObjectError> Snapshot::require_capability(std::string_view id, std::uint32_t version) const noexcept {
    if (!data_) return std::unexpected(ObjectError::uninitialized);
    if (!version) return std::unexpected(ObjectError::invalid_limits);
    if (capability_version(id) < version) return std::unexpected(ObjectError::unsupported_capability);
    return {};
}

ObjectRegistry::ObjectRegistry() noexcept = default;
ObjectRegistry::~ObjectRegistry() = default;
ObjectRegistry::ObjectRegistry(ObjectRegistry&&) noexcept = default;
ObjectRegistry& ObjectRegistry::operator=(ObjectRegistry&&) noexcept = default;
std::expected<ObjectRegistry, ObjectError> ObjectRegistry::make(ObjectLimits limits) {
    if (!valid_limits(limits)) { failed("registry.make", ObjectError::invalid_limits); return std::unexpected(ObjectError::invalid_limits); }
    auto owner = next_owner.load(std::memory_order_relaxed);
    do {
        if (owner == exhausted) return std::unexpected(ObjectError::identity_exhausted);
    } while (!next_owner.compare_exchange_weak(owner, owner + 1, std::memory_order_relaxed));
    ObjectRegistry registry; registry.data_ = std::make_unique<detail::RegistryData>(owner, limits);
    return registry;
}
std::uint64_t ObjectRegistry::owner() const noexcept { return data_ ? data_->owner : 0; }
std::size_t ObjectRegistry::size() const noexcept { return data_ ? data_->objects.size() : 0; }
bool ObjectRegistry::contains(ObjectRef ref) const noexcept { return bool(resolve(data_.get(), ref)); }
bool ObjectRegistry::contains(PartRef ref) const noexcept {
    const auto object = resolve(data_.get(), ref.object);
    return object && bool(get_part(*(*object)->current, ref));
}
std::expected<ObjectRef, ObjectError> ObjectRegistry::create(const ObjectDescription& description) {
    if (writing_) return std::unexpected(ObjectError::reentrant_mutation);
    WriteScope write(writing_); mlibs::trace::Span span("create", "hanim.object");
    auto refuse = [&](ObjectError error) -> std::expected<ObjectRef, ObjectError> { failed("create", error); return std::unexpected(error); };
    if (!data_) return refuse(ObjectError::uninitialized);
    const auto inserted = data_->objects.try_insert([&](detail::ObjectKey key) -> std::expected<detail::ObjectSlot, ObjectError> {
        const ObjectRef candidate{data_->owner, key.index, key.generation};
        auto prepared = prepare(candidate, 1, description, data_->limits, nullptr);
        if (!prepared) return std::unexpected(prepared.error());
        return detail::ObjectSlot{std::move(prepared->snapshot), std::move(prepared->identities)};
    }, ObjectError::capacity);
    if (!inserted) return refuse(inserted.error());
    const ObjectRef ref{data_->owner, inserted->index, inserted->generation};
    trace_ref(ref); mlibs::trace::count("objects.live", static_cast<double>(data_->objects.size()), "hanim.object");
    object_log.info("created owner={} slot={} generation={} revision=1", ref.owner, ref.slot, ref.generation);
    return ref;
}
std::expected<std::uint64_t, ObjectError> ObjectRegistry::update(ObjectRef ref, const ObjectDescription& description) {
    if (writing_) return std::unexpected(ObjectError::reentrant_mutation);
    WriteScope write(writing_); mlibs::trace::Span span("update", "hanim.object");
    auto refuse = [&](ObjectError error) -> std::expected<std::uint64_t, ObjectError> { failed("update", error, ref); return std::unexpected(error); };
    const auto resolved = resolve(data_.get(), ref); if (!resolved) return refuse(resolved.error());
    auto& slot = **resolved;
    if (slot.current->revision == exhausted) return refuse(ObjectError::version_exhausted);
    const auto version = slot.current->revision + 1;
    auto prepared = prepare(ref, version, description, data_->limits, &slot); if (!prepared) return refuse(prepared.error());
    slot.current = std::move(prepared->snapshot); slot.parts.swap(prepared->identities);
    trace_ref(ref);
    object_log.info("updated owner={} slot={} generation={} revision={}", ref.owner, ref.slot, ref.generation, version);
    return version;
}
std::expected<void, ObjectError> ObjectRegistry::destroy(ObjectRef ref) {
    if (writing_) return std::unexpected(ObjectError::reentrant_mutation);
    WriteScope write(writing_); mlibs::trace::Span span("destroy", "hanim.object");
    const auto resolved = resolve(data_.get(), ref);
    if (!resolved) { failed("destroy", resolved.error(), ref); return std::unexpected(resolved.error()); }
    data_->objects.erase({ref.slot, ref.generation});
    trace_ref(ref);
    object_log.info("destroyed owner={} slot={} generation={}", ref.owner, ref.slot, ref.generation);
    return {};
}
std::expected<Snapshot, ObjectError> ObjectRegistry::capture(ObjectRef ref) const noexcept {
    mlibs::trace::Span span("capture", "hanim.object");
    const auto slot = resolve(data_.get(), ref); if (!slot) return std::unexpected(slot.error());
    trace_ref(ref);
    return Snapshot((*slot)->current);
}
std::expected<std::uint64_t, ObjectError> ObjectRegistry::revision(ObjectRef ref) const noexcept {
    const auto slot = resolve(data_.get(), ref); if (!slot) return std::unexpected(slot.error());
    return (*slot)->current->revision;
}
std::expected<PartRef, ObjectError> ObjectRegistry::part(ObjectRef ref, std::string_view id) const noexcept {
    const auto slot = resolve(data_.get(), ref); if (!slot) return std::unexpected(slot.error());
    return find_part(*(*slot)->current, id);
}

} // namespace hanim
