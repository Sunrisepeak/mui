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
    quay::Guide guide;
    while (state.outcome == quay::Outcome::playing && state.frame < most) quay::step(state, guide(state));
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
