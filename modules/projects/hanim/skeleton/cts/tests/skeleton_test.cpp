// hanim.skeleton on the procedural biped: the world contract on a crowd (on one thread and on four), sampling as a
// pure function of time, ACL's compression within its error (said and measured between its samples too), blending
// by weight, by joint and by difference, root motion carrying a walker forward, the machine's fades (interrupted
// without a jump), and the skin's matrices at rest. The crowd's bits are pinned: the same on every platform CI runs.
import std;
import mlibs.cts;
import mlibs.math;
import hanim.skeleton;
import hanim.skeleton.cts;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace skeleton = hanim::skeleton;

// This program counts its allocations (its own: one program a file): a crowd's step allocates nothing.
namespace {
std::atomic<std::size_t> allocations{0};
}  // namespace
void* operator new(std::size_t size) {
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* got = std::malloc(size ? size : 1)) return got;
    std::abort();
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }

namespace {

float distance(const skeleton::Mat4& one, const skeleton::Mat4& other) {
    return mlibs::math::length(skeleton::Vec3{one.at(3, 0) - other.at(3, 0), one.at(3, 1) - other.at(3, 1), one.at(3, 2) - other.at(3, 2)});
}
float rotation_gap(const skeleton::Quat& one, const skeleton::Quat& other) {
    return 1.0f - std::abs(mlibs::math::dot(one, other));
}

}  // namespace

