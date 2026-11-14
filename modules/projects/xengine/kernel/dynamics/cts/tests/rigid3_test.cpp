// The 3D rigid family (xengine.dynamics' feature `rigid3`: Jolt under this package's contract) held to the same
// hard constraints as the 2D one: a saved state restored and run again is the uninterrupted run's bits, a world
// made from it runs on as the original, every id names its body across both; what touched what is a step's
// output; rays and shapes are asked of it; a material is read from xengine.matter's table.
import std;
import xengine.dynamics.rigid3;
import xengine.dynamics.cts;
import xengine.matter.json;
import mlibs.hash;
import mcpplibs.mtest;

using mcpplibs::mtest::UTest, mcpplibs::mtest::expect, mcpplibs::mtest::eq, mcpplibs::mtest::fatal, mcpplibs::mtest::operator""_test;
namespace rigid3 = xengine::dynamics::rigid3;

namespace {
// A floor, a tower of 6 × 3 boxes, a capsule and a hull beside it, and a heavy ball thrown at the tower.
struct Scene {
    rigid3::World world;
    rigid3::BodyId floor, ball;
    std::vector<rigid3::BodyId> boxes;
};
Scene make(int workers = 0) {
    auto world = rigid3::World::make({.workers = workers});
    const auto floor = world->add({.kind = rigid3::Kind::fixed, .at = {0, -0.5f, 0}}, rigid3::Shape::box({30, 0.5f, 30}));
    std::vector<rigid3::BodyId> boxes;
    for (int level = 0; level < 6; ++level)
        for (int column = 0; column < 3; ++column)
            boxes.push_back(*world->add({.at = {static_cast<float>(column) * 1.02f - 1.02f, 0.5f + static_cast<float>(level) * 1.01f, 0}}, rigid3::Shape::box({0.5f, 0.5f, 0.5f}), {.density = 600}));
    (void)world->add({.at = {4, 1, 0}}, rigid3::Shape::capsule(0.5f, 0.3f));
    (void)world->add({.at = {-4, 1, 1}}, rigid3::Shape::hull({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0.6f, 0.6f, 0.6f}}), {.density = 2500});
    const auto ball = world->add({.at = {-12, 2.5f, 0}, .velocity = {22, 1, 0}, .bullet = true}, rigid3::Shape::sphere(0.6f), {.density = 7800});
    return {std::move(*world), *floor, *ball, std::move(boxes)};
}
void run(rigid3::World& world, int steps) {
    for (int step = 0; step < steps; ++step) world.step();
}

// Jolt's family as the suite sees a rigid family (xengine.dynamics.cts' RigidFamily): the tower struck by the ball.
struct JoltFamily {
    using State = rigid3::World;
    using Body = rigid3::BodyId;
    std::unique_ptr<State> make() const { return std::make_unique<State>(std::move(::make().world)); }
    void step(State& world) const { world.step(); }
    void disturb(State& world) const {
        for (const rigid3::BodyId body : world.bodies()) world.push(body, {0, 50, 0});
    }
    std::uint64_t hash(const State& world) const { return world.state_hash(); }
    std::vector<std::uint8_t> save(const State& world) const { return world.save(); }
    bool restore(State& world, std::span<const std::uint8_t> image) const { return world.restore(image).has_value(); }
    std::unique_ptr<State> branch(const State& world) const {
        auto made = world.branch();
        return made ? std::make_unique<State>(std::move(*made)) : nullptr;
    }
    void read(const State& world) const {
        for (const rigid3::BodyId body : world.bodies()) (void)world.pose(body);
        (void)world.contacts();
        (void)world.ray({0, 10, 0}, {0, -10, 0});
    }
    std::vector<Body> bodies(const State& world) const { return {world.bodies().begin(), world.bodies().end()}; }
    Body add(State& world) const { return *world.add({.at = {8, 2, 0}}, rigid3::Shape::sphere(0.5f)); }
    void remove(State& world, Body body) const { world.remove(body); }
    bool has(const State& world, Body body) const { return world.has(body); }
    std::uint64_t where(const State& world, Body body) const {
        const auto pose = world.pose(body);
        mlibs::hash::Fnv1a hash;
        if (pose)
            for (const float value : {pose->at.x, pose->at.y, pose->at.z, pose->turn.x, pose->turn.y, pose->turn.z, pose->turn.w}) hash.u64(std::bit_cast<std::uint32_t>(value));
        return hash.value();
    }
};
}  // namespace

