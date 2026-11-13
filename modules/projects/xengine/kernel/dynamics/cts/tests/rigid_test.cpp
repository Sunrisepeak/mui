// The 2D rigid family (xengine.dynamics' feature `rigid`: Box2D v3 under this package's contract) held to what
// rollback and world.branch need: a saved state restored and run again is the uninterrupted run's bits, a world
// made from it runs on as the original does, every id names its body across both, what touched what is said.
import std;
import xengine.dynamics.rigid;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
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
}  // namespace

UTest cases{"dynamics.rigid", [] {
    "a saved state restored and run again is the uninterrupted run's bits; saving changes nothing; a branch runs on alike"_test = [] {
        Scene straight = make();
        std::vector<std::uint64_t> expected;
        for (int step = 0; step < 300; ++step) straight.world.step(), expected.push_back(straight.world.state_hash());
        Scene rolled = make();
        run(rolled.world, 150);
        const auto saved = rolled.world.save();
        expect(saved.size() > 1000u) << std::format("{} bytes", saved.size());
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

    "every id names its body across a restore: one removed after the save is back, one added after it is gone"_test = [] {
        Scene scene = make();
        run(scene.world, 30);
        const auto saved = scene.world.save();
        const rigid::BodyId box = scene.boxes[5];
        const auto before = scene.world.pose(box);
        scene.world.remove(box);
        expect(!scene.world.has(box));
        const auto added = scene.world.add({.at = {30, 2}}, rigid::Shape::circle(0.5f));
        expect(fatal(added.has_value()));
        expect(fatal(scene.world.restore(saved).has_value()));
        expect(scene.world.has(box)) << "the removed box is back";
        const auto after = scene.world.pose(box);
        expect(fatal(before && after));
        expect(before->at.x == after->at.x && before->at.y == after->at.y && before->angle == after->angle);
        const auto bodies = scene.world.bodies();
        expect(!scene.world.has(*added) || std::ranges::find(bodies, *added) == bodies.end()) << "the body added after the save is not in the world";
        expect(eq(bodies.size(), scene.boxes.size() + 2));
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
