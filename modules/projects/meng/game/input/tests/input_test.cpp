// Input: keys to buttons and axes; a tap shorter than a frame still pressed;
// a key's repeat changes nothing; both arrows held cancel; a stick past its
// dead zone wins over keys; the pointer as an axis; focus lost lets go;
// recordings as text, read back as themselves.
import std;
import meng.input;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

namespace {
enum Code : std::uint32_t { left = 1, right = 2, space = 3 };
}

UTest cases{"meng.input", [] {

    auto bindings = [] {
        meng::Bindings b;
        b.key(left, meng::Axis{0, -1});
        b.key(right, meng::Axis{0, 1});
        b.key(space, meng::Button{0});
        b.pad_button(0, meng::Button{0});
        b.pad_axis(0, meng::Axis{0, 1});
        b.pointer_x(meng::Axis{1, 1});
        b.mouse(1, meng::Button{0});
        return b;
    };

    "a tap shorter than a frame is still pressed; held while down; a repeat changes nothing"_test = [&] {
        meng::Sampler in(bindings());
        in.key(space, true);
        in.key(space, false);
        const auto a = in.sample();
        expect(a.went_down(0) && !a.down(0) && (a.released & 1u)) << "pressed and released within the frame";
        in.key(space, true);
        in.key(space, true);  // the key's repeat
        const auto b = in.sample();
        expect(b.down(0) && b.went_down(0));
        const auto c = in.sample();
        expect(c.down(0) && !c.went_down(0)) << "held, not pressed again";
        in.key(space, false);
        (void)in.sample();
        in.mouse(1, true);
        expect(in.sample().went_down(0)) << "the left button is bound to it too";
    };

    "axes: arrows push it, both cancel; a stick past its dead zone wins; the pointer across"_test = [&] {
        meng::Sampler in(bindings());
        in.key(left, true);
        expect(eq(in.sample().axes[0], std::int16_t{-32767}));
        in.key(right, true);
        expect(eq(in.sample().axes[0], std::int16_t{0}));
        in.key(left, false);
        in.pad_axis(0, 0.1);  // inside the dead zone: the key's
        expect(eq(in.sample().axes[0], std::int16_t{32767}));
        in.pad_axis(0, -0.5);
        expect(eq(in.sample().axes[0], std::int16_t{-16384}));
        in.pointer(0.25, 0.5);
        const auto p = in.sample();
        expect(p.axes[1] == -16383 && p.pointer_x == 8192) << std::format("{} {}", p.axes[1], p.pointer_x);  // a quarter across, in steps of 1/32767
        in.pointer(1.5, 0.5);
        expect(in.sample().pointer_x == -1) << "outside: no pointer";
        in.clear();
        const auto gone = in.sample();
        expect(gone.held == 0 && gone.axes[0] == 0) << "focus lost: nothing held";
    };

    "the last device steers: a key that moves an axis puts the pointer aside until it moves again"_test = [&] {
        meng::Sampler in(bindings());
        in.pointer(0.4, 0.5);
        expect(in.sample().pointer_x >= 0) << "the pointer";
        in.key(right, true);
        const auto k = in.sample();
        expect(k.pointer_x == -1 && k.axes[0] == 32767) << "the keys now";
        in.key(right, false);
        expect(in.sample().pointer_x == -1) << "still aside";
        in.pointer(0.6, 0.5);
        expect(in.sample().pointer_x >= 0) << "the pointer moved: its again";
        in.key(space, true);
        expect(in.sample().pointer_x >= 0) << "a button steers nothing: the pointer stays";
    };

    "a gamepad: a button presses, a d-pad pushes an axis as keys do, a repeat changes nothing"_test = [&] {
        meng::Bindings b;
        b.pad_button(15, meng::Axis{0, -1});  // d-pad left
        b.pad_button(16, meng::Axis{0, 1});   // d-pad right
        b.pad_button(0, meng::Button{2});     // south
        meng::Sampler in(b);
        in.pad_button(15, true);
        in.pad_button(15, true);
        expect(eq(in.sample().axes[0], std::int16_t{-32767}));
        in.pad_button(16, true);
        expect(eq(in.sample().axes[0], std::int16_t{0}));
        in.pad_button(15, false);
        in.pad_button(0, true);
        const auto a = in.sample();
        expect(a.axes[0] == 32767 && a.went_down(2)) << std::format("{}", a.axes[0]);
        in.clear();
        expect(eq(in.sample().axes[0], std::int16_t{0}));
        in.pad_button(16, true);  // after a clear, a press counts again
        expect(eq(in.sample().axes[0], std::int16_t{32767}));
    };

    "a recording as text reads back as itself"_test = [&] {
        meng::Sampler in(bindings());
        std::vector<meng::Actions> frames;
        for (int f = 0; f < 50; ++f) {
            in.key(f % 7 == 0 ? space : right, f % 3 != 0);
            in.pointer(f / 50.0, 0.5);
            if (f % 9 == 0) in.wheel(f % 2 ? 1 : -2);
            frames.push_back(in.sample());
        }
        const std::string text = meng::to_text(frames);
        auto back = meng::from_text(text);
        expect(back && *back == frames) << "the same frames";
        expect(!meng::from_text("1 2 3\n").has_value()) << "a short line: refused";
    };

    "a recording: what was played named on its first line, then the frames; read back the same, a bad frame told by its line"_test = [] {
        std::vector<meng::Actions> frames(3);
        frames[1].held = 1, frames[1].pointer_x = 100, frames[2].axes[0] = -32767;
        const std::string text = meng::to_recording("level second-night", frames);
        expect(text.starts_with("level second-night\n"));
        const auto back = meng::from_recording(text);
        expect(fatal(back.has_value())) << (back ? "" : back.error());
        expect(back->heading == "level second-night" && back->frames == frames);
        const auto plain = meng::from_recording(meng::to_recording("", frames));
        expect(plain && plain->heading.empty() && plain->frames == frames) << "no heading: the game's first";
        const auto bad = meng::from_recording("seed 3\n0 0 0 0 0 0 0 -1 -1 0\n1 2\n");
        expect(!bad && bad.error().starts_with("line 3:")) << (bad ? "" : bad.error());
    };

    "an agent's lane: no sooner than its reaction after what it saw, held like a key, rate-limited, the same every time"_test = [] {
        const meng::PlayTier human{.name = "human", .reaction_steps = 12, .actions_per_second = 10};
        const auto right = [] { meng::Actions a; a.axes[0] = 32767; a.held = 1; return a; };
        const auto run = [&] {
            meng::AgentLane lane(human, 120);
            std::vector<meng::Actions> steps;
            for (std::int64_t step = 0; step < 60; ++step) {
                if (step == 5) expect(lane.submit(step, 2, right()) == std::expected<std::int64_t, meng::AgentLane::Refusal>(14)) << "seen at 2: from step 14";
                if (step == 20) expect(lane.submit(step, 20, {}) == std::expected<std::int64_t, meng::AgentLane::Refusal>(32)) << "let go: from 32";
                steps.push_back(lane.take(step));
            }
            return steps;
        };
        const auto steps = run();
        expect(steps[13].axes[0] == 0 && steps[14].axes[0] == 32767 && steps[31].axes[0] == 32767 && steps[32].axes[0] == 0) << "held from 14 to 31";
        expect(steps[14].went_down(0) && !steps[15].went_down(0) && (steps[32].released & 1u)) << "pressed once, released once";
        expect(run() == steps) << "the same submissions, the same steps";
        meng::AgentLane lane(human, 120);
        expect(!lane.submit(10, 11, right()) && lane.submit(10, 11, right()).error() == meng::AgentLane::Refusal::unseen) << "not a step it has not seen";
        int accepted = 0;
        for (int index = 0; index < 15; ++index) accepted += lane.submit(10 + index, 10, right()).has_value();
        expect(accepted == 10) << std::format("ten a second: {}", accepted);
        expect(lane.submit(10 + 120, 130, right()).has_value()) << "a second later, again";
        meng::AgentLane late(human, 120);
        expect(late.submit(100, 40, right()) == std::expected<std::int64_t, meng::AgentLane::Refusal>(101)) << "seen long ago: the next step";
    };
}};
