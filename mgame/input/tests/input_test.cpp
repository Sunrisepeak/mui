// Input: keys to buttons and axes; a tap shorter than a frame still pressed;
// a key's repeat changes nothing; both arrows held cancel; a stick past its
// dead zone wins over keys; the pointer as an axis; focus lost lets go;
// recordings as text, read back as themselves.
import std;
import mgame.input;
import openmui.testing;

using namespace omui::testing;

namespace {
enum Code : std::uint32_t { left = 1, right = 2, space = 3 };
}

int main(int argc, char** argv) {
    Suite s{"mgame.input"};

    auto bindings = [] {
        mgame::Bindings b;
        b.key(left, mgame::Axis{0, -1});
        b.key(right, mgame::Axis{0, 1});
        b.key(space, mgame::Button{0});
        b.pad_button(0, mgame::Button{0});
        b.pad_axis(0, mgame::Axis{0, 1});
        b.pointer_x(mgame::Axis{1, 1});
        b.mouse(1, mgame::Button{0});
        return b;
    };

    s.test("a tap shorter than a frame is still pressed; held while down; a repeat changes nothing", [&] {
        mgame::Sampler in(bindings());
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
        mgame::Sampler in(bindings());
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

    s.test("a gamepad: a button presses, a d-pad pushes an axis as keys do, a repeat changes nothing", [&] {
        mgame::Bindings b;
        b.pad_button(15, mgame::Axis{0, -1});  // d-pad left
        b.pad_button(16, mgame::Axis{0, 1});   // d-pad right
        b.pad_button(0, mgame::Button{2});     // south
        mgame::Sampler in(b);
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
        mgame::Sampler in(bindings());
        std::vector<mgame::Actions> frames;
        for (int f = 0; f < 50; ++f) {
            in.key(f % 7 == 0 ? space : right, f % 3 != 0);
            in.pointer(f / 50.0, 0.5);
            if (f % 9 == 0) in.wheel(f % 2 ? 1 : -2);
            frames.push_back(in.sample());
        }
        const std::string text = mgame::to_text(frames);
        auto back = mgame::from_text(text);
        check(back && *back == frames, "the same frames");
        check(!mgame::from_text("1 2 3\n").has_value(), "a short line: refused");
    });

    return s.run(argc, argv);
}