UTest cases{"dynamics.rigid3", [] {
    // The family's hard constraints (the world contract and the ids), on Jolt: jolt-5/world.…, jolt-5/rigid.…
    mlibs::cts::against({.subject = "jolt-5"}, [] { xengine::dynamics::cts::declare_rigid(JoltFamily{}); });

    "a saved state restored and run again is the uninterrupted run's bits; a branch runs on alike; any number of workers, the same bits"_test = [] {
        Scene straight = make();
        std::vector<std::uint64_t> expected;
        for (int step = 0; step < 240; ++step) straight.world.step(), expected.push_back(straight.world.state_hash());
        Scene rolled = make();
        run(rolled.world, 120);
        const auto saved = rolled.world.save();
        expect(saved[0] == 'X' && saved[1] == 'R' && saved[2] == 'G' && saved[3] == '3');
        run(rolled.world, 120);
        expect(eq(rolled.world.state_hash(), expected[239])) << "taking a save changes nothing";
        expect(fatal(rolled.world.restore(saved).has_value()));
        expect(eq(rolled.world.steps(), std::uint64_t{120}));
        expect(eq(rolled.world.state_hash(), expected[119])) << "back where it was saved";
        for (int step = 120; step < 240; ++step) {
            rolled.world.step();
            if (rolled.world.state_hash() != expected[static_cast<std::size_t>(step)]) {
                expect(false) << std::format("restored, step {} parts from the uninterrupted run", step + 1);
                break;
            }
        }
        auto branch = rigid3::World::from(saved);
        expect(fatal(branch.has_value()));
        expect(eq(branch->state_hash(), expected[119]));
        run(*branch, 120);
        expect(eq(branch->state_hash(), expected[239])) << "a world made from the save runs on as the original";
        for (const int workers : {1, 3}) {
            Scene threaded = make(workers);
            run(threaded.world, 240);
            expect(eq(threaded.world.state_hash(), expected[239])) << std::format("{} workers: the same bits", workers);
        }
        // The tower was knocked down: the test saw something happen.
        const auto top = straight.world.pose(straight.boxes.back());
        expect(fatal(top.has_value()));
        expect(top->at.y < 4.0f) << std::format("the top box at {}", top->at.y);
    };

    "what touched what: the ball meets the tower, where, along which normal and how fast; in the order of the ids"_test = [] {
        Scene scene = make();
        std::optional<rigid3::Contact> met;
        for (int step = 0; step < 120 && !met; ++step) {
            scene.world.step();
            const auto contacts = scene.world.contacts();
            expect(std::ranges::is_sorted(contacts, {}, [](const rigid3::Contact& contact) { return std::pair(contact.one, contact.other); }));
            for (const rigid3::Contact& contact : contacts) {
                expect(contact.one < contact.other);
                if (contact.began && (contact.one == scene.ball || contact.other == scene.ball) &&
                    std::ranges::find(scene.boxes, contact.one == scene.ball ? contact.other : contact.one) != scene.boxes.end())
                    met = contact;
            }
        }
        expect(fatal(met.has_value())) << "the ball met a box";
        expect(met->approach > 10.0f) << std::format("{} m/s", met->approach);
        expect(met->point.x > -2.5f && met->point.x < -0.5f) << std::format("at x {}", met->point.x);
        expect(std::abs(met->normal.x) > 0.5f) << "head on";
    };

    "rays and shapes asked of the world"_test = [] {
        Scene scene = make();
        const auto hit = scene.world.ray({0, 20, 0}, {0, -20, 0});
        expect(fatal(hit.has_value()));
        expect(hit->body == scene.boxes[5 * 3 + 1]) << "straight down onto the top of the middle column";
        expect(std::abs(hit->point.y - 6.05f) < 1e-3f) << std::format("at {}", hit->point.y);
        expect(hit->normal.y > 0.99f);
        expect(!scene.world.ray({0, 20, 0}, {0, 30, 0}).has_value()) << "upwards: nothing";
        std::vector<rigid3::BodyId> inside;
        // A box from just under the floor's top to the bottom box's middle: that box and the floor, not its neighbours.
        scene.world.overlapping(rigid3::Shape::box({0.3f, 0.3f, 0.3f}), {0, 0.25f, 0}, {}, inside);
        expect(inside.size() == 2u && std::ranges::find(inside, scene.boxes[1]) != inside.end() && std::ranges::find(inside, scene.floor) != inside.end())
            << std::format("{} bodies about the bottom box's lower half: it and the floor it rests on", inside.size());
    };

    "a material read from xengine.matter's table: stone is heavier than wood of one shape; a ball of restitution 0.8 comes back up"_test = [] {
        const auto table = xengine::matter::standard();
        expect(fatal(table.has_value()));
        const auto stone = rigid3::material_of(*table, *table->material("stone")), wood = rigid3::material_of(*table, *table->material("wood"));
        auto world = rigid3::World::make();
        const auto heavy = world->add({.at = {0, 5, 0}}, rigid3::Shape::box({0.5f, 0.5f, 0.5f}), stone);
        const auto light = world->add({.at = {3, 5, 0}}, rigid3::Shape::box({0.5f, 0.5f, 0.5f}), wood);
        expect(world->pose(*heavy)->mass > world->pose(*light)->mass * 2) << std::format("stone {} kg, wood {} kg", world->pose(*heavy)->mass, world->pose(*light)->mass);
        expect(std::abs(world->pose(*heavy)->mass - stone.density) < stone.density * 1e-3f) << "a cubic metre of it";
        float highest[2]{};
        for (int which = 0; which < 2; ++which) {
            auto drop = rigid3::World::make();
            (void)drop->add({.kind = rigid3::Kind::fixed, .at = {0, -0.5f, 0}}, rigid3::Shape::box({5, 0.5f, 5}), {.restitution = 0});
            const auto ball = drop->add({.at = {0, 5, 0}}, rigid3::Shape::sphere(0.5f), {.restitution = which ? 0.0f : 0.8f});
            bool landed = false;
            for (int step = 0; step < 240; ++step) {
                drop->step();
                const auto pose = drop->pose(*ball);
                if (pose->velocity.y > 0) landed = true;
                if (landed) highest[which] = std::max(highest[which], pose->at.y);
            }
        }
        expect(highest[0] > 2.5f) << std::format("bounced to {}", highest[0]);
        expect(highest[1] < 0.7f) << std::format("stayed at {}", highest[1]);
    };

    "a mesh is ground to stand on; what is malformed is refused and changes nothing"_test = [] {
        auto world = rigid3::World::make();
        expect(fatal(world.has_value()));
        const auto ground = world->add({.kind = rigid3::Kind::fixed}, rigid3::Shape::mesh({{-10, 0, -10}, {10, 0, -10}, {10, 0, 10}, {-10, 0, 10}}, {0, 2, 1, 0, 3, 2}));
        expect(fatal(ground.has_value())) << (ground ? "" : ground.error().text);
        const auto ball = world->add({.at = {0, 3, 0}}, rigid3::Shape::sphere(0.5f));
        run(*world, 180);
        expect(std::abs(world->pose(*ball)->at.y - 0.5f) < 0.05f) << std::format("rests at {}", world->pose(*ball)->at.y);
        expect(!world->add({}, rigid3::Shape::mesh({{0, 0, 0}, {1, 0, 0}, {0, 0, 1}}, {0, 1, 2}))) << "a moving mesh";
        expect(!world->add({}, rigid3::Shape::box({0, 1, 1})));
        expect(!world->add({}, rigid3::Shape::sphere(-1)));
        expect(!world->add({}, rigid3::Shape::hull({{0, 0, 0}, {1, 0, 0}, {2, 0, 0}})));
        expect(!world->add({.kind = rigid3::Kind::fixed}, rigid3::Shape::mesh({{0, 0, 0}}, {0, 1, 2}))) << "a corner past the points";
        expect(eq(world->bodies().size(), std::size_t{2})) << "no half-made body left";
        const auto hash = world->state_hash();
        const std::vector<std::uint8_t> garbage(64, 7);
        expect(!world->restore(garbage));
        auto truncated = world->save();
        truncated.resize(truncated.size() - 1);
        expect(!world->restore(truncated) && !rigid3::World::from(truncated));
        expect(!rigid3::World::make({.collision_steps = 0}));
        expect(eq(world->state_hash(), hash) && world->has(*ball));
    };
}};
