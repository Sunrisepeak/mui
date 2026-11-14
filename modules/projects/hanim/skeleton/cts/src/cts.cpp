module hanim.skeleton.cts;

import std;
import mlibs.cts;
import mlibs.math;

namespace hanim::skeleton::cts {

namespace {

namespace det = mlibs::math::det;

constexpr float tau = static_cast<float>(det::two_pi);

Quat about(Vec3 axis, float radians) { return Quat::about(axis, radians); }
float wave_of(float phase) { return static_cast<float>(det::sin(static_cast<double>(phase))); }

// The biped's joints by name; each with its parent and its place at rest (y up, the character facing +z).
struct Bone {
    const char* name;
    int         parent;
    Vec3        at;
};
constexpr std::array<Bone, 23> bones{{
    {"hips", -1, {0, 1.0f, 0}},          {"spine", 0, {0, 0.1f, 0}},         {"chest", 1, {0, 0.15f, 0}},       {"upper_chest", 2, {0, 0.15f, 0}},
    {"neck", 3, {0, 0.12f, 0}},          {"head", 4, {0, 0.1f, 0}},          {"shoulder_l", 3, {0.08f, 0.08f, 0}}, {"arm_l", 6, {0.12f, 0, 0}},
    {"forearm_l", 7, {0.26f, 0, 0}},     {"hand_l", 8, {0.24f, 0, 0}},       {"shoulder_r", 3, {-0.08f, 0.08f, 0}}, {"arm_r", 10, {-0.12f, 0, 0}},
    {"forearm_r", 11, {-0.26f, 0, 0}},   {"hand_r", 12, {-0.24f, 0, 0}},     {"thigh_l", 0, {0.1f, -0.05f, 0}}, {"shin_l", 14, {0, -0.42f, 0}},
    {"foot_l", 15, {0, -0.42f, 0}},      {"toe_l", 16, {0, -0.05f, 0.12f}},  {"thigh_r", 0, {-0.1f, -0.05f, 0}}, {"shin_r", 18, {0, -0.42f, 0}},
    {"foot_r", 19, {0, -0.42f, 0}},      {"toe_r", 20, {0, -0.05f, 0.12f}},  {"root", -1, {0, 0, 0}},
}};

std::uint32_t index_of(const Skeleton& skeleton, std::string_view name) { return *skeleton.find(name); }

// A clip of `seconds` keyed `rate` times a second from `pose_at(time, pose)` (the pose starts at rest).
template <typename Formula>
Clip keyed(std::string name, const Skeleton& skeleton, float seconds, Formula pose_at, float rate = 30) {
    Clip clip{.name = std::move(name), .duration = seconds};
    clip.joints.resize(skeleton.size());
    const auto keys = static_cast<std::size_t>(std::lround(seconds * rate)) + 1;
    Pose pose;
    for (std::size_t key = 0; key < keys; ++key) {
        const float time = key + 1 == keys ? seconds : static_cast<float>(key) / rate;
        pose.assign(skeleton.rest().begin(), skeleton.rest().end());
        const auto changed = pose_at(time, pose);
        for (const std::uint32_t joint : changed) {
            JointChannels& channels = clip.joints[joint];
            channels.rotation.times.push_back(time), channels.rotation.values.push_back(pose[joint].rotation);
            channels.translation.times.push_back(time), channels.translation.values.push_back(pose[joint].translation);
        }
    }
    // A channel that never leaves the rest is the rest: left empty.
    for (std::size_t joint = 0; joint < clip.joints.size(); ++joint) {
        JointChannels& channels = clip.joints[joint];
        if (std::ranges::all_of(channels.translation.values, [&](Vec3 at) { return at == skeleton.rest()[joint].translation; })) channels.translation = {};
    }
    return clip;
}

// A gait: legs and arms swinging in opposition at `stride` metres a cycle of `cycle` seconds, the hips bobbing and
// moving forward (root motion).
Clip gait(std::string name, const Skeleton& skeleton, float cycle, float stride, float swing) {
    const std::uint32_t hips = index_of(skeleton, "hips"), spine = index_of(skeleton, "spine");
    const std::array<std::uint32_t, 2> thighs{index_of(skeleton, "thigh_l"), index_of(skeleton, "thigh_r")}, shins{index_of(skeleton, "shin_l"), index_of(skeleton, "shin_r")},
        arms{index_of(skeleton, "arm_l"), index_of(skeleton, "arm_r")}, forearms{index_of(skeleton, "forearm_l"), index_of(skeleton, "forearm_r")};
    Clip clip = keyed(std::move(name), skeleton, cycle, [&](float time, Pose& pose) {
        const float phase = tau * time / cycle;
        std::vector<std::uint32_t> changed{hips, spine};
        pose[hips].translation = {0, 1.0f - 0.03f * stride * wave_of(2 * phase) * wave_of(2 * phase), stride * time / cycle};
        pose[hips].rotation = about({0, 1, 0}, 0.08f * swing * wave_of(phase));
        pose[spine].rotation = about({0, 1, 0}, -0.12f * swing * wave_of(phase));
        for (std::size_t side = 0; side < 2; ++side) {
            const float sign = side == 0 ? 1.0f : -1.0f, swung = sign * wave_of(phase);
            pose[thighs[side]].rotation = about({1, 0, 0}, -0.5f * swing * swung);
            pose[shins[side]].rotation = about({1, 0, 0}, 0.6f * swing * std::max(0.0f, wave_of(phase + sign * 1.2f)));
            pose[arms[side]].rotation = about({0, 0, 1}, sign * -1.2f) * about({1, 0, 0}, 0.45f * swing * swung);
            pose[forearms[side]].rotation = about({0, 1, 0}, sign * 0.3f * swing);
            changed.insert(changed.end(), {thighs[side], shins[side], arms[side], forearms[side]});
        }
        return changed;
    });
    clip.root_motion = RootMotion{hips};
    clip.tags = {"locomotion"};
    clip.markers = {{"foot_l_down", 0}, {"foot_r_down", cycle / 2}};
    return clip;
}

}  // namespace

Skeleton biped(std::size_t joints) {
    std::vector<JointDef> made;
    for (const Bone& bone : bones) {
        if (std::string_view(bone.name) == "root") continue;  // the ground's: not a joint of the biped
        made.push_back({.name = bone.name, .parent = bone.parent, .rest = {.translation = bone.at}});
    }
    // Arms hang at rest a little down from level.
    made[7].rest.rotation = Quat::about({0, 0, 1}, -1.2f), made[11].rest.rotation = Quat::about({0, 0, 1}, 1.2f);
    // Past 23: chains of five under the hands, alternately.
    for (std::size_t chain = 0; made.size() < joints; ++chain) {
        int parent = chain % 2 == 0 ? 9 : 13;
        const float side = chain % 2 == 0 ? 1.0f : -1.0f;
        for (int link = 0; link < 5 && made.size() < joints; ++link) {
            made.push_back({.name = std::format("finger{}_{}", chain, link), .parent = parent, .rest = {.translation = {side * 0.03f, 0, 0.01f * static_cast<float>(chain % 5)}}});
            parent = static_cast<int>(made.size() - 1);
        }
    }
    return *Skeleton::make(std::move(made));
}

std::shared_ptr<Rig> rig(std::size_t joints, bool compressed) {
    auto made = std::make_shared<Rig>(Rig{.skeleton = biped(joints)});
    const Skeleton& skeleton = made->skeleton;
    const std::uint32_t chest = index_of(skeleton, "chest"), head = index_of(skeleton, "head"), spine = index_of(skeleton, "spine");
    const std::uint32_t arm = index_of(skeleton, "arm_r"), forearm = index_of(skeleton, "forearm_r"), hand = index_of(skeleton, "hand_r");
    Clip idle = keyed("idle", skeleton, 2.0f, [&](float time, Pose& pose) {
        const float breath = wave_of(tau * time / 2.0f);
        pose[chest].rotation = about({1, 0, 0}, 0.04f * breath);
        pose[head].rotation = about({0, 1, 0}, 0.1f * wave_of(tau * time / 2.0f + 0.7f));
        std::vector<std::uint32_t> changed{chest, head};
        // Fingers curl and open a little: every joint past the 22 of the body moves.
        for (std::uint32_t joint = 22; joint < skeleton.size(); ++joint) {
            pose[joint].rotation = about({0, 0, 1}, 0.2f * wave_of(tau * time / 2.0f + 0.1f * static_cast<float>(joint)));
            changed.push_back(joint);
        }
        return changed;
    });
    idle.tags = {"idle"};
    Clip walk = gait("walk", skeleton, 1.0f, 1.4f, 1.0f);
    Clip run = gait("run", skeleton, 0.6f, 2.4f, 1.6f);
    Clip wave = keyed("wave", skeleton, 1.5f, [&](float time, Pose& pose) {
        const float raised = std::min(1.0f, time * 3.0f);
        pose[arm].rotation = about({0, 0, 1}, 1.2f - 2.6f * raised);
        pose[forearm].rotation = about({0, 0, 1}, -0.6f * raised + 0.5f * raised * wave_of(tau * time * 2));
        pose[hand].rotation = about({1, 0, 0}, 0.2f * wave_of(tau * time * 2));
        return std::vector<std::uint32_t>{arm, forearm, hand};
    });
    Clip lean = keyed("lean", skeleton, 1.0f, [&](float time, Pose& pose) {
        pose[spine].rotation = about({1, 0, 0}, 0.35f * wave_of(tau * time / 2.0f));
        return std::vector<std::uint32_t>{spine};
    });
    made->clips = {std::move(idle), std::move(walk), std::move(run), std::move(wave), std::move(lean)};
    if (compressed)
        for (Clip& clip : made->clips) (void)clip.compress(skeleton);
    return made;
}

std::shared_ptr<Machine> machine(const Rig& rig) {
    auto made = std::make_shared<Machine>();
    made->states = {{.name = "idle", .clip = *rig.clip("idle")}, {.name = "walk", .clip = *rig.clip("walk")}, {.name = "run", .clip = *rig.clip("run")}};
    made->parameters = {"speed", "halt"};
    using Kind = Condition::Kind;
    made->transitions = {
        {.from = any_state, .to = 0, .fade = 0.3f, .when = {{.kind = Kind::trigger, .parameter = 1}}},
        {.from = 0, .to = 1, .fade = 0.25f, .when = {{.kind = Kind::above, .parameter = 0, .value = 0.5f}}},
        {.from = 1, .to = 2, .fade = 0.25f, .when = {{.kind = Kind::above, .parameter = 0, .value = 2.5f}}},
        {.from = 2, .to = 1, .fade = 0.25f, .when = {{.kind = Kind::below, .parameter = 0, .value = 2.5f}}},
        {.from = 1, .to = 0, .fade = 0.3f, .when = {{.kind = Kind::below, .parameter = 0, .value = 0.5f}}},
    };
    return made;
}

Crowd crowd(std::size_t characters, unsigned workers) {
    static const std::shared_ptr<const Rig> shared_rig = rig();
    static const std::shared_ptr<const Machine> shared_machine = machine(*shared_rig);
    Crowd made(shared_rig, shared_machine, 1.0f / 60, workers);
    for (std::size_t index = 0; index < characters; ++index) {
        const auto row = static_cast<float>(index / 4), column = static_cast<float>(index % 4);
        const std::uint32_t added = made.add({.moved = {column * 2.0f, 0, row * 3.0f}});
        (void)made.characters()[added].stage.set("speed", static_cast<float>(index % 3) * 1.6f);
    }
    return made;
}

void Family::disturb(State& state) const {
    std::size_t index = 0;
    for (Character& character : state.characters()) {
        (void)character.stage.set("speed", 3.0f);
        if (index % 2 == 0) (void)character.player.layer(0, {.clip = 3, .weight = 1, .mask = state.rig()->skeleton.find("upper_chest")});
        if (index % 3 == 0) (void)character.stage.set("halt", 1);
        ++index;
    }
}

void Family::read(const State& state) const {
    for (const Character& character : state.characters()) {
        (void)character.player.world(5);
        (void)character.stage.current();
    }
}

void declare(Family family, mlibs::cts::world::Run run) { mlibs::cts::world::declare(family, contract, run); }

}  // namespace hanim::skeleton::cts
