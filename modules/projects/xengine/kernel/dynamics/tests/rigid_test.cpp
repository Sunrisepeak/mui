// The 2D rigid family (xengine.dynamics' feature `rigid`: Box2D v3 under this package's contract) held to what
// rollback and world.branch need: a saved state restored and run again is the uninterrupted run's bits, a world
// made from it runs on as the original does, every id names its body across both, what touched what is said.
import std;
import xengine.dynamics.rigid;
import xengine.dynamics.cts;
import mlibs.hash;
import mcpplibs.mtest;

// mtest by name: its `cts` is not ours.
using mcpplibs::mtest::UTest, mcpplibs::mtest::expect, mcpplibs::mtest::eq, mcpplibs::mtest::fatal, mcpplibs::mtest::operator""_test;
namespace rigid = xengine::dynamics::rigid;

namespace {
// A floor, a pyramid of 20 rows of boxes and a heavy ball thrown at it.
struct Scene {
    rigid::World world;
    rigid::BodyId ball;
    std::vector<rigid::BodyId> boxes;
};
Scene make() {
    auto world = rigid::World::make();
    std::vector<rigid::BodyId> boxes;
    (void)world->add({.kind = rigid::Kind::fixed}, rigid::Shape::segment({-60, 0}, {60, 0}));
    for (int row = 0; row < 20; ++row)
        for (int column = 0; column < 20 - row; ++column)
            boxes.push_back(*world->add({.at = {-10.0f + column * 1.05f + row * 0.525f, 0.5f + row * 1.0f}}, rigid::Shape::box(0.5f, 0.5f)));
    const auto ball = world->add({.at = {-40, 6}, .velocity = {40, 0}, .bullet = true}, rigid::Shape::circle(1.5f), {.density = 20});
    return {std::move(*world), *ball, std::move(boxes)};
}
void run(rigid::World& world, int steps) { for (int step = 0; step < steps; ++step) world.step(); }

// Box2D's family as the suite sees a rigid family (xengine.dynamics.cts' RigidFamily): the pyramid struck by the
// ball. Jolt, for three dimensions, will be another of these.
struct Box2dFamily {
    using State = rigid::World;
    using Body = rigid::BodyId;
    std::unique_ptr<State> make() const { return std::make_unique<State>(std::move(make_scene().world)); }
    static Scene make_scene();
    void step(State& world) const { world.step(); }
    void disturb(State& world) const {
        for (const rigid::BodyId body : world.bodies()) world.push(body, {0, -400});
    }
    std::uint64_t hash(const State& world) const { return world.state_hash(); }
    std::vector<std::uint8_t> save(const State& world) const { return world.save(); }
    bool restore(State& world, std::span<const std::uint8_t> image) const { return world.restore(image).has_value(); }
    std::unique_ptr<State> branch(const State& world) const {
        auto made = world.branch();
        return made ? std::make_unique<State>(std::move(*made)) : nullptr;
    }
    void read(const State& world) const {
        for (const rigid::BodyId body : world.bodies()) (void)world.pose(body);
        (void)world.contacts();
    }
    std::vector<Body> bodies(const State& world) const { return world.bodies(); }
    Body add(State& world) const { return *world.add({.at = {30, 2}}, rigid::Shape::circle(0.5f)); }
    void remove(State& world, Body body) const { world.remove(body); }
    bool has(const State& world, Body body) const { return world.has(body); }
    std::uint64_t where(const State& world, Body body) const {
        const auto pose = world.pose(body);
        mlibs::hash::Fnv1a hash;
        if (pose)
            for (const float value : {pose->at.x, pose->at.y, pose->angle}) hash.u64(std::bit_cast<std::uint32_t>(value));
        return hash.value();
    }
};
Scene Box2dFamily::make_scene() { return ::make(); }
}  // namespace

