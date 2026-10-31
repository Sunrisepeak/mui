// The breakout from a gamepad, injected as a host gives it (spec §6.1): the
// d-pad and the left stick move the paddle, south launches the ball.
import std;
import openmui;
import openmui.showcase.widget_breakout;
import openmui.testing;

using namespace omui::testing;
using namespace omui;
namespace bo = omui::breakout;

namespace {

Value field_value(Headless& h) {
    for (const auto& n : h.tree().snapshot({}).nodes)
        if (n.id == "main/field") return n.value;
    return {};
}

double paddle(Headless& h) { return field_value(h).get("paddle")->as_double(); }  // (the value lives to the end of the expression)

bool waiting(Headless& h) {
    const Value v = field_value(h);
    const Value* balls = v.get("balls");
    if (!balls || !balls->list() || balls->list()->empty()) return false;
    return balls->list()->front().get("on_paddle")->as_bool();
}

GamepadEvent pad(GamepadKind kind, GamepadButton b = GamepadButton::south) { return {.kind = kind, .device = 1, .button = b}; }

}  // namespace

int main(int argc, char** argv) {
    Suite s{"widget_breakout.gamepad"};

    s.test("the d-pad and the stick move the paddle; south launches", [] {
        Headless h{HeadlessOptions{.level = HeadlessLevel::layout, .size = {1280, 720}}};
        AppSpec spec = bo::app();
        h.runtime().set_theme(spec.theme);
        spec.setup(h.runtime());
        h.mount("main", spec.view());
        h.step();
        h.advance(seconds(0.25));
        h.runtime().focus(h.runtime().find("main/field"));
        const double start = paddle(h);
        check(waiting(h), "the ball waits on the paddle");
        h.step({GamepadEvent{.kind = GamepadKind::connected, .device = 1, .name = "pad"}, pad(GamepadKind::down, GamepadButton::dpad_right)});
        h.advance(seconds(0.3));
        h.step({pad(GamepadKind::up, GamepadButton::dpad_right)});
        const double right = paddle(h);
        check(right > start, std::format("the d-pad: {} → {}", start, right));
        h.step({GamepadEvent{.kind = GamepadKind::axis, .device = 1, .axis = GamepadAxis::left_x, .value = -1}});
        h.advance(seconds(0.3));
        h.step({GamepadEvent{.kind = GamepadKind::axis, .device = 1, .axis = GamepadAxis::left_x, .value = 0}});
        const double left = paddle(h);
        check(left < right, std::format("the stick: {} → {}", right, left));
        h.step({pad(GamepadKind::down)});
        h.advance(seconds(0.1));
        h.step({pad(GamepadKind::up)});
        check(!waiting(h), "south launched the ball");
    });

    return s.run(argc, argv);
}
