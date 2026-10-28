// The clock and the timeline: 120 steps a second, a stall not owed, half as
// many at half speed; any kept frame reached exactly by a seek, none outside;
// a branch from a seek forgets what came after it, and the same inputs played
// again reach the same bytes as playing straight through.
import std;
import mgame.sim;
import openmui.testing;

using namespace omui::testing;

namespace {

// A toy game: a state that mixes every input into itself.
struct Toy {
    struct State {
        std::uint64_t mix = 0x9e3779b97f4a7c15ull;
        std::int64_t  sum = 0;
    };
    struct Input {
        std::int32_t value = 0;
    };
    static void step(State& s, const Input& in) {
        s.mix = (s.mix ^ static_cast<std::uint64_t>(in.value)) * 0x100000001b3ull + 0x7f4a7c15ull;
        s.sum += in.value;
    }
};

Toy::Input input_for(std::int64_t f, std::int32_t salt = 0) { return {static_cast<std::int32_t>((f * 2654435761ll + salt) % 1000)}; }

}  // namespace

int main(int argc, char** argv) {
    Suite s{"mgame.sim"};

    s.test("the clock: 120 steps a second in sixtieths; a stall is not owed past its limit; at half speed, half as many", [] {
        mgame::Clock c(120, 8);
        check_eq(c.advance(10.0), std::int32_t{0});  // its start
        int steps = 0;
        for (int i = 1; i <= 60; ++i) steps += c.advance(10.0 + i / 60.0);
        check_eq(steps, 120);
        check(c.advance(10.0 + 1 + 1.0 / 240) == 0 && std::abs(c.alpha() - 0.5) < 1e-9, std::format("half a step on: {}", c.alpha()));
        check_eq(c.advance(20.0), std::int32_t{8});  // nine seconds of stall: eight steps, the rest forgiven
        c.scale = 0.5;
        steps = 0;
        for (int i = 1; i <= 60; ++i) steps += c.advance(20.0 + i / 60.0);
        check(steps >= 59 && steps <= 61, std::format("{} at half speed", steps));
    });

    s.test("the timeline: any kept frame reached exactly; none outside what is kept", [] {
        mgame::Timeline<Toy> t(Toy::State{}, 600, 60);
        std::vector<Toy::State> straight{t.state()};
        for (std::int64_t f = 0; f < 2000; ++f) {
            t.step(input_for(f));
            straight.push_back(t.state());
        }
        check(t.frame() == 2000 && t.earliest() == 1440, std::format("kept from {}", t.earliest()));
        for (const std::int64_t f : {1440ll, 1441ll, 1500ll, 1777ll, 1999ll, 2000ll}) {
            check(t.seek(f), std::format("frame {}", f));
            check(t.state().mix == straight[static_cast<std::size_t>(f)].mix && t.state().sum == straight[static_cast<std::size_t>(f)].sum, std::format("frame {} exactly", f));
        }
        check(!t.seek(1439) && !t.seek(2001), "outside what is kept: refused");
    });

    s.test("a branch: back two seconds, other inputs, a new future; the same inputs again reach the same bytes as straight through", [] {
        mgame::Timeline<Toy> t(Toy::State{}, 3600, 60);
        for (std::int64_t f = 0; f < 1000; ++f) t.step(input_for(f));
        const Toy::State end = t.state();
        check(t.seek(760));
        for (std::int64_t f = 760; f < 1000; ++f) t.step(input_for(f, 7));  // another future
        check(t.latest() == 1000 && t.state().mix != end.mix, "a different future");
        check(!t.seek(1001), "the old future is gone");
        check(t.seek(760));
        for (std::int64_t f = 760; f < 1000; ++f) t.step(input_for(f));  // the first one's inputs again
        check(t.state().mix == end.mix && t.state().sum == end.sum, "the same bytes as straight through");
    });

    return s.run(argc, argv);
}
