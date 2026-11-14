// Quay in a window, with no display (the headless host: no device, so the reference's pixels): a person's keys
// and mouse play it — a click on the harbour keeps the pointer to it, the hand's movement turns the watchkeeper,
// W walks, the left button fires — the HUD follows, and the recording of each step's input, played by the game
// with no window, ends at the same bits.
import std;
import omui;
import omui.host.headless;
import omui.showcase.quay.play;
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

UTest cases{"quay.window", [] {
    "a person plays the harbour in a window: looks by the hand, walks, fires; the HUD follows; the recording plays the same game with no window"_test = [] {
        omui::HeadlessOptions options;
        options.level = omui::HeadlessLevel::pixels;
        options.size = {1280, 760};
        options.fonts = &fonts();
        omui::Headless host(options);
        const omui::AppSpec spec = quay::play::app({.hearing = quay::play::Hearing::silent});
        spec.setup(host.runtime());
        host.mount("main", spec.view());
        host.step();
        const omui::NodeId node = host.runtime().find("main/harbour");
        const auto* harbour = host.runtime().component<quay::play::Harbour>(node);
        expect(fatal(harbour != nullptr && harbour->game() != nullptr));
        const float yaw_before = harbour->game()->state().keeper.yaw;

        // A click keeps the pointer to the world; the hand moves right: the watchkeeper turns right.
        const omui::Rect box = host.runtime().bounds(node);
        host.click(box.center());
        host.step();
        expect(host.pointer_held()) << "a press on the harbour keeps the pointer to it";
        for (int frame = 0; frame < 20; ++frame) host.move_by({12, 0}), host.step();
        const float yaw_turned = harbour->game()->state().keeper.yaw;
        // W held two seconds: it walks the way it faces.
        const quay::Vec3 before_walk = harbour->game()->state().keeper.at;
        host.step({key(KeyKind::down, Key::w)});
        for (int frame = 0; frame < 120; ++frame) host.step();
        host.step({key(KeyKind::up, Key::w)});
        const quay::Vec3 after_walk = harbour->game()->state().keeper.at;
        // The left button held half a second: shots.
        const std::uint32_t rounds_before = harbour->game()->state().keeper.rounds;
        host.step({omui::PointerEvent{.kind = omui::PointerKind::down, .position = box.center(), .button = omui::MouseButton::left}});
        for (int frame = 0; frame < 30; ++frame) host.step();
        host.step({omui::PointerEvent{.kind = omui::PointerKind::up, .position = box.center(), .button = omui::MouseButton::left}});
        host.step();

        const quay::State& played = harbour->game()->state();
        const float walked = std::hypot(after_walk.x - before_walk.x, after_walk.z - before_walk.z);
        std::println("in the window: {} steps; turned {:.2f} rad by the hand; walked {:.2f} m in two seconds; {} rounds fired; HUD: {} | {} | {}", played.frame, yaw_turned - yaw_before, walked,
                     rounds_before - played.keeper.rounds, quay::play::hud()->health.get(), quay::play::hud()->rounds.get(), quay::play::hud()->tide.get());
        expect(gt(yaw_turned - yaw_before, 0.3f)) << "the hand to the right turns the watchkeeper right";
        expect(gt(walked, 5.0f)) << "walking at 3.5 m/s";
        expect(gt(rounds_before - played.keeper.rounds, 2u)) << "held, the rifle fires again and again";
        expect(quay::play::hud()->rounds.get() == std::format("Rounds {}", played.keeper.rounds)) << quay::play::hud()->rounds.get();
        // Heard as it was played: each shot and step a sound at the watchkeeper's ears, the output (the null one
        // here) draining what the window renders.
        expect(fatal(harbour->sound() != nullptr && harbour->output() != nullptr));
        std::println("heard: {} sounds begun; the output drained {} frames ({} short)", harbour->sounds_begun(), harbour->output()->frames(), harbour->feed()->starved());
        expect(gt(harbour->sounds_begun(), std::uint64_t{rounds_before - played.keeper.rounds})) << "every shot, and the steps";
        expect(gt(harbour->output()->frames(), std::int64_t{0}));

        // What the window played is the game with no window.
        const std::string lines = quay::play::recorded(*harbour->game());
        const auto alone = quay::play::replayed(lines);
        expect(fatal(alone.has_value())) << (alone ? "" : alone.error());
        std::println("the recording, {} steps, played alone: hash {:016x}, the window's {:016x}", alone->frame, quay::checksum(*alone), quay::checksum(played));
        expect(eq(alone->frame, played.frame) && eq(quay::checksum(*alone), quay::checksum(played)));
    };
}};
