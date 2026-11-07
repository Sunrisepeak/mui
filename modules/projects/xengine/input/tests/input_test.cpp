// Input: keys to buttons and axes; a tap shorter than a frame still pressed;
// a key's repeat changes nothing; both arrows held cancel; a stick past its
// dead zone wins over keys; the pointer as an axis; focus lost lets go;
// recordings as text, read back as themselves.
import std;
import xengine.input;
import mlibs.testing;

using namespace mlibs::testing;

namespace {
enum Code : std::uint32_t { left = 1, right = 2, space = 3 };
}

int main(int argc, char** argv) {
    Suite s{"xengine.input"};

    auto bindings = [] {
        xengine::Bindings b;
        b.key(left, xengine::Axis{0, -1});
        b.key(right, xengine::Axis{0, 1});
        b.key(space, xengine::Button{0});
        b.pad_button(0, xengine::Button{0});
        b.pad_axis(0, xengine::Axis{0, 1});
        b.pointer_x(xengine::Axis{1, 1});
        b.mouse(1, xengine::Button{0});
        return b;
    };

    s.test("a tap shorter than a frame is still pressed; held while down; a repeat changes nothing", [&] {
        xengine::Sampler in(bindings());
        in.key(space, true);
        in.key(space, false);
        const auto a = in.sample();
        check(a.went_down(0) && !a.down(0) && (a.released & 1u), "pressed and released within the frame");
        in.key(space, true);
        in.key(space, true);  // the key's repeat
        const auto b = in.sample();
        check(b.down(0) && b.went_down(0));
        const auto c = in.sample();
        check(c.down(0) && !c.went_down(0), "held, not pressed again");
        in.key(space, false);
        (void)in.sample();
        in.mouse(1, true);
        check(in.sample().went_down(0), "the left button is bound to it too");
    });

    s.test("axes: arrows push it, both cancel; a stick past its dead zone wins; the pointer across", [&] {
        xengine::Sampler in(bindings());
        in.key(left, true);
        check_eq(in.sample().axes[0], std::int16_t{-32767});
        in.key(right, true);
        check_eq(in.sample().axes[0], std::int16_t{0});
        in.key(left, false);
        in.pad_axis(0, 0.1);  // inside the dead zone: the key's
        check_eq(in.sample().axes[0], std::int16_t{32767});
        in.pad_axis(0, -0.5);
        check_eq(in.sample().axes[0], std::int16_t{-16384});
        in.pointer(0.25, 0.5);
        const auto p = in.sample();
        check(p.axes[1] == -16383 && p.pointer_x == 8192, std::format("{} {}", p.axes[1], p.pointer_x));  // a quarter across, in steps of 1/32767
        in.pointer(1.5, 0.5);
        check(in.sample().pointer_x == -1, "outside: no pointer");
        in.clear();
        const auto gone = in.sample();
        check(gone.held == 0 && gone.axes[0] == 0, "focus lost: nothing held");
    });

    s.test("the last device steers: a key that moves an axis puts the pointer aside until it moves again", [&] {
        xengine::Sampler in(bindings());
        in.pointer(0.4, 0.5);
        check(in.sample().pointer_x >= 0, "the pointer");
        in.key(right, true);
        const auto k = in.sample();
        check(k.pointer_x == -1 && k.axes[0] == 32767, "the keys now");
        in.key(right, false);
        check(in.sample().pointer_x == -1, "still aside");
        in.pointer(0.6, 0.5);
        check(in.sample().pointer_x >= 0, "the pointer moved: its again");
        in.key(space, true);
        check(in.sample().pointer_x >= 0, "a button steers nothing: the pointer stays");
    });

    s.test("a gamepad: a button presses, a d-pad pushes an axis as keys do, a repeat changes nothing", [&] {
        xengine::Bindings b;
        b.pad_button(15, xengine::Axis{0, -1});  // d-pad left
        b.pad_button(16, xengine::Axis{0, 1});   // d-pad right
        b.pad_button(0, xengine::Button{2});     // south
        xengine::Sampler in(b);
        in.pad_button(15, true);
        in.pad_button(15, true);
        check_eq(in.sample().axes[0], std::int16_t{-32767});
        in.pad_button(16, true);
        check_eq(in.sample().axes[0], std::int16_t{0});
        in.pad_button(15, false);
        in.pad_button(0, true);
        const auto a = in.sample();
        check(a.axes[0] == 32767 && a.went_down(2), std::format("{}", a.axes[0]));
        in.clear();
        check_eq(in.sample().axes[0], std::int16_t{0});
        in.pad_button(16, true);  // after a clear, a press counts again
        check_eq(in.sample().axes[0], std::int16_t{32767});
    });

    s.test("a recording as text reads back as itself", [&] {
        xengine::Sampler in(bindings());
        std::vector<xengine::Actions> frames;
        for (int f = 0; f < 50; ++f) {
            in.key(f % 7 == 0 ? space : right, f % 3 != 0);
            in.pointer(f / 50.0, 0.5);
            if (f % 9 == 0) in.wheel(f % 2 ? 1 : -2);
            frames.push_back(in.sample());
        }
        const std::string text = xengine::to_text(frames);
        auto back = xengine::from_text(text);
        check(back && *back == frames, "the same frames");
        check(!xengine::from_text("1 2 3\n").has_value(), "a short line: refused");
    });

    s.test("an agent's lane: no sooner than its reaction after what it saw, held like a key, rate-limited, the same every time", [] {
        const xengine::PlayTier human{.name = "human", .reaction_steps = 12, .actions_per_second = 10};
        const auto right = [] { xengine::Actions a; a.axes[0] = 32767; a.held = 1; return a; };
        const auto run = [&] {
            xengine::AgentLane lane(human, 120);
            std::vector<xengine::Actions> steps;
            for (std::int64_t step = 0; step < 60; ++step) {
                if (step == 5) check(lane.submit(step, 2, right()) == std::expected<std::int64_t, xengine::AgentLane::Refusal>(14), "seen at 2: from step 14");
                if (step == 20) check(lane.submit(step, 20, {}) == std::expected<std::int64_t, xengine::AgentLane::Refusal>(32), "let go: from 32");
                steps.push_back(lane.take(step));
            }
            return steps;
        };
        const auto steps = run();
        check(steps[13].axes[0] == 0 && steps[14].axes[0] == 32767 && steps[31].axes[0] == 32767 && steps[32].axes[0] == 0, "held from 14 to 31");
        check(steps[14].went_down(0) && !steps[15].went_down(0) && (steps[32].released & 1u), "pressed once, released once");
        check(run() == steps, "the same submissions, the same steps");
        xengine::AgentLane lane(human, 120);
        check(!lane.submit(10, 11, right()) && lane.submit(10, 11, right()).error() == xengine::AgentLane::Refusal::unseen, "not a step it has not seen");
        int accepted = 0;
        for (int index = 0; index < 15; ++index) accepted += lane.submit(10 + index, 10, right()).has_value();
        check(accepted == 10, std::format("ten a second: {}", accepted));
        check(lane.submit(10 + 120, 130, right()).has_value(), "a second later, again");
        xengine::AgentLane late(human, 120);
        check(late.submit(100, 40, right()) == std::expected<std::int64_t, xengine::AgentLane::Refusal>(101), "seen long ago: the next step");
    });

    return s.run(argc, argv);
}
