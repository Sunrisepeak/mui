// The fight from gamepads, injected as a host gives them (spec §6.1): the
// first two connected are players 1 and 2; the d-pad and the left stick are
// directions; the buttons are controls/pad.controls.omui.json's six-button
// layout — a press arrives as the move in the match.
import std;
import openmui;
import openmui.showcase.fighting;
import openmui.testing;

using namespace omui::testing;
using namespace omui;

namespace {

struct Game {
    Headless h{HeadlessOptions{.level = HeadlessLevel::layout, .size = {960, 540}}};
    Game() {
        AppSpec spec = fighting::app();
        spec.setup(h.runtime());
        h.mount("main", spec.view());
        h.step();
    }
    const gongfu::State& state() { return h.runtime().component<fighting::Stage>(h.runtime().find("main"))->match().state(); }
    std::string doing(int player) {
        for (const auto& n : h.tree().snapshot({}).nodes)
            if (n.id == "main") return std::string(n.value.get(player == 0 ? "p1" : "p2")->get("doing")->as_string());
        return {};
    }
};

GamepadEvent connect(std::uint32_t device) { return {.kind = GamepadKind::connected, .device = device, .name = "pad"}; }
GamepadEvent press(std::uint32_t device, GamepadButton b, bool down = true) {
    return {.kind = down ? GamepadKind::down : GamepadKind::up, .device = device, .button = b};
}
GamepadEvent stick(std::uint32_t device, GamepadAxis a, float v) { return {.kind = GamepadKind::axis, .device = device, .axis = a, .value = v}; }

}  // namespace

int main(int argc, char** argv) {
    Suite s{"showcase.fighting.gamepad"};

    s.test("the first two gamepads are the players: player 1's d-pad walks forward, player 2's stick walks back", [] {
        Game g;
        const auto x0 = g.state().f[0].pos.x, y0 = g.state().f[1].pos.x;
        g.h.step({connect(5), connect(9), connect(12), press(5, GamepadButton::dpad_right), stick(9, GamepadAxis::left_x, 1)});
        g.h.advance(seconds(0.5));
        check_eq(g.doing(0), std::string("walk_forward"));
        g.h.step({press(5, GamepadButton::dpad_right, false), stick(9, GamepadAxis::left_x, 0)});
        const auto x1 = g.state().f[0].pos.x, y1 = g.state().f[1].pos.x;
        check(x1 > x0, std::format("player 1 walked from {} to {}", x0.to_double(), x1.to_double()));
        check(y1 > y0, std::format("player 2, facing left, walked back from {} to {}", y0.to_double(), y1.to_double()));
        // A third pad has no seat: its stick moves no one.
        g.h.step({stick(12, GamepadAxis::left_x, -1)});
        g.h.advance(seconds(0.3));
        check(g.state().f[0].pos.x <= x1 + gongfu::Fixed::of(8) && g.state().f[1].pos.x <= y1 + gongfu::Fixed::of(8), "the third pad plays no one");
    });

    s.test("a press arrives as the move: west is LP, R1 is HP, down and south is a crouching LK", [] {
        Game g;
        g.h.step({connect(1)});
        g.h.step({press(1, GamepadButton::west)});
        g.h.advance(seconds(0.05));
        check_eq(g.doing(0), std::string("5LP"));
        g.h.step({press(1, GamepadButton::west, false)});
        g.h.advance(seconds(1));
        g.h.step({press(1, GamepadButton::right_shoulder)});
        g.h.advance(seconds(0.05));
        check_eq(g.doing(0), std::string("5HP"));
        g.h.step({press(1, GamepadButton::right_shoulder, false)});
        g.h.advance(seconds(1.5));
        g.h.step({press(1, GamepadButton::dpad_down)});
        g.h.advance(seconds(0.1));
        g.h.step({press(1, GamepadButton::south)});
        g.h.advance(seconds(0.05));
        check_eq(g.doing(0), std::string("2LK"));
    });

    s.test("unplugged, the seat is free: the next pad connected plays it", [] {
        Game g;
        g.h.step({connect(1), connect(2), GamepadEvent{.kind = GamepadKind::disconnected, .device = 1}, connect(3), press(3, GamepadButton::west)});
        g.h.advance(seconds(0.05));
        check_eq(g.doing(0), std::string("5LP"));
    });

    return s.run(argc, argv);
}
