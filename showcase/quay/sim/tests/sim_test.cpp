// Quay's simulation: the harbour is made; a copy is a branch; a warden behind a container hears a shot and goes to
// where it seemed to come from (further than it was: the steel dulled it), while one that hears nothing keeps its
// round; a player that walks the quay's paths and shoots the wardens it can see lights the beacon before the tide —
// the same frames every time.
import std;
import omui.showcase.quay.sim;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace math = mlibs::math;
namespace nav = xengine::nav;

namespace {

std::int16_t axis_of(float value) { return static_cast<std::int16_t>(std::clamp(value, -1.0f, 1.0f) * 32767); }
float angle_between(float from, float to) {
    float apart = to - from;
    while (apart > 3.14159265f) apart -= 6.2831853f;
    while (apart < -3.14159265f) apart += 6.2831853f;
    return apart;
}

// A player that sees as the watchkeeper does: it turns to a warden in sight (nothing between, within 40 m and
// ahead) and fires when on it; otherwise it walks the quay's path to the beacon and works it there.
struct Player {
    std::vector<quay::Vec3> path;
    std::size_t             next = 0;
    std::uint64_t           planned = 0;
    std::uint32_t           shots = 0;
    quay::Input operator()(const quay::State& state) {
        quay::Input input;
        xengine::Actions& actions = input.player;
        const quay::Vec3 eye = quay::eye(state), look = quay::looking(state);
        // A warden in sight: the nearest.
        std::optional<quay::Vec3> target;
        float nearest = 40;
        for (const quay::Warden& warden : state.wardens) {
            if (warden.health <= 0) continue;
            const quay::Vec3 chest = warden.at + quay::Vec3{0, 1.2f, 0};
            const float distance = math::length(chest - eye);
            if (distance < nearest && math::dot(math::normalized(chest - eye), look) > 0.3f && quay::clear(state, eye, chest)) nearest = distance, target = chest;
        }
        const auto aim_at = [&](quay::Vec3 at) {
            const quay::Vec3 way = math::normalized(at - eye);
            const float yaw = std::atan2(way.x, -way.z), pitch = std::asin(std::clamp(way.y, -1.0f, 1.0f));
            const float turn = angle_between(state.keeper.yaw, yaw), tilt = pitch - state.keeper.pitch;
            actions.axes[2] = axis_of(turn * 8), actions.axes[3] = axis_of(tilt * 8);
            return std::abs(turn) + std::abs(tilt);
        };
        if (target) {
            if (aim_at(*target) < 0.02f) actions.held |= 1u << quay::fire_button, ++shots;
            return input;
        }
        // The way to the beacon, planned again every second.
        if (state.frame >= planned) {
            planned = state.frame + 60;
            if (const auto made = state.ground->path(state.keeper.at, state.beacon); made && !made->corners.empty()) path = made->corners, next = 0;
        }
        while (next < path.size() && math::length(quay::Vec3{path[next].x - state.keeper.at.x, 0, path[next].z - state.keeper.at.z}) < 0.8f) ++next;
        if (next < path.size()) {
            const float off = aim_at({path[next].x, eye.y, path[next].z});
            if (off < 0.6f) actions.axes[1] = axis_of(1);
        } else {
            actions.held |= 1u << quay::use_button;
        }
        return input;
    }
};

struct Played {
    quay::Outcome outcome = quay::Outcome::playing;
    std::string   why;
    std::uint64_t frames = 0, checksum = 0;
    std::size_t   standing = 0;
    std::int32_t  health = 0;
    std::uint32_t shots = 0;  // rounds fired
};
Played play(const quay::Level& level, std::uint64_t most = 60 * 240) {
    quay::State state = quay::start(level);
    Player player;
    while (state.outcome == quay::Outcome::playing && state.frame < most) quay::step(state, player(state));
    return {state.outcome, state.why, state.frame, quay::checksum(state), quay::standing(state), state.keeper.health, level.rounds - state.keeper.rounds};
}

}  // namespace

UTest cases{"quay.sim", [] {
    "the harbour: the watchkeeper at the landward end, four wardens on their rounds, the beacon on the breakwater"_test = [] {
        const quay::State state = quay::start(quay::level::harbour());
        expect(fatal(static_cast<bool>(state.bodies) && static_cast<bool>(state.ground)));
        expect(eq(state.wardens.size(), std::size_t{4}) && eq(state.crates.size(), std::size_t{5}));
        expect(gt(state.ground->polygons(), 10)) << "the quay is walkable";
        const auto there = state.ground->path(state.keeper.at, state.beacon);
        expect(there.has_value() && there->reaches) << "the beacon can be walked to";
    };

    "a copy is a branch: stepped alike they agree, and one pushed on its own goes its own way"_test = [] {
        quay::State one = quay::start(quay::level::harbour());
        quay::Input walk;
        walk.player.axes[1] = 32767;
        for (int frame = 0; frame < 30; ++frame) quay::step(one, walk);
        quay::State other = one;
        for (int frame = 0; frame < 30; ++frame) quay::step(one, walk), quay::step(other, walk);
        expect(eq(quay::checksum(one), quay::checksum(other)));
        quay::Input turn;
        turn.player.axes[2] = 32767;
        quay::step(other, turn);
        quay::step(one, walk);
        expect(ne(quay::checksum(one), quay::checksum(other)));
    };

    "a warden behind a container hears a shot and goes toward it, thinking it further than it was; one that hears nothing keeps its round"_test = [] {
        quay::State state = quay::start(quay::level::harbour());
        // The watchkeeper stands still a second: no warden is drawn.
        for (int frame = 0; frame < 60; ++frame) quay::step(state, {});
        for (const quay::Warden& warden : state.wardens) expect(warden.mind == quay::Mind::patrol);
        // A shot into the air: the nearest warden (on its round 13 m off, across the containers) hears it.
        quay::Input fire;
        fire.player.held = 1u << quay::fire_button;
        fire.player.axes[3] = 32767;
        quay::step(state, fire);
        quay::step(state, {});
        const quay::Warden& near = state.wardens[0];
        const float real = math::length(quay::Vec3{state.keeper.at.x - near.at.x, 0, state.keeper.at.z - near.at.z});
        const float thought = math::length(quay::Vec3{near.heading_for.x - near.at.x, 0, near.heading_for.z - near.at.z});
        std::println("a shot at {:.1f} m: the first warden {} (heard through walls: goes {:.1f} m that way), the one on the breakwater {}", real, quay::mind_names[static_cast<std::size_t>(near.mind)],
                     thought, quay::mind_names[static_cast<std::size_t>(state.wardens[3].mind)]);
        expect(near.mind == quay::Mind::search);
        expect(ge(thought, real * 0.9f)) << "steel dulls a sound: it seems as far or further";
        expect(state.wardens[3].mind == quay::Mind::patrol) << "60 m off, past the containers: nothing heard";
    };

    "a player that walks the paths and shoots what it sees lights the beacon before the tide, the same frames each time"_test = [] {
        const Played first = play(quay::level::harbour());
        std::println("played: {} at frame {} ({:.1f} s) — {}; {} wardens standing, health {}, {} rounds fired", quay::outcome_names[static_cast<std::size_t>(first.outcome)], first.frames,
                     static_cast<double>(first.frames) / quay::steps_per_second, first.why, first.standing, first.health, first.shots);
        expect(first.outcome == quay::Outcome::won) << first.why;
        const Played again = play(quay::level::harbour());
        expect(eq(first.checksum, again.checksum) && eq(first.frames, again.frames));
    };
}};
