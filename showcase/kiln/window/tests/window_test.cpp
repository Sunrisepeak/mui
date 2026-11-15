// Kiln in a window, with no display (the headless host; the picture drawn on the CPU as it always is): a person's
// keys and mouse play it — D walks right, W jumps, the left button casts fire toward the pointer — the HUD follows,
// the picture is the cavern, and the recording of each step's input, played by the game with no window, ends at
// the same bits; a gamepad walks and casts too.
import std;
import omui;
import omui.host.headless;
import omui.showcase.kiln.window;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
using omui::Key, omui::KeyKind;

namespace {

mlibs::text::FontCollection& fonts() {
    static mlibs::text::FontCollection made = [] {
        mlibs::text::FontCollection collection;
        (void)collection.add_defaults();
        return collection;
    }();
    return made;
}
omui::InputEvent key(KeyKind kind, Key which) { return omui::KeyEvent{.kind = kind, .key = which}; }

}  // namespace

UTest cases{"kiln.window", [] {
    "a person plays the cavern in a window: walks, jumps, casts fire at the barricade; the HUD follows; the recording is the headless game, the same bits"_test = [] {
        omui::HeadlessOptions options;
        options.level = omui::HeadlessLevel::pixels;
        options.size = {1100, 560};
        options.fonts = &fonts();
        omui::Headless host(options);
        const omui::AppSpec spec = kiln::window::app({.hearing = kiln::window::Hearing::silent});
        spec.setup(host.runtime());
        host.mount("main", spec.view());
        host.step();
        const omui::NodeId node = host.runtime().find("main/cavern");
        const auto* cavern = host.runtime().component<kiln::window::Cavern>(node);
        expect(fatal(cavern != nullptr && cavern->game() != nullptr));
        const kiln::State& state = cavern->game()->state();
        const float x_before = state.cat.x;
        // D held a third of a second: she walks right along the ledge, still well short of the barricade.
        host.step({key(KeyKind::down, Key::d)});
        for (int frame = 0; frame < 20; ++frame) host.step();
        host.step({key(KeyKind::up, Key::d)});
        const float walked = state.cat.x - x_before;
        // W: a jump.
        float highest = state.cat.y;
        host.step({key(KeyKind::down, Key::w)});
        for (int frame = 0; frame < 20; ++frame) host.step(), highest = std::min(highest, state.cat.y);
        host.step({key(KeyKind::up, Key::w)});
        for (int frame = 0; frame < 60; ++frame) host.step();
        // The pointer on the barricade (cells 25 … 27 across, 21 … 29 down), the left button: fire.
        const omui::Rect box = host.runtime().bounds(node);
        const omui::Point at{box.x + box.width * 26.0f / 128, box.y + box.height * 25.0f / 64};
        const std::uint32_t casts_before = state.casts;
        host.step({omui::PointerEvent{.kind = omui::PointerKind::move, .position = at}});
        host.step({omui::PointerEvent{.kind = omui::PointerKind::down, .position = at, .button = omui::MouseButton::left}});
        for (int frame = 0; frame < 6; ++frame) host.step();
        host.step({omui::PointerEvent{.kind = omui::PointerKind::up, .position = at, .button = omui::MouseButton::left}});
        for (int frame = 0; frame < 240; ++frame) host.step();
        std::println("in the window: {} steps; walked {:.1f} cells in a third of a second, jumped {:.1f} cells, {} spells cast; HUD: {} | {} | {}; {} sounds begun", state.frame, walked,
                     state.cat.y - highest, state.casts - casts_before, kiln::window::hud()->health.get(), kiln::window::hud()->mana.get(), kiln::window::hud()->spell.get(),
                     cavern->sounds_begun());
        expect(gt(walked, 4.0f)) << "walking at 25 cells a second";
        expect(state.outcome == kiln::Outcome::playing && gt(state.cat.health, 0)) << "the barricade burns ahead of her, not on her";
        expect(lt(highest, state.cat.y - 2)) << "W jumps";
        expect(ge(state.casts - casts_before, 1u)) << "the left button casts toward the pointer";
        expect(kiln::window::hud()->mana.get() == std::format("Mana {}", state.cat.mana)) << kiln::window::hud()->mana.get();
        expect(eq(cavern->picture().width(), 128 * kiln::window::Cavern::scale) && eq(cavern->picture().height(), 64 * kiln::window::Cavern::scale));
        // The picture is the cavern: its rock is not the background.
        std::size_t lit = 0;
        for (const auto pixel : cavern->picture().pixels()) lit += pixel.r + pixel.g + pixel.b > 60;
        expect(gt(lit, std::size_t{20000})) << "the cavern drawn";
        // What the window played is the headless game.
        const std::string lines = kiln::window::recorded(*cavern->game());
        expect(lines.starts_with("level first\n"));
        const auto alone = kiln::window::replayed(lines);
        expect(fatal(alone.has_value())) << (alone ? "" : alone.error());
        std::println("the recording, {} steps, played alone: hash {:016x}, the window's {:016x}", alone->frame, kiln::checksum(*alone), kiln::checksum(state));
        expect(eq(alone->frame, state.frame) && eq(kiln::checksum(*alone), kiln::checksum(state)));
    };

    "a gamepad: the left stick walks, A jumps, the right trigger casts the way she faces"_test = [] {
        omui::HeadlessOptions options;
        options.level = omui::HeadlessLevel::pixels;
        options.size = {1100, 560};
        options.fonts = &fonts();
        omui::Headless host(options);
        const omui::AppSpec spec = kiln::window::app({.hearing = kiln::window::Hearing::none});
        spec.setup(host.runtime());
        host.mount("main", spec.view());
        host.step();
        const auto* cavern = host.runtime().component<kiln::window::Cavern>(host.runtime().find("main/cavern"));
        expect(fatal(cavern != nullptr && cavern->game() != nullptr));
        const kiln::State& state = cavern->game()->state();
        const auto pad = [](omui::GamepadKind kind, omui::GamepadButton button = omui::GamepadButton::south, omui::GamepadAxis axis = omui::GamepadAxis::left_x, float value = 0) {
            return omui::InputEvent{omui::GamepadEvent{.kind = kind, .device = 1, .button = button, .axis = axis, .value = value}};
        };
        const float x_before = state.cat.x;
        host.step({pad(omui::GamepadKind::axis, {}, omui::GamepadAxis::left_x, 1.0f)});
        for (int frame = 0; frame < 45; ++frame) host.step();
        host.step({pad(omui::GamepadKind::axis, {}, omui::GamepadAxis::left_x, 0.0f)});
        const std::uint32_t casts_before = state.casts;
        host.step({pad(omui::GamepadKind::down, omui::GamepadButton::right_trigger)});
        for (int frame = 0; frame < 4; ++frame) host.step();
        host.step({pad(omui::GamepadKind::up, omui::GamepadButton::right_trigger)});
        host.step();
        std::println("a gamepad: walked {:.1f} cells, {} cast", state.cat.x - x_before, state.casts - casts_before);
        expect(gt(state.cat.x - x_before, 8.0f)) << "the left stick right walks right";
        expect(ge(state.casts - casts_before, 1u)) << "the right trigger casts";
        const auto alone = kiln::window::replayed(kiln::window::recorded(*cavern->game()));
        expect(fatal(alone.has_value())) << (alone ? "" : alone.error());
        expect(eq(kiln::checksum(*alone), kiln::checksum(state)));
    };
}};
