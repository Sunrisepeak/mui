// The native unit of the rigid family: the only one that sees Box2D (D-83).
module xengine.dynamics.rigid;
import std;
import mcpplibs.mwrap.box2d;
import mcpplibs.mwrap.box2d.macros;
import mlibs.hash;

namespace xengine::dynamics::rigid {
namespace {
constexpr std::array<char, 4> magic{'X', 'R', 'G', '1'};
b2Vec2 vec(Vec v) noexcept { return {v.x, v.y}; }
Vec vec(b2Vec2 v) noexcept { return {v.x, v.y}; }
}  // namespace

struct World::Native {
    b2WorldId world{};
    ~Native() {
        if (b2World_IsValid(world)) b2DestroyWorld(world);
    }
    [[nodiscard]] b2BodyId native(BodyId body) const noexcept { return {body.index, static_cast<std::uint16_t>(world.index1 - 1), body.generation}; }
};

World::World(World&&) noexcept = default;
World& World::operator=(World&&) noexcept = default;
World::~World() = default;

std::expected<World, Error> World::make(const Options& options) {
    if (options.substeps < 1 || !(options.step > 0)) return std::unexpected(Error{Errc::state, "a step is longer than 0 and has a substep at least"});
    b2WorldDef def = b2DefaultWorldDef();
    def.gravity = vec(options.gravity);
    World world;
    world.options_ = options;
    world.native_ = std::make_unique<Native>();
    world.native_->world = b2CreateWorld(&def);
    if (!b2World_IsValid(world.native_->world)) return std::unexpected(Error{Errc::limit, "Box2D has no room for another world"});
    return world;
}

namespace {
std::expected<void, Error> attach_to(b2BodyId body, const Shape& shape, const Material& material) {
    b2ShapeDef def = b2DefaultShapeDef();
    def.density = material.density;
    def.material.friction = material.friction;
    def.material.restitution = material.restitution;
    def.enableHitEvents = true;
    switch (shape.form) {
        case Shape::Form::box: {
            if (!(shape.half.x > 0) || !(shape.half.y > 0)) return std::unexpected(Error{Errc::shape, "a box has half extents above 0"});
            const b2Polygon box = b2MakeOffsetBox(shape.half.x, shape.half.y, vec(shape.centre), b2MakeRot(shape.angle));
            b2CreatePolygonShape(body, &def, &box);
            return {};
        }
        case Shape::Form::circle: {
            if (!(shape.radius > 0)) return std::unexpected(Error{Errc::shape, "a circle has a radius above 0"});
            const b2Circle circle{vec(shape.centre), shape.radius};
            b2CreateCircleShape(body, &def, &circle);
            return {};
        }
        case Shape::Form::polygon: {
            if (shape.points.size() < 3 || shape.points.size() > 8) return std::unexpected(Error{Errc::shape, "a polygon has 3 to 8 points"});
            std::array<b2Vec2, 8> points{};
            for (std::size_t index = 0; index < shape.points.size(); ++index) points[index] = vec(shape.points[index]);
            const b2Hull hull = b2ComputeHull(points.data(), static_cast<int>(shape.points.size()));
            if (hull.count == 0) return std::unexpected(Error{Errc::shape, "the points make no convex polygon (collinear, or too close together)"});
            const b2Polygon polygon = b2MakePolygon(&hull, shape.radius);
            b2CreatePolygonShape(body, &def, &polygon);
            return {};
        }
        case Shape::Form::segment: {
            if (shape.points.size() != 2) return std::unexpected(Error{Errc::shape, "a segment has 2 points"});
            const b2Segment segment{vec(shape.points[0]), vec(shape.points[1])};
            b2CreateSegmentShape(body, &def, &segment);
            return {};
        }
    }
    return std::unexpected(Error{Errc::shape, "an unknown form"});
}
}  // namespace

std::expected<BodyId, Error> World::add(const BodyDef& body, const Shape& shape, const Material& material) {
    b2BodyDef def = b2DefaultBodyDef();
    def.type = body.kind == Kind::fixed ? b2_staticBody : body.kind == Kind::kinematic ? b2_kinematicBody : b2_dynamicBody;
    def.position = vec(body.at);
    def.rotation = b2MakeRot(body.angle);
    def.linearVelocity = vec(body.velocity);
    def.angularVelocity = body.spin;
    def.isBullet = body.bullet;
    def.motionLocks.angularZ = body.fixed_rotation;
    const b2BodyId made = b2CreateBody(native_->world, &def);
    if (!b2Body_IsValid(made)) return std::unexpected(Error{Errc::limit, "the world could not make another body"});
    if (auto attached = attach_to(made, shape, material); !attached) {
        b2DestroyBody(made);
        return std::unexpected(attached.error());
    }
    const BodyId id{made.index1, made.generation};
    bodies_.push_back(id);
    return id;
}

std::expected<void, Error> World::attach(BodyId body, const Shape& shape, const Material& material) {
    if (!has(body)) return std::unexpected(Error{Errc::body, "no such body"});
    return attach_to(native_->native(body), shape, material);
}

void World::remove(BodyId body) noexcept {
    if (!has(body)) return;
    b2DestroyBody(native_->native(body));
    std::erase(bodies_, body);
}

bool World::has(BodyId body) const noexcept { return body && b2Body_IsValid(native_->native(body)); }

std::optional<Pose> World::pose(BodyId body) const noexcept {
    if (!has(body)) return std::nullopt;
    const b2BodyId native = native_->native(body);
    const b2Rot rotation = b2Body_GetRotation(native);
    return Pose{vec(b2Body_GetPosition(native)), b2Rot_GetAngle(rotation), vec(b2Body_GetLinearVelocity(native)), b2Body_GetAngularVelocity(native),
                b2Body_IsAwake(native), {rotation.c, rotation.s}, vec(b2Body_GetWorldCenter(native)), b2Body_GetMass(native)};
}

std::vector<BodyId> World::bodies() const { return bodies_; }

void World::push(BodyId body, Vec impulse, std::optional<Vec> at) noexcept {
    if (!has(body)) return;
    if (at) b2Body_ApplyLinearImpulse(native_->native(body), vec(impulse), vec(*at), true);
    else b2Body_ApplyLinearImpulseToCenter(native_->native(body), vec(impulse), true);
}

void World::set_velocity(BodyId body, Vec velocity, float spin) noexcept {
    if (!has(body)) return;
    b2Body_SetLinearVelocity(native_->native(body), vec(velocity));
    b2Body_SetAngularVelocity(native_->native(body), spin);
}

void World::apply(BodyId body, Vec impulse, float angular) noexcept {
    if (!has(body)) return;
    const b2BodyId native = native_->native(body);
    if (impulse.x != 0 || impulse.y != 0) b2Body_ApplyLinearImpulseToCenter(native, vec(impulse), true);
    if (angular != 0) b2Body_ApplyAngularImpulse(native, angular, true);
}

std::expected<void, Error> World::reshape(BodyId body, std::span<const Shape> shapes, std::span<const Material> materials) {
    if (!has(body)) return std::unexpected(Error{Errc::body, "no such body"});
    if (shapes.empty() || shapes.size() != materials.size()) return std::unexpected(Error{Errc::shape, "a shape at least, and a material each"});
    const b2BodyId native = native_->native(body);
    std::vector<b2ShapeId> old(static_cast<std::size_t>(b2Body_GetShapeCount(native)));
    (void)b2Body_GetShapes(native, old.data(), static_cast<int>(old.size()));
    for (std::size_t index = 0; index < shapes.size(); ++index)
        if (auto attached = attach_to(native, shapes[index], materials[index]); !attached) {
            // What was attached so far goes again: the body keeps the shapes it had.
            std::vector<b2ShapeId> all(static_cast<std::size_t>(b2Body_GetShapeCount(native)));
            (void)b2Body_GetShapes(native, all.data(), static_cast<int>(all.size()));
            for (const b2ShapeId shape : all)
                if (std::ranges::none_of(old, [&](b2ShapeId kept) { return mcpplibs::mwrap::box2d::macros::B2_ID_EQUALS(kept, shape); })) b2DestroyShape(shape, false);
            b2Body_UpdateMassFromShapes(native);
            return std::unexpected(attached.error());
        }
    for (const b2ShapeId shape : old) b2DestroyShape(shape, false);
    b2Body_UpdateMassFromShapes(native);
    return {};
}

std::expected<BodyId, Error> World::add(const BodyDef& body, std::span<const Shape> shapes, std::span<const Material> materials) {
    if (shapes.empty() || shapes.size() != materials.size()) return std::unexpected(Error{Errc::shape, "a shape at least, and a material each"});
    auto made = add(body, shapes.front(), materials.front());
    if (!made) return made;
    for (std::size_t index = 1; index < shapes.size(); ++index)
        if (auto attached = attach(*made, shapes[index], materials[index]); !attached) {
            remove(*made);
            return std::unexpected(attached.error());
        }
    return made;
}

void World::step() noexcept {
    b2World_Step(native_->world, options_.step, options_.substeps);
    ++steps_;
    contacts_.clear();
    const b2ContactEvents events = b2World_GetContactEvents(native_->world);
    const auto body_of = [](b2ShapeId shape) {
        const b2BodyId body = b2Shape_GetBody(shape);
        return BodyId{body.index1, body.generation};
    };
    for (int index = 0; index < events.beginCount; ++index)
        contacts_.push_back({body_of(events.beginEvents[index].shapeIdA), body_of(events.beginEvents[index].shapeIdB), true, {}, {}, 0});
    for (int index = 0; index < events.hitCount; ++index) {
        const b2ContactHitEvent& hit = events.hitEvents[index];
        const BodyId one = body_of(hit.shapeIdA), other = body_of(hit.shapeIdB);
        auto found = std::ranges::find_if(contacts_, [&](const Contact& c) { return c.began && c.one == one && c.other == other; });
        if (found == contacts_.end()) found = contacts_.insert(contacts_.end(), Contact{one, other, true, {}, {}, 0});
        found->point = vec(hit.point);
        found->normal = vec(hit.normal);
        found->approach = hit.approachSpeed;
    }
    for (int index = 0; index < events.endCount; ++index) {
        const b2ContactEndTouchEvent& ended = events.endEvents[index];
        if (!b2Shape_IsValid(ended.shapeIdA) || !b2Shape_IsValid(ended.shapeIdB)) continue;  // a body removed
        contacts_.push_back({body_of(ended.shapeIdA), body_of(ended.shapeIdB), false, {}, {}, 0});
    }
}

// Saved: the magic, the step count, the bodies in the order made, then Box2D's image.
std::vector<std::uint8_t> World::save() const {
    const int size = b2World_GetSnapshot(native_->world, nullptr, 0);
    std::vector<std::uint8_t> saved;
    // Little-endian, a byte at a time: the same image on every platform, and no reading of an object's bytes.
    const auto put = [&](std::uint64_t value, int bytes) {
        for (int index = 0; index < bytes; ++index) saved.push_back(static_cast<std::uint8_t>(value >> (8 * index)));
    };
    for (const char c : magic) saved.push_back(static_cast<std::uint8_t>(c));
    put(steps_, 8);
    put(bodies_.size(), 8);
    for (const BodyId body : bodies_) put(static_cast<std::uint32_t>(body.index), 4), put(body.generation, 2);
    const std::size_t at = saved.size();
    saved.resize(at + static_cast<std::size_t>(size));
    (void)b2World_GetSnapshot(native_->world, saved.data() + at, size);
    return saved;
}

namespace {
struct Parsed { std::uint64_t steps = 0; std::vector<BodyId> bodies; std::span<const std::uint8_t> image; };
std::expected<Parsed, Error> parse(std::span<const std::uint8_t> saved) {
    std::size_t at = 0;
    const auto take = [&](int bytes) -> std::optional<std::uint64_t> {
        if (saved.size() - at < static_cast<std::size_t>(bytes)) return std::nullopt;
        std::uint64_t value = 0;
        for (int index = 0; index < bytes; ++index) value |= static_cast<std::uint64_t>(saved[at + static_cast<std::size_t>(index)]) << (8 * index);
        at += static_cast<std::size_t>(bytes);
        return value;
    };
    if (saved.size() < magic.size() || !std::equal(magic.begin(), magic.end(), saved.begin())) return std::unexpected(Error{Errc::state, "not a saved rigid world"});
    at = magic.size();
    Parsed parsed;
    const auto steps = take(8), count = take(8);
    if (!steps || !count || *count > saved.size()) return std::unexpected(Error{Errc::state, "a saved rigid world cut short"});
    parsed.steps = *steps;
    for (std::uint64_t index = 0; index < *count; ++index) {
        const auto body_index = take(4), generation = take(2);
        if (!body_index || !generation) return std::unexpected(Error{Errc::state, "a saved rigid world cut short"});
        parsed.bodies.push_back({static_cast<std::int32_t>(static_cast<std::uint32_t>(*body_index)), static_cast<std::uint16_t>(*generation)});
    }
    parsed.image = saved.subspan(at);
    return parsed;
}
}  // namespace

std::expected<void, Error> World::restore(std::span<const std::uint8_t> saved) {
    auto parsed = parse(saved);
    if (!parsed) return std::unexpected(parsed.error());
    if (!b2World_RestoreFromSnapshot(native_->world, parsed->image.data(), static_cast<int>(parsed->image.size())))
        return std::unexpected(Error{Errc::state, "Box2D refused the image (another version's, or corrupt)"});
    steps_ = parsed->steps;
    bodies_ = std::move(parsed->bodies);
    contacts_.clear();
    return {};
}

std::expected<World, Error> World::from(std::span<const std::uint8_t> saved, const Options& options) {
    auto parsed = parse(saved);
    if (!parsed) return std::unexpected(parsed.error());
    World world;
    world.options_ = options;
    world.native_ = std::make_unique<Native>();
    world.native_->world = b2CreateWorldFromSnapshot(parsed->image.data(), static_cast<int>(parsed->image.size()), 1);
    if (!b2World_IsValid(world.native_->world)) return std::unexpected(Error{Errc::state, "Box2D made no world of the image"});
    world.steps_ = parsed->steps;
    world.bodies_ = std::move(parsed->bodies);
    return world;
}

std::expected<World, Error> World::branch() const { return from(save(), options_); }

std::uint64_t World::state_hash() const noexcept {
    mlibs::hash::Fnv1a hash;
    const auto mix = [&](float value) { hash.le(std::bit_cast<std::uint32_t>(value), 4); };
    for (const BodyId body : bodies_) {
        const b2BodyId native = native_->native(body);
        if (!b2Body_IsValid(native)) continue;
        const b2Transform transform = b2Body_GetTransform(native);
        const b2Vec2 velocity = b2Body_GetLinearVelocity(native);
        mix(transform.p.x), mix(transform.p.y), mix(transform.q.c), mix(transform.q.s), mix(velocity.x), mix(velocity.y), mix(b2Body_GetAngularVelocity(native));
    }
    return hash.value();
}

}  // namespace xengine::dynamics::rigid
