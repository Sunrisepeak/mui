// The 3D rigid family's joints, characters, compounds, sensors and shape casts — each data on the world, held to
// the family's hard constraints with the bodies: a scene with all of them passes the same suite as the plain one
// (an image restored runs on bit for bit, a branch runs alike, ids name the same thing across both), and runs to
// the same bits with 1 and 3 workers.
import std;
import xengine.dynamics.rigid3;
import xengine.dynamics.cts;
import mlibs.hash;
import mcpplibs.mtest;

using mcpplibs::mtest::UTest, mcpplibs::mtest::expect, mcpplibs::mtest::eq, mcpplibs::mtest::fatal, mcpplibs::mtest::operator""_test;
namespace rigid3 = xengine::dynamics::rigid3;

namespace {

constexpr float pi = 3.14159265f;

// Everything at once: a floor and a ramp; a door on a limited hinge, a wheel its motor turns, a block on a limited
// slider, a chain of balls on points, two boxes held apart by a distance joint and two fixed together, a capsule on
// a cone; a fixed compound (an L), a movable one; a sensor on the floor; a character walking up the ramp.
struct Scene {
    rigid3::World world;
    rigid3::BodyId floor, door, wheel, block, sensor, movable, last_ball, weight, hanging;
    rigid3::JointId door_hinge, wheel_hinge, slide, rope, cone;
    rigid3::CharacterId walker;
};
Scene make(int workers = 0) {
    auto made = rigid3::World::make({.workers = workers});
    expect(fatal(made.has_value()));
    rigid3::World& world = *made;
    Scene scene{std::move(world)};
    rigid3::World& w = scene.world;
    scene.floor = *w.add({.kind = rigid3::Kind::fixed, .at = {0, -0.5f, 0}}, rigid3::Shape::box({30, 0.5f, 30}));
    // A ramp of 20° rising along +x from the floor at x = 4.
    (void)*w.add({.kind = rigid3::Kind::fixed, .at = {7, 0.879f, -6}, .turn = rigid3::Turn::about({0, 0, 1}, 20 * pi / 180)}, rigid3::Shape::box({3, 0.2f, 1.5f}));
    // The door: a hinge on its left edge to the world, ±0.5 rad, pushed open.
    scene.door = *w.add({.at = {-6, 1.05f, 0}}, rigid3::Shape::box({1, 1, 0.05f}), {.density = 300});
    rigid3::Joint door = rigid3::Joint::hinge(scene.door, {}, {-7, 1.05f, 0}, {0, 1, 0});
    door.lower = -0.5f, door.upper = 0.5f;
    scene.door_hinge = *w.join(door);
    w.push(scene.door, {0, 0, 400}, rigid3::Vec{-5, 1.05f, 0});
    // The wheel: turned by its motor at 2 rad/s about z.
    scene.wheel = *w.add({.at = {-6, 2, 4}}, rigid3::Shape::compound({{rigid3::Shape::box({0.8f, 0.1f, 0.1f}), {}, {}}, {rigid3::Shape::box({0.1f, 0.8f, 0.1f}), {}, {}}}));
    rigid3::Joint wheel = rigid3::Joint::hinge(scene.wheel, {}, {-6, 2, 4}, {0, 0, 1});
    wheel.motor = {.mode = rigid3::Motor::Mode::velocity, .target = 2, .most = 500};
    scene.wheel_hinge = *w.join(wheel);
    // The block: on a slider along x, at most 1 m either way, given a shove.
    scene.block = *w.add({.at = {0, 0.5f, 6}}, rigid3::Shape::box({0.4f, 0.4f, 0.4f}));
    rigid3::Joint slide = rigid3::Joint::slider(scene.block, {}, {0, 0.5f, 6}, {1, 0, 0});
    slide.lower = -1, slide.upper = 1;
    scene.slide = *w.join(slide);
    w.push(scene.block, {2000, 0, 0});
    // The chain: three balls hung from a point and from each other, let go sideways.
    rigid3::BodyId above;
    for (int link = 0; link < 3; ++link) {
        const rigid3::BodyId ball = *w.add({.at = {4.0f + static_cast<float>(link + 1), 6, 4}}, rigid3::Shape::sphere(0.25f));
        (void)*w.join(rigid3::Joint::point(ball, above, {4.0f + static_cast<float>(link) + 0.5f, 6, 4}));
        above = scene.last_ball = ball;
    }
    // A weight on a 2 m rope from a point 1.5 m above it, let fall; two boxes fixed together.
    scene.weight = *w.add({.at = {-2, 4, -3}}, rigid3::Shape::box({0.3f, 0.3f, 0.3f}));
    rigid3::Joint rope = rigid3::Joint::distance(scene.weight, {}, {-2, 4, -3}, {-2, 5.5f, -3});
    rope.lower = 0, rope.upper = 2;
    scene.rope = *w.join(rope);
    const auto left = *w.add({.at = {2, 3, -3}}, rigid3::Shape::box({0.3f, 0.3f, 0.3f})), right = *w.add({.at = {2.6f, 3, -3}}, rigid3::Shape::box({0.3f, 0.3f, 0.3f}));
    (void)*w.join(rigid3::Joint::fixed(left, right));
    // A capsule hung by a cone of 0.3 rad from the world, kicked.
    scene.hanging = *w.add({.at = {-3, 3, 7}}, rigid3::Shape::capsule(0.5f, 0.15f));
    scene.cone = *w.join(rigid3::Joint::cone(scene.hanging, {}, {-3, 3.8f, 7}, {0, -1, 0}, 0.3f));
    w.push(scene.hanging, {300, 0, 0});
    // An L fixed together, falling; a movable compound of two parts.
    (void)*w.add({.at = {5, 2, 1}}, rigid3::Shape::compound({{rigid3::Shape::box({0.5f, 0.1f, 0.1f}), {0.4f, 0, 0}, {}}, {rigid3::Shape::box({0.1f, 0.5f, 0.1f}), {0, 0.4f, 0}, {}}}));
    scene.movable = *w.add({.at = {-1, 1, -8}}, rigid3::Shape::compound({{rigid3::Shape::box({0.3f, 0.3f, 0.3f}), {-0.5f, 0, 0}, {}}, {rigid3::Shape::sphere(0.3f), {0.5f, 0, 0}, {}}}, true));
    // A sensor across the slider's path and the falling L's.
    scene.sensor = *w.add({.kind = rigid3::Kind::fixed, .at = {5, 0.5f, 1}, .sensor = true}, rigid3::Shape::box({1, 0.5f, 1}));
    // The walker: up the ramp along +x.
    scene.walker = *w.add(rigid3::CharacterDef{.at = {1, 0, -6}});
    w.walk(scene.walker, {2, 0, 0});
    return scene;
}
void run(rigid3::World& world, int steps) {
    for (int step = 0; step < steps; ++step) world.step();
}

struct PartsFamily {
    using State = rigid3::World;
    using Body = rigid3::BodyId;
    std::unique_ptr<State> make() const { return std::make_unique<State>(std::move(::make().world)); }
    void step(State& world) const { world.step(); }
    void disturb(State& world) const {
        for (const rigid3::BodyId body : world.bodies()) world.push(body, {0, 30, 0});
        for (const rigid3::CharacterId character : world.characters()) world.jump(character, 4);
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
        for (const rigid3::JointId joint : world.joints()) (void)world.joint(joint);
        for (const rigid3::CharacterId character : world.characters()) (void)world.character(character);
        (void)world.cast(rigid3::Shape::sphere(0.3f), {}, {0, 10, 0}, {0, -10, 0});
    }
    std::vector<Body> bodies(const State& world) const { return {world.bodies().begin(), world.bodies().end()}; }
    Body add(State& world) const { return *world.add({.at = {8, 2, 8}}, rigid3::Shape::sphere(0.5f)); }
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

UTest cases{"dynamics.rigid3.parts", [] {
    // The hard constraints on a world of joints, characters, compounds and a sensor: jolt-5-parts/world.…, /rigid.…
    mlibs::cts::against({.subject = "jolt-5-parts"}, [] { xengine::dynamics::cts::declare_rigid(PartsFamily{}, {.at = 30}); });

    "with every part: restored mid-run and run on, the uninterrupted run's bits; the same with 1 and 3 workers"_test = [] {
        Scene straight = make();
        run(straight.world, 240);
        const std::uint64_t expected = straight.world.state_hash();
        Scene rolled = make();
        run(rolled.world, 90);
        const auto saved = rolled.world.save();
        run(rolled.world, 50);
        rolled.world.unjoin(rolled.slide);
        rolled.world.remove(rolled.walker);
        expect(fatal(rolled.world.restore(saved).has_value()));
        expect(eq(rolled.world.joints().size(), straight.world.joints().size()) && eq(rolled.world.characters().size(), std::size_t{1})) << "the joint and the walker are back";
        run(rolled.world, 150);
        expect(eq(rolled.world.state_hash(), expected)) << "restored with a joint and the walker taken away since";
        auto branch = rigid3::World::from(saved);
        expect(fatal(branch.has_value()));
        run(*branch, 150);
        expect(eq(branch->state_hash(), expected)) << "a world made from the bytes alone";
        for (const int workers : {1, 3}) {
            Scene threaded = make(workers);
            run(threaded.world, 240);
            expect(eq(threaded.world.state_hash(), expected)) << std::format("{} workers", workers);
        }
    };

    "joints hold: the door within its limits, the wheel at its motor's speed, the block within its slider, the rope no longer than 2 m, the cone within 0.3 rad"_test = [] {
        // A limit meets a body moving into it in the step it arrives: past it by at most that step's travel, and
        // back within it as the solver settles. Peaks are bounded by a step's travel at the speed each one was given
        // (the door 5 rad/s, the block 3.9 m/s, the weight 3.1 m/s after its 0.5 m fall, the capsule 4.4 rad/s), rests by 1%.
        Scene scene = make();
        constexpr float dt = 1.0f / 60;
        float widest = 0, farthest = 0, longest = 0, leaning = 0;
        for (int step = 0; step < 180; ++step) {
            scene.world.step();
            widest = std::max(widest, std::abs(scene.world.joint(scene.door_hinge)->value));
            farthest = std::max(farthest, std::abs(scene.world.joint(scene.slide)->value));
            longest = std::max(longest, scene.world.joint(scene.rope)->value);
            const rigid3::Turn turn = scene.world.pose(scene.hanging)->turn;
            leaning = std::max(leaning, std::acos(std::clamp(1 - 2 * (turn.x * turn.x + turn.z * turn.z), -1.0f, 1.0f)));
        }
        expect(widest > 0.3f && widest < 0.5f + 5 * dt) << std::format("the door opened to {} rad at most", widest);
        expect(std::abs(scene.world.joint(scene.door_hinge)->value) < 0.505f) << std::format("the door rests at {} rad", scene.world.joint(scene.door_hinge)->value);
        expect(farthest > 0.9f && farthest < 1 + 3.9f * dt) << std::format("the block slid {} m at most", farthest);
        expect(std::abs(scene.world.joint(scene.slide)->value) < 1.01f) << std::format("the block rests at {} m", scene.world.joint(scene.slide)->value);
        expect(longest > 1.9f && longest < 2 + 3.1f * dt) << std::format("the rope reached {} m at most", longest);
        expect(scene.world.joint(scene.rope)->value < 2.02f) << std::format("the weight hangs at {} m", scene.world.joint(scene.rope)->value);
        expect(leaning > 0.25f && leaning < 0.3f + 4.4f * dt) << std::format("the capsule leaned {} rad at most", leaning);
        const auto wheel = scene.world.pose(scene.wheel);
        expect(std::abs(wheel->spin.z - 2) < 0.05f) << std::format("the wheel at {} rad/s", wheel->spin.z);
        const auto turned = scene.world.joint(scene.wheel_hinge);
        expect(turned->drive == rigid3::Motor::Mode::velocity && turned->target == 2.0f);
        // Driven back: the motor told to turn the other way from the next step.
        expect(scene.world.drive(scene.wheel_hinge, rigid3::Motor::Mode::velocity, -1).has_value());
        run(scene.world, 60);
        expect(std::abs(scene.world.pose(scene.wheel)->spin.z + 1) < 0.05f) << std::format("driven back: {} rad/s", scene.world.pose(scene.wheel)->spin.z);
        // Held at an angle: the position motor turns the wheel to 1 rad and keeps it there.
        expect(scene.world.drive(scene.wheel_hinge, rigid3::Motor::Mode::position, 1).has_value());
        run(scene.world, 180);
        expect(std::abs(scene.world.joint(scene.wheel_hinge)->value - 1) < 0.02f) << std::format("held at {} rad", scene.world.joint(scene.wheel_hinge)->value);
        expect(!scene.world.drive(scene.rope, rigid3::Motor::Mode::velocity, 1)) << "a rope has no motor";
        // The chain's last ball hangs within three links of the point: its joints hold.
        const auto ball = scene.world.pose(scene.last_ball);
        const float reach = mlibs::math::length(ball->at - rigid3::Vec{4.5f, 6, 4});
        expect(reach < 2.6f) << std::format("{} m from where the chain hangs", reach);
        // A body removed takes its joints.
        const std::size_t joints = scene.world.joints().size();
        scene.world.remove(scene.door);
        expect(eq(scene.world.joints().size(), joints - 1) && !scene.world.joint(scene.door_hinge));
        expect(!scene.world.join(rigid3::Joint::point(scene.door, {}, {0, 0, 0}))) << "a body that is gone";
    };

    "the walker: along the floor, up a 20° ramp, kept back by a 60° one, up a step, and a jump; where it stands is a step's output"_test = [] {
        auto made = rigid3::World::make();
        rigid3::World& world = *made;
        // Each slope's top face meets the floor: the 20° one at x = 3, the 60° one at x = 4.
        const auto floor = *world.add({.kind = rigid3::Kind::fixed, .at = {0, -0.5f, 0}}, rigid3::Shape::box({40, 0.5f, 40}));
        const auto ramp = *world.add({.kind = rigid3::Kind::fixed, .at = {6, 0.879f, 0}, .turn = rigid3::Turn::about({0, 0, 1}, 20 * pi / 180)}, rigid3::Shape::box({3, 0.2f, 1.5f}));
        (void)*world.add({.kind = rigid3::Kind::fixed, .at = {6, 3.064f, 6}, .turn = rigid3::Turn::about({0, 0, 1}, 60 * pi / 180)}, rigid3::Shape::box({6, 0.2f, 1.5f}));
        const auto step = *world.add({.kind = rigid3::Kind::fixed, .at = {4, 0.15f, -6}}, rigid3::Shape::box({1, 0.15f, 1}));
        const auto up = *world.add(rigid3::CharacterDef{.at = {1, 0, 0}});
        const auto steep = *world.add(rigid3::CharacterDef{.at = {1, 0, 6}});
        const auto stair = *world.add(rigid3::CharacterDef{.at = {1, 0, -6}});
        const auto jumper = *world.add(rigid3::CharacterDef{.at = {-6, 0, 0}});
        for (const auto character : {up, steep, stair}) world.walk(character, {1.5f, 0, 0});
        run(world, 30);
        const auto standing = world.grounds();
        expect(eq(standing.size(), std::size_t{4}) && std::ranges::is_sorted(standing, {}, &rigid3::CharacterState::id));
        expect(standing[3].ground == rigid3::Ground::on_ground && standing[3].standing_on == floor) << "the jumper stands on the floor";
        world.jump(jumper, 5);
        float highest = 0;
        for (int index = 0; index < 150; ++index) {
            world.step();
            highest = std::max(highest, world.character(jumper)->at.y);
        }
        expect(highest > 1.0f && highest < 1.5f) << std::format("jumped to {} m (5 m/s: 1.27 m)", highest);
        expect(world.character(jumper)->ground == rigid3::Ground::on_ground) << "and landed";
        const auto climbed = world.character(up), held = world.character(steep), stepped = world.character(stair);
        expect(climbed->at.y > 0.6f && climbed->standing_on == ramp && climbed->ground == rigid3::Ground::on_ground) << std::format("up the 20° ramp to {} m", climbed->at.y);
        expect(held->at.y < 0.5f && held->at.x < 4.5f) << std::format("held back by the 60° one at x {}, y {}", held->at.x, held->at.y);
        expect(stepped->at.x > 4.5f) << std::format("over the 0.3 m step to x {}", stepped->at.x);
        (void)step;
        expect(!world.add(rigid3::CharacterDef{.height = 0.4f, .radius = 0.3f})) << "shorter than it is wide";
    };

    "shape casts, compounds and sensors"_test = [] {
        auto made = rigid3::World::make();
        rigid3::World& world = *made;
        const auto floor = *world.add({.kind = rigid3::Kind::fixed, .at = {0, -0.5f, 0}}, rigid3::Shape::box({20, 0.5f, 20}));
        const auto crate = *world.add({.kind = rigid3::Kind::fixed, .at = {0, 1, 0}}, rigid3::Shape::box({1, 1, 1}));
        // A ball of radius 0.5 swept down from 10 m meets the crate's top at 2: its centre at 2.5, 7.5 m along 20.
        const auto hit = world.cast(rigid3::Shape::sphere(0.5f), {}, {0, 10, 0}, {0, -10, 0});
        expect(fatal(hit.has_value()));
        expect(hit->body == crate && std::abs(hit->fraction - 0.375f) < 1e-3f && std::abs(hit->point.y - 2) < 1e-3f && hit->normal.y > 0.99f)
            << std::format("{} at {}, y {}", hit->body.value, hit->fraction, hit->point.y);
        const auto beside = world.cast(rigid3::Shape::box({0.2f, 0.2f, 0.2f}), {}, {5, 10, 0}, {5, -10, 0});
        expect(beside && beside->body == floor) << "beside the crate: the floor";
        // An L of two equal bars weighs two bars; one part of a movable compound moved changes how it lies.
        const auto bar = *world.add({.at = {8, 5, 0}}, rigid3::Shape::box({0.5f, 0.1f, 0.1f}));
        const auto shape = *world.add({.at = {-8, 5, 0}}, rigid3::Shape::compound({{rigid3::Shape::box({0.5f, 0.1f, 0.1f}), {0.4f, 0, 0}, {}}, {rigid3::Shape::box({0.5f, 0.1f, 0.1f}), {0, 0.4f, 0}, {}}}));
        expect(std::abs(world.pose(shape)->mass - 2 * world.pose(bar)->mass) < 1e-3f) << "an L of two bars";
        const auto movable = *world.add({.kind = rigid3::Kind::fixed, .at = {0, 0.3f, 6}}, rigid3::Shape::compound({{rigid3::Shape::box({0.3f, 0.3f, 0.3f}), {-2, 0, 0}, {}}, {rigid3::Shape::box({0.3f, 0.3f, 0.3f}), {2, 0, 0}, {}}}, true));
        const auto before = world.save();
        expect(world.ray({2, 5, 6}, {2, -5, 6})->body == movable) << "a part at x 2";
        expect(world.move_part(movable, 1, {4, 0, 0}, {}).has_value());
        expect(world.ray({2, 5, 6}, {2, -5, 6})->body == floor && world.ray({4, 5, 6}, {4, -5, 6})->body == movable) << "moved to x 4";
        expect(!world.move_part(crate, 0, {}, {})) << "a box has no parts to move";
        expect(world.restore(before).has_value());
        expect(world.ray({2, 5, 6}, {2, -5, 6})->body == movable) << "restored: where it was";
        // A ball falls through a sensor: its overlap begins and ends, as contacts marked so; nothing stops it.
        const auto sensor = *world.add({.kind = rigid3::Kind::fixed, .at = {-5, 2, 5}, .sensor = true}, rigid3::Shape::box({1, 0.5f, 1}));
        const auto ball = *world.add({.at = {-5, 5, 5}}, rigid3::Shape::sphere(0.25f));
        bool began = false, ended = false, plain = true;
        for (int step = 0; step < 120; ++step) {
            world.step();
            for (const rigid3::Contact& contact : world.contacts()) {
                if (contact.one == sensor || contact.other == sensor) {
                    plain = plain && contact.sensor;
                    began = began || contact.began;
                    ended = ended || (began && !contact.began);
                }
            }
        }
        expect(began && ended && plain) << "the overlap began and ended, marked a sensor's";
        expect(std::abs(world.pose(ball)->at.y - 0.25f) < 0.05f) << std::format("the ball on the floor at {}", world.pose(ball)->at.y);
    };
}};
