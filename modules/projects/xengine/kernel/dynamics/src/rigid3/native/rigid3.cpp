// The native unit of the 3D rigid family: the only one that sees Jolt (D-83).
module xengine.dynamics.rigid3;
import std;
import mcpplibs.mwrap.jolt;
import mlibs.hash;
import xengine.matter;

namespace xengine::dynamics::rigid3 {
namespace {

namespace jolt = mcpplibs::mwrap::jolt;
constexpr std::array<char, 4> magic{'X', 'R', 'G', '3'};
constexpr JPH::ObjectLayer still = 0, moving = 1;

// Once a process: Jolt's allocator, its factory and the types it reads shapes by.
void library() {
    static const bool registered = [] {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
        return true;
    }();
    (void)registered;
}

// Two object layers on two broad-phase layers: what does not move, what does; what does not move meets only
// what does. Made once and only read after (by every world, on every worker).
bool map_layers(JPH::BroadPhaseLayerInterfaceTable& broad, JPH::ObjectLayerPairFilterTable& pairs) {
    broad.MapObjectToBroadPhaseLayer(still, JPH::BroadPhaseLayer(0));
    broad.MapObjectToBroadPhaseLayer(moving, JPH::BroadPhaseLayer(1));
    pairs.EnableCollision(moving, still);
    pairs.EnableCollision(moving, moving);
    return true;
}
struct Layers {
    JPH::BroadPhaseLayerInterfaceTable broad{2, 2};
    JPH::ObjectLayerPairFilterTable pairs{2};
    bool mapped = map_layers(broad, pairs);
    JPH::ObjectVsBroadPhaseLayerFilterTable object_vs_broad{broad, 2, pairs, 2};
};
const Layers& layers() {
    static const Layers made;
    return made;
}

JPH::Vec3 vec(Vec value) noexcept { return {value.x, value.y, value.z}; }
Vec vec(JPH::Vec3 value) noexcept { return {value.GetX(), value.GetY(), value.GetZ()}; }
JPH::Quat turn(Turn value) noexcept {
    const Turn unit = mlibs::math::normalized(value);
    return {unit.x, unit.y, unit.z, unit.w};
}
Turn turn(JPH::Quat value) noexcept { return {value.GetX(), value.GetY(), value.GetZ(), value.GetW()}; }
std::string text(const JPH::String& value) { return {value.data(), value.size()}; }
bool finite(Vec value) noexcept { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }

std::expected<JPH::Ref<JPH::Shape>, Error> made(JPH::ShapeSettings::ShapeResult result) {
    if (result.HasError()) return std::unexpected(Error{Errc::shape, "Jolt refused the shape: " + text(result.GetError())});
    return result.Get();
}

// Jolt's shape for ours, of the material's density.
std::expected<JPH::Ref<JPH::Shape>, Error> shape_of(const Shape& shape, const Material& material, Kind kind) {
    if (!(material.density > 0) || !std::isfinite(material.density)) return std::unexpected(Error{Errc::shape, "a density above 0"});
    switch (shape.form) {
        case Shape::Form::box: {
            if (!(shape.half.x > 0 && shape.half.y > 0 && shape.half.z > 0) || !finite(shape.half)) return std::unexpected(Error{Errc::shape, "a box has half extents above 0"});
            // Jolt's rounding of the corners, no more than half the thinnest side.
            const float radius = std::min(jolt::cDefaultConvexRadius, 0.5f * std::min({shape.half.x, shape.half.y, shape.half.z}));
            JPH::BoxShapeSettings settings(vec(shape.half), radius);
            settings.mDensity = material.density;
            return made(settings.Create());
        }
        case Shape::Form::sphere: {
            if (!(shape.radius > 0) || !std::isfinite(shape.radius)) return std::unexpected(Error{Errc::shape, "a sphere has a radius above 0"});
            JPH::SphereShapeSettings settings(shape.radius);
            settings.mDensity = material.density;
            return made(settings.Create());
        }
        case Shape::Form::capsule: {
            if (!(shape.radius > 0 && shape.half_height > 0) || !std::isfinite(shape.radius) || !std::isfinite(shape.half_height))
                return std::unexpected(Error{Errc::shape, "a capsule has a radius and a half height above 0"});
            JPH::CapsuleShapeSettings settings(shape.half_height, shape.radius);
            settings.mDensity = material.density;
            return made(settings.Create());
        }
        case Shape::Form::hull: {
            if (shape.points.size() < 4 || !std::ranges::all_of(shape.points, finite)) return std::unexpected(Error{Errc::shape, "a hull wraps 4 points or more"});
            JPH::Array<JPH::Vec3> points;
            for (const Vec point : shape.points) points.push_back(vec(point));
            JPH::ConvexHullShapeSettings settings(points);
            settings.mDensity = material.density;
            return made(settings.Create());
        }
        case Shape::Form::mesh: {
            if (kind != Kind::fixed) return std::unexpected(Error{Errc::shape, "a mesh is a fixed body's shape (terrain, buildings): a moving one is a hull"});
            if (shape.corners.empty() || shape.corners.size() % 3 != 0) return std::unexpected(Error{Errc::shape, "a mesh has three corners a triangle"});
            if (!std::ranges::all_of(shape.points, finite)) return std::unexpected(Error{Errc::shape, "a mesh's points are finite"});
            JPH::VertexList points;
            for (const Vec point : shape.points) points.push_back(JPH::Float3(point.x, point.y, point.z));
            JPH::IndexedTriangleList triangles;
            for (std::size_t index = 0; index < shape.corners.size(); index += 3) {
                if (std::max({shape.corners[index], shape.corners[index + 1], shape.corners[index + 2]}) >= shape.points.size())
                    return std::unexpected(Error{Errc::shape, std::format("triangle {} names a corner past the mesh's {} points", index / 3, shape.points.size())});
                triangles.push_back(JPH::IndexedTriangle(shape.corners[index], shape.corners[index + 1], shape.corners[index + 2]));
            }
            JPH::MeshShapeSettings settings(std::move(points), std::move(triangles));
            return made(settings.Create());
        }
    }
    return std::unexpected(Error{Errc::shape, "an unknown form"});
}

// A shape as bytes (Jolt's binary form, with its children) and back.
std::string shape_bytes(const JPH::Shape& shape) {
    std::stringstream stream;
    JPH::StreamOutWrapper out(stream);
    JPH::Shape::ShapeToIDMap shapes;
    JPH::Shape::MaterialToIDMap materials;
    shape.SaveWithChildren(out, shapes, materials);
    return std::move(stream).str();
}
std::expected<JPH::Ref<JPH::Shape>, Error> shape_from(std::string_view bytes) {
    std::stringstream stream{std::string(bytes)};
    JPH::StreamInWrapper in(stream);
    JPH::Shape::IDToShapeMap shapes;
    JPH::Shape::IDToMaterialMap materials;
    JPH::Shape::ShapeResult result = JPH::Shape::sRestoreWithChildren(in, shapes, materials);
    if (result.HasError()) return std::unexpected(Error{Errc::state, "a saved shape Jolt could not read: " + text(result.GetError())});
    return result.Get();
}

JPH::EMotionType motion_of(Kind kind) noexcept {
    return kind == Kind::fixed ? JPH::EMotionType::Static : kind == Kind::kinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic;
}
JPH::BodyCreationSettings settings_of(const JPH::Shape* shape, Kind kind, const BodyDef& body, const Material& material) {
    JPH::BodyCreationSettings settings(shape, vec(body.at), turn(body.turn), motion_of(kind), kind == Kind::fixed ? still : moving);
    settings.mFriction = material.friction;
    settings.mRestitution = material.restitution;
    settings.mLinearVelocity = vec(body.velocity);
    settings.mAngularVelocity = vec(body.spin);
    settings.mMotionQuality = body.bullet ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
    return settings;
}

std::unique_ptr<JPH::JobSystem> jobs_for(int workers) {
    if (workers <= 0) return std::make_unique<JPH::JobSystemSingleThreaded>(jolt::cMaxPhysicsJobs);
    return std::make_unique<JPH::JobSystemThreadPool>(jolt::cMaxPhysicsJobs, jolt::cMaxPhysicsBarriers, workers);
}

// What touched what, as Jolt says it during a step (from any worker), by Jolt's ids: gathered under a lock; the
// world names and orders them after.
class Listener final : public JPH::ContactListener {
public:
    void OnContactAdded(const JPH::Body& one, const JPH::Body& other, const JPH::ContactManifold& manifold, JPH::ContactSettings&) override {
        const JPH::Vec3 point = manifold.GetWorldSpaceContactPointOn1(0);
        const JPH::Vec3 normal = manifold.mWorldSpaceNormal;  // from `one` toward `other`
        const float approach = (one.GetPointVelocity(point) - other.GetPointVelocity(point)).Dot(normal);
        const std::scoped_lock hold(lock_);
        found_.push_back({{one.GetID().GetIndexAndSequenceNumber()}, {other.GetID().GetIndexAndSequenceNumber()}, true, vec(point), vec(normal), approach});
    }
    void OnContactRemoved(const JPH::SubShapeIDPair& pair) override {
        const std::scoped_lock hold(lock_);
        found_.push_back({{pair.GetBody1ID().GetIndexAndSequenceNumber()}, {pair.GetBody2ID().GetIndexAndSequenceNumber()}, false, {}, {}, 0});
    }
    void take(std::vector<Contact>& out) {
        out.swap(found_);
        found_.clear();
    }

private:
    std::mutex lock_;
    std::vector<Contact> found_;
};

// How a body was made: what restoring an image needs to make it again under the same id.
struct Record {
    std::uint32_t native = 0;  // Jolt's id: an index and a sequence number
    Kind kind = Kind::moving;
    bool bullet = false;
    Material material;
    std::string shape;  // the shape's bytes
};

}  // namespace

struct World::Native {
    std::unique_ptr<JPH::TempAllocator> temp = std::make_unique<JPH::TempAllocatorImplWithMallocFallback>(4 * 1024 * 1024);
    std::unique_ptr<JPH::JobSystem> jobs;
    std::unique_ptr<JPH::PhysicsSystem> system = std::make_unique<JPH::PhysicsSystem>();
    Listener listener;
    std::map<BodyId, Record> records;
    std::unordered_map<std::uint32_t, BodyId> ours;  // Jolt's id → the world's
    std::uint32_t next = 1;                          // the next body's id
    std::vector<Contact> found;                      // a step's, by Jolt's ids
    [[nodiscard]] JPH::BodyID native(BodyId body) const noexcept {
        const auto record = records.find(body);
        return record == records.end() ? JPH::BodyID() : JPH::BodyID(record->second.native);
    }
    [[nodiscard]] BodyId named(JPH::BodyID body) const noexcept {
        const auto found_body = ours.find(body.GetIndexAndSequenceNumber());
        return found_body == ours.end() ? BodyId{} : found_body->second;
    }
    [[nodiscard]] JPH::BodyInterface& bodies() noexcept { return system->GetBodyInterfaceNoLock(); }
    [[nodiscard]] const JPH::BodyInterface& bodies() const noexcept { return system->GetBodyInterfaceNoLock(); }
    void remember(BodyId body, Record record) {
        ours.insert_or_assign(record.native, body);
        records.insert_or_assign(body, std::move(record));
    }
    void destroy(BodyId body) {
        const JPH::BodyID id = native(body);
        bodies().RemoveBody(id);
        bodies().DestroyBody(id);
        ours.erase(id.GetIndexAndSequenceNumber());
        records.erase(body);
    }
};

World::World(World&&) noexcept = default;
World& World::operator=(World&&) noexcept = default;
World::~World() = default;

Material material_of(const xengine::matter::Program& matter, xengine::matter::MaterialId material, Material base) noexcept {
    if (material >= matter.materials().size()) return base;
    const auto read = [&](std::string_view column, float otherwise) {
        const auto index = matter.column(column);
        return index ? static_cast<float>(matter.number(material, *index)) : otherwise;
    };
    base.density = read("density", base.density);
    base.friction = read("friction", base.friction);
    base.restitution = read("restitution", base.restitution);
    return base;
}

std::expected<World, Error> World::make(const Options& options) {
    if (options.collision_steps < 1 || !(options.step > 0) || !finite(options.gravity) || options.most_bodies < 1)
        return std::unexpected(Error{Errc::state, "a step is longer than 0 and has a collision step at least; gravity is finite; a world holds a body"});
    library();
    World world;
    world.options_ = options;
    world.native_ = std::make_unique<Native>();
    world.native_->jobs = jobs_for(options.workers);
    const Layers& tables = layers();
    world.native_->system->Init(options.most_bodies, 0, options.most_bodies, options.most_bodies, tables.broad, tables.object_vs_broad, tables.pairs);
    world.native_->system->SetGravity(vec(options.gravity));
    world.native_->system->SetContactListener(&world.native_->listener);
    return world;
}

std::expected<BodyId, Error> World::add(const BodyDef& body, const Shape& shape, const Material& material) {
    if (!finite(body.at) || !finite(body.velocity) || !finite(body.spin)) return std::unexpected(Error{Errc::body, "a body's place and velocities are finite"});
    auto native_shape = shape_of(shape, material, body.kind);
    if (!native_shape) return std::unexpected(native_shape.error());
    JPH::Body* made_body = native_->bodies().CreateBody(settings_of(native_shape->GetPtr(), body.kind, body, material));
    if (!made_body) return std::unexpected(Error{Errc::limit, std::format("the world holds {} bodies at most", options_.most_bodies)});
    native_->bodies().AddBody(made_body->GetID(), body.kind == Kind::fixed ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
    const BodyId id{native_->next++};
    native_->remember(id, Record{made_body->GetID().GetIndexAndSequenceNumber(), body.kind, body.bullet, material, shape_bytes(**native_shape)});
    bodies_.push_back(id);
    return id;
}

void World::remove(BodyId body) noexcept {
    if (!has(body)) return;
    native_->destroy(body);
    std::erase(bodies_, body);
}

bool World::has(BodyId body) const noexcept { return native_->records.contains(body); }

std::optional<Pose> World::pose(BodyId body) const noexcept {
    if (!has(body)) return std::nullopt;
    const JPH::BodyLockRead lock(native_->system->GetBodyLockInterfaceNoLock(), native_->native(body));
    if (!lock.Succeeded()) return std::nullopt;
    const JPH::Body& found = lock.GetBody();
    const JPH::MotionProperties* motion = found.IsStatic() ? nullptr : found.GetMotionProperties();
    const float inverse_mass = motion ? motion->GetInverseMass() : 0.0f;
    return Pose{vec(found.GetPosition()),          turn(found.GetRotation()), vec(found.GetLinearVelocity()), vec(found.GetAngularVelocity()), found.IsActive(),
                vec(found.GetCenterOfMassPosition()), inverse_mass > 0 ? 1.0f / inverse_mass : 0.0f};
}

std::span<const BodyId> World::bodies() const noexcept { return bodies_; }

void World::push(BodyId body, Vec impulse, std::optional<Vec> at) noexcept {
    if (!has(body) || !finite(impulse) || (at && !finite(*at))) return;
    if (at) native_->bodies().AddImpulse(native_->native(body), vec(impulse), vec(*at));
    else native_->bodies().AddImpulse(native_->native(body), vec(impulse));
}

void World::set_velocity(BodyId body, Vec velocity, Vec spin) noexcept {
    if (!has(body) || !finite(velocity) || !finite(spin)) return;
    native_->bodies().SetLinearAndAngularVelocity(native_->native(body), vec(velocity), vec(spin));
}

void World::step() noexcept {
    (void)native_->system->Update(options_.step, options_.collision_steps, native_->temp.get(), native_->jobs.get());
    ++steps_;
    native_->listener.take(native_->found);
    // Named by the world's ids, `one` the lower; in their order, each pair once a kind: the same on every run and
    // with any number of workers. A contact of a body removed since is not the world's any more.
    contacts_.clear();
    for (Contact contact : native_->found) {
        contact.one = native_->named(JPH::BodyID(contact.one.value));
        contact.other = native_->named(JPH::BodyID(contact.other.value));
        if (!contact.one || !contact.other) continue;
        if (contact.other < contact.one) std::swap(contact.one, contact.other), contact.normal = -contact.normal;
        contacts_.push_back(contact);
    }
    std::ranges::sort(contacts_, [](const Contact& one, const Contact& other) {
        return std::tie(one.one, one.other, other.began) < std::tie(other.one, other.other, one.began);
    });
    const auto repeated = std::ranges::unique(contacts_, [](const Contact& one, const Contact& other) {
        return one.one == other.one && one.other == other.other && one.began == other.began;
    });
    contacts_.erase(repeated.begin(), repeated.end());
}

std::optional<RayHit> World::ray(Vec from, Vec to) const {
    if (!finite(from) || !finite(to)) return std::nullopt;
    const JPH::RRayCast cast{vec(from), vec(to - from)};
    JPH::RayCastResult hit;
    if (!native_->system->GetNarrowPhaseQuery().CastRay(cast, hit)) return std::nullopt;
    const JPH::Vec3 point = cast.GetPointOnRay(hit.mFraction);
    const JPH::BodyLockRead lock(native_->system->GetBodyLockInterfaceNoLock(), hit.mBodyID);
    const Vec normal = lock.Succeeded() ? vec(lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, point)) : Vec{};
    return RayHit{native_->named(hit.mBodyID), hit.mFraction, vec(point), normal};
}

void World::overlapping(const Shape& shape, Vec at, Turn turned, std::vector<BodyId>& out) const {
    if (!finite(at)) return;
    const auto query = shape_of(shape, {}, Kind::fixed);
    if (!query) return;
    const JPH::Quat rotation = turn(turned);
    const JPH::Mat44 centre = JPH::Mat44::sRotationTranslation(rotation, vec(at) + rotation * (*query)->GetCenterOfMass());
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> hits;
    native_->system->GetNarrowPhaseQuery().CollideShape(query->GetPtr(), JPH::Vec3::sOne(), centre, JPH::CollideShapeSettings(), JPH::Vec3::sZero(), hits);
    const std::size_t first = out.size();
    for (const JPH::CollideShapeResult& hit : hits.mHits) out.push_back(native_->named(hit.mBodyID2));
    std::sort(out.begin() + static_cast<std::ptrdiff_t>(first), out.end());
    out.erase(std::unique(out.begin() + static_cast<std::ptrdiff_t>(first), out.end()), out.end());
}

// Saved: the magic, the step count and the bodies in the order made, each with how it was made; then Jolt's
// state of them under its length; the whole after the head under a checksum. Nothing reaches Jolt that is not,
// whole, what was written here: an image cut short or damaged is refused with the world as it was.
namespace {
struct Writer {
    std::vector<std::uint8_t> bytes;
    // Little-endian, a byte at a time: the same image on every platform.
    void put(std::uint64_t value, int count) {
        for (int index = 0; index < count; ++index) bytes.push_back(static_cast<std::uint8_t>(value >> (8 * index)));
    }
    void put(float value) { put(std::bit_cast<std::uint32_t>(value), 4); }
    void put(std::string_view block) {
        put(block.size(), 8);
        bytes.insert(bytes.end(), block.begin(), block.end());
    }
};
struct Reader {
    std::span<const std::uint8_t> bytes;
    std::size_t at = 0;
    std::optional<std::uint64_t> take(int count) {
        if (bytes.size() - at < static_cast<std::size_t>(count)) return std::nullopt;
        std::uint64_t value = 0;
        for (int index = 0; index < count; ++index) value |= static_cast<std::uint64_t>(bytes[at + static_cast<std::size_t>(index)]) << (8 * index);
        at += static_cast<std::size_t>(count);
        return value;
    }
    std::optional<float> number() {
        const auto bits = take(4);
        return bits ? std::optional(std::bit_cast<float>(static_cast<std::uint32_t>(*bits))) : std::nullopt;
    }
    std::optional<std::string_view> block() {
        const auto size = take(8);
        if (!size || bytes.size() - at < *size) return std::nullopt;
        const std::string_view out(reinterpret_cast<const char*>(bytes.data() + at), *size);
        at += *size;
        return out;
    }
};
constexpr std::size_t head_size = magic.size() + 8 + 8;  // the magic, the steps, the checksum of the rest
std::uint64_t sum_of(std::span<const std::uint8_t> bytes) noexcept { return mlibs::hash::Fnv1a().bytes(bytes).value(); }

struct Image {
    std::uint64_t steps = 0;
    std::uint32_t next = 1;
    std::vector<std::pair<BodyId, Record>> bodies;
    std::string_view state;
};
std::expected<Image, Error> parse(std::span<const std::uint8_t> saved) {
    const auto cut = std::unexpected(Error{Errc::state, "a saved 3D rigid world cut short"});
    if (saved.size() < magic.size() || !std::equal(magic.begin(), magic.end(), saved.begin())) return std::unexpected(Error{Errc::state, "not a saved 3D rigid world"});
    Reader reader{saved, magic.size()};
    const auto steps = reader.take(8), sum = reader.take(8);
    if (!steps || !sum) return cut;
    if (sum_of(saved.subspan(head_size)) != *sum) return std::unexpected(Error{Errc::state, "a saved 3D rigid world cut short or damaged"});
    Image image{.steps = *steps};
    const auto count = reader.take(8), next = reader.take(4);
    if (!count || !next || *count > saved.size()) return cut;
    image.next = static_cast<std::uint32_t>(*next);
    for (std::uint64_t index = 0; index < *count; ++index) {
        const auto id = reader.take(4), native = reader.take(4), kind = reader.take(1), bullet = reader.take(1);
        const auto density = reader.number(), friction = reader.number(), restitution = reader.number();
        const auto shape = reader.block();
        if (!id || !native || !kind || !bullet || !density || !friction || !restitution || !shape || *kind > 2) return cut;
        image.bodies.push_back({BodyId{static_cast<std::uint32_t>(*id)}, Record{static_cast<std::uint32_t>(*native), static_cast<Kind>(*kind), *bullet != 0,
                                                                                 {*density, *friction, *restitution}, std::string(*shape)}});
    }
    const auto state = reader.block();
    if (!state || reader.at != saved.size()) return cut;
    image.state = *state;
    return image;
}
}  // namespace

std::vector<std::uint8_t> World::save() const {
    Writer writer;
    for (const char letter : magic) writer.bytes.push_back(static_cast<std::uint8_t>(letter));
    writer.put(steps_, 8);
    writer.put(0, 8);  // the checksum, below
    writer.put(bodies_.size(), 8);
    writer.put(native_->next, 4);
    for (const BodyId body : bodies_) {
        const Record& record = native_->records.at(body);
        writer.put(body.value, 4);
        writer.put(record.native, 4);
        writer.put(static_cast<std::uint8_t>(record.kind), 1);
        writer.put(record.bullet ? 1 : 0, 1);
        writer.put(record.material.density), writer.put(record.material.friction), writer.put(record.material.restitution);
        writer.put(record.shape);
    }
    JPH::StateRecorderImpl state;
    native_->system->SaveState(state);
    writer.put(state.GetData());
    const std::uint64_t sum = sum_of(std::span(writer.bytes).subspan(head_size));
    for (int index = 0; index < 8; ++index) writer.bytes[magic.size() + 8 + static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(sum >> (8 * index));
    return std::move(writer.bytes);
}

std::expected<void, Error> World::restore(std::span<const std::uint8_t> saved) {
    auto image = parse(saved);
    if (!image) return std::unexpected(image.error());
    // Every shape read before the world is touched: one Jolt cannot read refuses the image whole.
    std::vector<JPH::Ref<JPH::Shape>> shapes;
    for (const auto& [body, record] : image->bodies) {
        if (has(body) && native_->records.at(body).native == record.native) {
            shapes.emplace_back();
            continue;
        }
        auto shape = shape_from(record.shape);
        if (!shape) return std::unexpected(shape.error());
        shapes.push_back(*shape);
    }
    // The bodies made since go (and one an id of the image names that is not the image's: another branch's); those
    // removed since come back under their ids.
    for (const BodyId body : std::vector(bodies_)) {
        const std::uint32_t native = native_->records.at(body).native;
        if (std::ranges::none_of(image->bodies, [&](const auto& entry) { return entry.first == body && entry.second.native == native; })) native_->destroy(body);
    }
    for (std::size_t index = 0; index < image->bodies.size(); ++index) {
        const auto& [body, record] = image->bodies[index];
        if (has(body)) continue;  // the same body: kept above
        JPH::Body* made_body = native_->bodies().CreateBodyWithID(JPH::BodyID(record.native), settings_of(shapes[index].GetPtr(), record.kind, {.bullet = record.bullet}, record.material));
        if (!made_body) return std::unexpected(Error{Errc::limit, std::format("body {}'s place is taken or past the world's {} bodies", body.value, options_.most_bodies)});
        native_->bodies().AddBody(made_body->GetID(), record.kind == Kind::fixed ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
        native_->remember(body, record);
    }
    JPH::StateRecorderImpl state;
    state.WriteBytes(image->state.data(), image->state.size());
    if (!native_->system->RestoreState(state)) return std::unexpected(Error{Errc::state, "Jolt refused the state (another version's)"});
    bodies_.clear();
    for (const auto& entry : image->bodies) bodies_.push_back(entry.first);
    steps_ = image->steps;
    native_->next = image->next;
    contacts_.clear();
    return {};
}

std::expected<World, Error> World::from(std::span<const std::uint8_t> saved, const Options& options) {
    auto world = make(options);
    if (!world) return world;
    if (auto restored = world->restore(saved); !restored) return std::unexpected(restored.error());
    return world;
}

std::expected<World, Error> World::branch() const { return from(save(), options_); }

std::uint64_t World::state_hash() const noexcept {
    mlibs::hash::Fnv1a hash;
    const auto mix = [&](float value) { hash.le(std::bit_cast<std::uint32_t>(value), 4); };
    const JPH::BodyInterface& bodies = native_->bodies();
    for (const BodyId body : bodies_) {
        const JPH::BodyID native = native_->native(body);
        const JPH::Vec3 at = bodies.GetPosition(native), velocity = bodies.GetLinearVelocity(native), spin = bodies.GetAngularVelocity(native);
        const JPH::Quat rotation = bodies.GetRotation(native);
        for (const float value : {at.GetX(), at.GetY(), at.GetZ(), rotation.GetX(), rotation.GetY(), rotation.GetZ(), rotation.GetW(), velocity.GetX(), velocity.GetY(),
                                  velocity.GetZ(), spin.GetX(), spin.GetY(), spin.GetZ()})
            mix(value);
    }
    return hash.value();
}

struct Direct::Native {
    std::unique_ptr<JPH::TempAllocator> temp = std::make_unique<JPH::TempAllocatorImplWithMallocFallback>(4 * 1024 * 1024);
    std::unique_ptr<JPH::JobSystem> jobs;
    std::unique_ptr<JPH::PhysicsSystem> system = std::make_unique<JPH::PhysicsSystem>();
    std::vector<JPH::BodyID> bodies;
    Options options;
};

Direct::Direct(Direct&&) noexcept = default;
Direct& Direct::operator=(Direct&&) noexcept = default;
Direct::~Direct() = default;

std::expected<Direct, Error> Direct::make(std::span<const Piece> pieces, const Options& options) {
    if (options.collision_steps < 1 || !(options.step > 0)) return std::unexpected(Error{Errc::state, "a step is longer than 0 and has a collision step at least"});
    library();
    Direct direct;
    direct.native_ = std::make_unique<Native>();
    direct.native_->options = options;
    direct.native_->jobs = jobs_for(options.workers);
    const Layers& tables = layers();
    direct.native_->system->Init(options.most_bodies, 0, options.most_bodies, options.most_bodies, tables.broad, tables.object_vs_broad, tables.pairs);
    direct.native_->system->SetGravity(vec(options.gravity));
    JPH::BodyInterface& bodies = direct.native_->system->GetBodyInterfaceNoLock();
    for (const Piece& piece : pieces) {
        auto shape = shape_of(piece.shape, piece.material, piece.body.kind);
        if (!shape) return std::unexpected(shape.error());
        const JPH::BodyID made_body = bodies.CreateAndAddBody(settings_of(shape->GetPtr(), piece.body.kind, piece.body, piece.material),
                                                              piece.body.kind == Kind::fixed ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
        if (made_body.IsInvalid()) return std::unexpected(Error{Errc::limit, "the world could not make another body"});
        direct.native_->bodies.push_back(made_body);
    }
    return direct;
}

void Direct::step() noexcept { (void)native_->system->Update(native_->options.step, native_->options.collision_steps, native_->temp.get(), native_->jobs.get()); }

Vec Direct::at(std::size_t index) const noexcept {
    return index < native_->bodies.size() ? vec(native_->system->GetBodyInterfaceNoLock().GetPosition(native_->bodies[index])) : Vec{};
}

}  // namespace xengine::dynamics::rigid3