UTest cases{"hanim.skeleton", [] {
    "the world contract on a crowd of characters, on one thread and on four"_test = [] {
        mlibs::cts::against({.subject = "hanim.skeleton"}, [] { skeleton::cts::declare(skeleton::cts::Family{}); });
        mlibs::cts::against({.subject = "hanim.skeleton (4 workers)"}, [] { skeleton::cts::declare(skeleton::cts::Family{.workers = 4}); });
    };

    "the same bits on any number of workers, pinned"_test = [] {
        std::vector<std::uint64_t> hashes;
        for (const unsigned workers : {0u, 1u, 4u}) {
            skeleton::Crowd crowd = skeleton::cts::crowd(16, workers);
            for (int step = 0; step < 240; ++step) {
                if (step == 100) skeleton::cts::Family{}.disturb(crowd);
                crowd.step();
            }
            hashes.push_back(crowd.state_hash());
        }
        std::println("crowd of 16, 240 steps: {:016x}", hashes[0]);
        expect(hashes[0] == hashes[1] && hashes[1] == hashes[2]) << "0, 1 and 4 workers";
        expect(eq(hashes[0], 0x995eb6bd571fddd9ull)) << std::format("pinned: {:016x}", hashes[0]);
    };

    "a clip is a pure function of time: its keys where they are, the same pose for the same time"_test = [] {
        const auto rig = skeleton::cts::rig(23, false);
        const skeleton::Clip& walk = rig->clips[*rig->clip("walk")];
        skeleton::Pose one(rig->skeleton.size()), two(rig->skeleton.size());
        skeleton::sample_raw(walk, rig->skeleton, 0.37f, one);
        skeleton::sample(walk, rig->skeleton, 0.5f, two);
        skeleton::sample_raw(walk, rig->skeleton, 0.37f, two);
        expect(one == two) << "sampled again, out of order";
        const std::uint32_t thigh = *rig->skeleton.find("thigh_l");
        const auto& keys = walk.joints[thigh].rotation;
        skeleton::sample_raw(walk, rig->skeleton, keys.times[7], one);
        expect(one[thigh].rotation == keys.values[7]) << "at a key, the key";
        skeleton::sample_raw(walk, rig->skeleton, -1.0f, one);
        expect(one[thigh].rotation == keys.values.front()) << "before the start: the first";
        expect(walk.joints[*rig->skeleton.find("head")].rotation.empty()) << "a joint the walk does not move has no channel";
    };

    "compressed by ACL within the error said, measured between its samples too"_test = [] {
        const auto rig = skeleton::cts::rig(23, true);
        for (const skeleton::Clip& clip : rig->clips) {
            expect(fatal(clip.compressed.has_value())) << clip.name;
            const skeleton::Fidelity fidelity = skeleton::measure(clip, rig->skeleton, 240);
            std::println("{:5}: {:6} bytes raw, {:5} compressed ({:.1f}×); ACL's error {:.4f} mm, measured at 240/s {:.4f} mm (joint {}, {:.3f} s)", clip.name, clip.bytes(),
                         clip.compressed->bytes(), static_cast<double>(clip.bytes()) / static_cast<double>(clip.compressed->bytes()), clip.compressed->error() * 1000,
                         fidelity.worst * 1000, rig->skeleton.names()[fidelity.joint], fidelity.time);
            expect(clip.compressed->error() < 0.00015f) << clip.name << ": ACL's own measure, 0.1 mm asked";
            expect(fidelity.worst < 0.0005f) << clip.name << ": between samples, 0.5 mm at most";
            expect(clip.compressed->bytes() < clip.bytes()) << clip.name;
        }
        const auto again = skeleton::cts::rig(23, true);
        expect(again->clips[1].compressed->hash() == rig->clips[1].compressed->hash()) << "the same clip, the same bytes";
    };

    "blends: by a weight, by a joint's subtree, and a difference added back"_test = [] {
        const auto rig = skeleton::cts::rig(23, false);
        const std::size_t joints = rig->skeleton.size();
        skeleton::Pose walk(joints), wave(joints), out(joints), delta(joints), rest(rig->skeleton.rest().begin(), rig->skeleton.rest().end());
        skeleton::sample(rig->clips[1], rig->skeleton, 0.3f, walk);
        skeleton::sample(rig->clips[3], rig->skeleton, 0.8f, wave);
        skeleton::blend(walk, wave, 0.0f, out);
        expect(out == walk);
        skeleton::blend(walk, wave, 1.0f, out);
        expect(out == wave);
        const auto mask = rig->skeleton.mask(*rig->skeleton.find("upper_chest"));
        skeleton::blend(walk, wave, mask, out);
        const std::uint32_t thigh = *rig->skeleton.find("thigh_l"), arm = *rig->skeleton.find("arm_r");
        expect(out[thigh] == walk[thigh] && out[arm] == wave[arm]) << "the legs walk, the arm waves";
        skeleton::sample(rig->clips[4], rig->skeleton, 0.5f, out);
        skeleton::difference(out, rest, delta);
        skeleton::Pose added(joints);
        skeleton::add(rest, delta, 1.0f, {}, added);
        const std::uint32_t spine = *rig->skeleton.find("spine");
        expect(rotation_gap(added[spine].rotation, out[spine].rotation) < 1e-6f) << "rest + (lean − rest) = lean";
        skeleton::add(walk, delta, 0.0f, {}, added);
        expect(added == walk) << "added by 0: nothing";
    };

    "root motion: a walk carries its character forward 1.4 m a lap, its hips in place"_test = [] {
        const auto rig = skeleton::cts::rig(23, true);
        const skeleton::Clip& walk = rig->clips[1];
        skeleton::Pose pose(rig->skeleton.size());
        skeleton::sample(walk, rig->skeleton, 0.5f, pose);
        expect(std::abs(pose[0].translation.z) < 1e-4f && std::abs(pose[0].translation.x) < 1e-4f) << "the hips in place";
        const skeleton::Motion lap = skeleton::motion(walk, rig->skeleton, 0.0f, 1.0f);
        expect(std::abs(lap.moved.z - 1.4f) < 1e-4f) << lap.moved.z;
        const skeleton::Motion two = skeleton::motion(walk, rig->skeleton, 0.25f, 0.25f, 2);
        // In the space the character was in at a quarter (turned a little by the hips' sway): the distance is two laps'.
        expect(std::abs(mlibs::math::length(two.moved) - 2.8f) < 1e-3f) << "two laps round from a quarter: " << mlibs::math::length(two.moved);
        skeleton::Crowd crowd(rig, skeleton::cts::machine(*rig));
        crowd.add();
        (void)crowd.characters()[0].stage.set("speed", 1.4f);  // else the machine walks it back to idle
        (void)crowd.characters()[0].stage.go("walk", 0);
        for (int step = 0; step < 120; ++step) crowd.step();
        const auto at = crowd.characters()[0].player.placed().moved;
        expect(std::abs(at.z - 2.8f) < 0.05f && std::abs(at.x) < 0.05f) << std::format("two seconds of walking: at ({:.3f}, {:.3f})", at.x, at.z);
    };

    "the machine: walk, then run by speed; a fade with no jump, interrupted with none either; a halt taken once"_test = [] {
        const auto rig = skeleton::cts::rig(23, true);
        skeleton::Crowd crowd(rig, skeleton::cts::machine(*rig));
        crowd.add();
        skeleton::Character& hero = crowd.characters()[0];
        const std::uint32_t hand = *rig->skeleton.find("hand_r");
        (void)hero.stage.set("speed", 3.0f);
        float largest = 0;
        skeleton::Mat4 before = hero.player.model()[hand];
        std::vector<std::string> states;
        for (int step = 0; step < 90; ++step) {
            if (step == 20) (void)hero.stage.set("speed", 1.0f);  // back to walk while it is still fading toward run
            crowd.step();
            float total = 0;
            for (std::uint32_t item = 0; item < hero.player.chosen().count; ++item) total += hero.player.chosen().playing[item].weight;
            expect(std::abs(total - 1.0f) < 1e-5f) << "weights sum to 1 at step " << step;
            largest = std::max(largest, distance(before, hero.player.model()[hand]));
            before = hero.player.model()[hand];
            const std::string state = hero.stage.machine().states[hero.stage.current()].name;
            if (states.empty() || states.back() != state) states.push_back(state);
        }
        expect(states == std::vector<std::string>{"walk", "run", "walk"}) << "idle → walk at once (speed 3 > 0.5), → run, → walk";
        expect(largest < 0.08f) << std::format("the hand moves at most {:.4f} m in a step, through fades and an interruption", largest);
        (void)hero.stage.set("halt", 1);
        crowd.step();
        expect(hero.stage.machine().states[hero.stage.current()].name == std::string("idle") && hero.stage.parameters()[1] == 0.0f) << "the trigger taken";
        expect(hero.stage.last().transition == 0u) << "by the first transition";
    };

    "layers: a wave on the upper body over a walk, a lean added"_test = [] {
        const auto rig = skeleton::cts::rig(23, true);
        skeleton::Crowd crowd(rig, skeleton::cts::machine(*rig));
        crowd.add(), crowd.add();
        for (skeleton::Character& character : crowd.characters()) (void)character.stage.set("speed", 1.4f), (void)character.stage.go("walk", 0);
        skeleton::Character& waving = crowd.characters()[1];
        expect(waving.player.layer(0, {.clip = 3, .weight = 1, .mask = rig->skeleton.find("upper_chest")}).has_value());
        expect(waving.player.layer(1, {.clip = 4, .weight = 1, .additive = true}).has_value());
        expect(!waving.player.layer(9, {.clip = 3, .weight = 1}).has_value() && !waving.player.layer(2, {.clip = 99, .weight = 1}).has_value());
        for (int step = 0; step < 45; ++step) crowd.step();
        const auto& plain = crowd.characters()[0].player;
        const std::uint32_t thigh = *rig->skeleton.find("thigh_l"), arm = *rig->skeleton.find("arm_r"), spine = *rig->skeleton.find("spine");
        expect(plain.pose()[thigh] == waving.player.pose()[thigh]) << "the legs walk alike";
        expect(!(plain.pose()[arm] == waving.player.pose()[arm]) && !(plain.pose()[spine].rotation == waving.player.pose()[spine].rotation)) << "the arm waves, the spine leans";
    };

    "measured (printed, not judged; the benchmark motion/skeleton-* is the gate): 64 × 50 joints, ACL beside the raw channels, and a crowd's step"_test = [] {
        using clock = std::chrono::steady_clock;
        constexpr std::size_t characters = 64, joints = 50;
        const auto rig = skeleton::cts::rig(joints, true);
        const skeleton::Clip& walk = rig->clips[1];
        std::vector<skeleton::Pose> poses(characters, skeleton::Pose(joints));
        std::vector<double> packed, raw, stepped;
        for (int round = 0; round < 400; ++round) {
            const float base = static_cast<float>(round % 600) / 600.0f;
            const auto from = clock::now();
            for (std::size_t index = 0; index < characters; ++index) walk.compressed->sample(std::fmod(base + static_cast<float>(index) * 0.013f, walk.duration), poses[index]);
            const auto between = clock::now();
            for (std::size_t index = 0; index < characters; ++index) skeleton::sample_raw(walk, rig->skeleton, std::fmod(base + static_cast<float>(index) * 0.013f, walk.duration), poses[index]);
            const auto to = clock::now();
            packed.push_back(std::chrono::duration<double, std::nano>(between - from).count());
            raw.push_back(std::chrono::duration<double, std::nano>(to - between).count());
        }
        skeleton::Crowd crowd(rig, skeleton::cts::machine(*rig));
        for (std::size_t index = 0; index < characters; ++index) {
            const std::uint32_t added = crowd.add();
            (void)crowd.characters()[added].stage.set("speed", static_cast<float>(index % 3) * 1.6f);
            if (index % 2 == 0) (void)crowd.characters()[added].player.layer(0, {.clip = 3, .weight = 1, .mask = rig->skeleton.find("upper_chest")});
        }
        for (int step = 0; step < 600; ++step) {
            const auto from = clock::now();
            crowd.step();
            stepped.push_back(std::chrono::duration<double, std::micro>(clock::now() - from).count());
        }
        const std::size_t before = allocations.load();
        for (int step = 0; step < 120; ++step) {
            if (step % 30 == 0) (void)crowd.characters()[static_cast<std::size_t>(step) % characters].stage.set("speed", static_cast<float>(step % 3) * 1.6f);
            crowd.step();
        }
        expect(eq(allocations.load() - before, 0u)) << "a crowd's step, fades and layers included, allocates nothing";
        const auto median = [](std::vector<double> values) { std::ranges::sort(values); return values[values.size() / 2]; };
        const auto p99 = [](std::vector<double> values) { std::ranges::sort(values); return values[values.size() * 99 / 100]; };
        const double each = static_cast<double>(characters * joints);
        std::println("sampling {} × {} joints: ACL {:.1f} ns a joint, raw channels {:.1f} ns a joint ({:.2f}×); a crowd's step {:.1f} µs median, {:.1f} µs p99 ({:.2f} µs a character)",
                     characters, joints, median(packed) / each, median(raw) / each, median(packed) / median(raw), median(stepped), p99(stepped), median(stepped) / characters);
    };

    "the skin at rest: every joint's matrix is the identity; a skeleton refused for what it cannot be"_test = [] {
        const auto rig = skeleton::cts::rig(23, false);
        skeleton::Crowd crowd(rig, std::make_shared<skeleton::Machine>(skeleton::Machine{.states = {{.name = "still", .clip = 0}}}));
        crowd.add();
        skeleton::Player player(rig);
        float off = 0;
        for (const skeleton::Mat4& matrix : player.skin())
            for (std::size_t index = 0; index < 16; ++index) off = std::max(off, std::abs(matrix.values[index] - (index % 5 == 0 ? 1.0f : 0.0f)));
        expect(off < 1e-5f) << off;
        expect(!skeleton::Skeleton::make({{"a", 0, {}}}).has_value()) << "its own parent";
        expect(!skeleton::Skeleton::make({{"a", -1, {}}, {"a", 0, {}}}).has_value()) << "two of one name";
        expect(!skeleton::Machine{.states = {{.name = "x", .clip = 99}}}.check(*rig).has_value()) << "a clip the rig has not";
    };
}};