UTest cases{"dynamics.rigid", [] {
    // The family's hard constraints (the world contract and the ids), on Box2D: box2d-v3/world.…, box2d-v3/rigid.…
    // Box2D writes its structures as they lie in memory, unused bytes with them: two worlds at one state need not
    // have one image, byte for byte (their hashes are one). Said, so that case is unsupported — not passed.
    mlibs::cts::against({.subject = "box2d-v3", .has = [](std::string_view capability) { return capability != "canonical-image"; }}, [] { xengine::dynamics::cts::declare_rigid(Box2dFamily{}); });
    "the family has ten hard constraints, the case that reading changes nothing among them"_test = [] {
        expect(eq(xengine::dynamics::cts::rigid_case_count<Box2dFamily>(), std::size_t{10}));
    };

    "a saved state restored and run again is the uninterrupted run's bits; saving changes nothing; a branch runs on alike"_test = [] {
        Scene straight = make();
        std::vector<std::uint64_t> expected;
        for (int step = 0; step < 300; ++step) straight.world.step(), expected.push_back(straight.world.state_hash());
        Scene rolled = make();
        run(rolled.world, 150);
        const auto saved = rolled.world.save();
        expect(saved.size() > 1000u) << std::format("{} bytes", saved.size());
        // The image's head, byte for byte: the magic, the steps and how many bodies, little-endian — the same on
        // every platform and at every optimisation (a build that wrote an object's bytes here once wrote garbage).
        const auto word = [&](std::size_t at) {
            std::uint64_t value = 0;
            for (int byte = 0; byte < 8; ++byte) value |= static_cast<std::uint64_t>(saved[at + static_cast<std::size_t>(byte)]) << (8 * byte);
            return value;
        };
        expect(saved[0] == 'X' && saved[1] == 'R' && saved[2] == 'G' && saved[3] == '2');
        expect(eq(word(4), std::uint64_t{150}));
        expect(word(12) >= 1u && word(12) < 1000u) << std::format("{} bodies in the image's head", word(12));
        run(rolled.world, 150);
        expect(eq(rolled.world.state_hash(), expected[299])) << "taking a save changes nothing";
        expect(fatal(rolled.world.restore(saved).has_value()));
        expect(eq(rolled.world.steps(), std::uint64_t{150}));
        expect(eq(rolled.world.state_hash(), expected[149])) << "back where it was saved";
        for (int step = 150; step < 300; ++step) {
            rolled.world.step();
            if (rolled.world.state_hash() != expected[static_cast<std::size_t>(step)]) {
                expect(false) << std::format("restored, step {} parts from the uninterrupted run", step + 1);
                break;
            }
        }
        auto branch = rigid::World::from(saved);
        expect(fatal(branch.has_value()));
        expect(eq(branch->state_hash(), expected[149]));
        run(*branch, 150);
        expect(eq(branch->state_hash(), expected[299])) << "a world made from the save runs on as the original";
        // Two at once, from one world: the branch and the original do not touch each other.
        auto again = rolled.world.branch();
        expect(fatal(again.has_value()));
        again->push(rolled.ball, {0, 5000});
        run(*again, 10);
        expect(eq(rolled.world.state_hash(), expected[299])) << "pushing the branch moved nothing of the original";
        expect(again->state_hash() != expected[299]);
        // The pyramid was knocked about: the test saw something happen.
        const auto top = straight.world.pose(straight.boxes.back());
        expect(fatal(top.has_value()));
        expect(top->at.y < 15.0f) << std::format("the top box at {}", top->at.y);
    };

    "what touched what: the ball meets the pyramid, with where, which way and how fast"_test = [] {
        Scene scene = make();
        std::optional<rigid::Contact> met;
        for (int step = 0; step < 120 && !met; ++step) {
            scene.world.step();
            for (const rigid::Contact& contact : scene.world.contacts())
                if (contact.began && contact.approach > 0 && (contact.one == scene.ball || contact.other == scene.ball) &&
                    std::ranges::find(scene.boxes, contact.one == scene.ball ? contact.other : contact.one) != scene.boxes.end())
                    met = contact;
        }
        expect(fatal(met.has_value())) << "the ball met a box";
        expect(met->approach > 20.0f) << std::format("{} m/s", met->approach);
        expect(met->point.x > -15.0f && met->point.x < 0.0f) << std::format("at x {}", met->point.x);
        expect(std::abs(met->normal.x) > 0.5f) << "head on";
    };

    "a material's numbers: a ball of restitution 0.8 comes back up, one of 0 stays down"_test = [] {
        float highest[2]{};
        for (int which = 0; which < 2; ++which) {
            auto world = rigid::World::make();
            // A floor of restitution 0: two surfaces meet at the greater of their restitutions (Box2D's rule), so the ball's decides.
            (void)world->add({.kind = rigid::Kind::fixed}, rigid::Shape::segment({-10, 0}, {10, 0}), {.restitution = 0.0f});
            const auto ball = world->add({.at = {0, 5}}, rigid::Shape::circle(0.5f), {.restitution = which ? 0.0f : 0.8f});
            bool landed = false;
            for (int step = 0; step < 240; ++step) {
                world->step();
                const auto pose = world->pose(*ball);
                if (pose->velocity.y > 0) landed = true;
                if (landed) highest[which] = std::max(highest[which], pose->at.y);
            }
        }
        expect(highest[0] > 2.5f) << std::format("bounced to {}", highest[0]);
        expect(highest[1] < 0.7f) << std::format("stayed at {}", highest[1]);
    };

    "what is malformed is refused and changes nothing"_test = [] {
        auto world = rigid::World::make();
        expect(fatal(world.has_value()));
        expect(!world->add({}, rigid::Shape::box(0, 1)));
        expect(!world->add({}, rigid::Shape::circle(-1)));
        expect(!world->add({}, rigid::Shape::polygon({{0, 0}, {1, 0}})));
        expect(!world->add({}, rigid::Shape::polygon({{0, 0}, {1, 0}, {2, 0}}))) << "collinear";
        expect(eq(world->bodies().size(), std::size_t{0})) << "no half-made body left";
        const auto ball = world->add({.at = {0, 3}}, rigid::Shape::circle(0.5f));
        const auto hash = world->state_hash();
        const std::vector<std::uint8_t> garbage(64, 7);
        expect(!world->restore(garbage));
        expect(!rigid::World::make({.substeps = 0}));
        expect(eq(world->state_hash(), hash) && world->has(*ball));
        auto truncated = world->save();
        truncated.resize(12);
        expect(!world->restore(truncated) && !rigid::World::from(truncated));
    };
}};
