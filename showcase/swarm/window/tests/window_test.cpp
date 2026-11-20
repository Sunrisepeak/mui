// Swarm on screen, with no display: the picture of a state read back (the player, the gun, the enemies and the
// bullets where the state has them; the same state, the same bytes), and the window played by a person's keys,
// pointer and gamepad, its recording the headless game to the bit.
import std;
import omui;
import omui.host.headless;
import omui.showcase.swarm.window;
import mlibs.hash;
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

constexpr int width = static_cast<int>(swarm::arena_width) * swarm::window::Arena::scale, height = static_cast<int>(swarm::arena_height) * swarm::window::Arena::scale;

xrender::raster::Surface drawn(const swarm::State& state) {
    xrender::DisplayList list;
    swarm::picture::draw(state, list, {0, 0, static_cast<float>(width), static_cast<float>(height)});
    xrender::raster::Surface frame(width, height);
    xrender::CpuRenderer renderer;
    (void)renderer.render(list, frame, {});
    return frame;
}
std::uint64_t hash_of(const xrender::raster::Surface& frame) { return mlibs::hash::fnv1a(std::as_bytes(std::span(frame.pixels()))); }
// A pixel of the frame at a place of the arena.
auto at(const xrender::raster::Surface& frame, swarm::Vec2 place) {
    return frame.pixels()[static_cast<std::size_t>(place.y * swarm::window::Arena::scale) * static_cast<std::size_t>(width) + static_cast<std::size_t>(place.x * swarm::window::Arena::scale)];
}

}  // namespace

UTest cases{"swarm.window", [] {
    "the picture of a state: the player, its gun, the enemies and bullets where the state has them; the same state, the same bytes"_test = [] {
        // The first wave closing in from the east, the gun aimed at it and firing (the game's own first task).
        swarm::State state = swarm::start();
        meng::Actions firing;
        firing.held = 1u << swarm::shoot_button;
        std::tie(firing.pointer_x, firing.pointer_y) = swarm::pointer_at({swarm::arena_width, swarm::arena_height / 2});
        for (int step = 0; step < 150; ++step) swarm::step(state, {firing});
        std::size_t enemies = 0, bullets = 0;
        swarm::Vec2 enemy_at{}, bullet_at{};
        state.field.each<swarm::Body, swarm::Enemy>([&](meng::Entity, const swarm::Body& body, const swarm::Enemy&) { ++enemies, enemy_at = body.at; });
        state.field.each<swarm::Body, swarm::Bullet>([&](meng::Entity, const swarm::Body& body, const swarm::Bullet&) { ++bullets, bullet_at = body.at; });
        expect(fatal(enemies > 0 && bullets > 0)) << std::format("{} enemies, {} bullets", enemies, bullets);

        const auto frame = drawn(state);
        const auto player = at(frame, state.player), enemy = at(frame, enemy_at), bullet = at(frame, bullet_at);
        const auto floor = at(frame, {3, 3});
        std::println("frame {}×{}: player ({}, {}, {}), an enemy ({}, {}, {}), a bullet ({}, {}, {}), the floor ({}, {}, {}); {} enemies, {} bullets; hash {:016x}", width, height, player.r, player.g,
                     player.b, enemy.r, enemy.g, enemy.b, bullet.r, bullet.g, bullet.b, floor.r, floor.g, floor.b, enemies, bullets, hash_of(frame));
        expect(eq(int{player.r}, 0x5c) && eq(int{player.g}, 0xe1) && eq(int{player.b}, 0xe6)) << "the player's colour at its place";
        expect(gt(int{bullet.r}, 200) && gt(int{bullet.g}, 180)) << "a bullet's yellow at its place";
        expect(gt(int{enemy.r}, int{enemy.b})) << "the first wave's red at an enemy's place (its core darker)";
        expect(eq(int{floor.r}, 0x0b) && eq(int{floor.g}, 0x10) && eq(int{floor.b}, 0x20)) << "the floor";
        // The gun points east: a pixel just east of the player's edge is the gun's, one just west is the floor's.
        const auto east = at(frame, state.player + swarm::Vec2{swarm::player_radius + 0.8f, 0});
        const auto west = at(frame, state.player - swarm::Vec2{swarm::player_radius + 0.8f, 0});
        expect(gt(int{east.r}, 200) && eq(int{west.r}, 0x0b)) << std::format("the gun's line east ({}), none west ({})", east.r, west.r);
        // A function of the state: drawn again, the same bytes; a copy (a branch) draws the same.
        const swarm::State copy = state;
        expect(eq(hash_of(drawn(copy)), hash_of(frame)));
        expect(eq(hash_of(frame), 0xc40652ede3832bb0ull)) << std::format("pinned {:016x}", hash_of(frame));
    };

    "a person plays the arena in a window: keys move, the pointer aims, the button shoots; the HUD follows; the recording is the headless game"_test = [] {
        omui::HeadlessOptions options;
        options.level = omui::HeadlessLevel::pixels;
        options.size = {980, 600};
        options.fonts = &fonts();
        omui::Headless host(options);
        const omui::AppSpec spec = swarm::window::app({.seed = 3});
        spec.setup(host.runtime());
        host.mount("main", spec.view());
        host.step();
        const omui::NodeId node = host.runtime().find("main/arena");
        const auto* arena = host.runtime().component<swarm::window::Arena>(node);
        expect(fatal(arena != nullptr && arena->game() != nullptr));
        const swarm::State& state = arena->game()->state();
        const swarm::Vec2 before = state.player;
        // D and S held a third of a second: right and down.
        host.step({key(KeyKind::down, Key::d), key(KeyKind::down, Key::s)});
        for (int frame = 0; frame < 20; ++frame) host.step();
        host.step({key(KeyKind::up, Key::d), key(KeyKind::up, Key::s)});
        const swarm::Vec2 moved = state.player - before;
        // The pointer at the arena's east edge, the left button held half a second: shots go east.
        const omui::Rect box = host.runtime().bounds(node);
        const omui::Point east{box.x + box.width * 0.98f, box.y + box.height * 0.5f};
        const std::int32_t shots_before = state.shots;
        host.step({omui::PointerEvent{.kind = omui::PointerKind::move, .position = east}});
        host.step({omui::PointerEvent{.kind = omui::PointerKind::down, .position = east, .button = omui::MouseButton::left}});
        for (int frame = 0; frame < 30; ++frame) host.step();
        host.step({omui::PointerEvent{.kind = omui::PointerKind::up, .position = east, .button = omui::MouseButton::left}});
        for (int frame = 0; frame < 120; ++frame) host.step();
        std::println("in the window: {} steps; moved ({:.1f}, {:.1f}); aim ({:.2f}, {:.2f}); {} shots, {} kills; HUD: {} | {} | {}", state.frame, moved.x, moved.y, state.aim.x, state.aim.y,
                     state.shots - shots_before, state.kills, swarm::window::hud()->lives.get(), swarm::window::hud()->score.get(), swarm::window::hud()->wave.get());
        expect(gt(moved.x, 5.0f) && gt(moved.y, 5.0f)) << "D and S move right and down";
        expect(gt(state.aim.x, 0.9f)) << "the pointer aims";
        expect(ge(state.shots - shots_before, 3)) << "the held button shoots";
        expect(swarm::window::hud()->score.get() == std::format("Score {}", state.score));
        expect(eq(arena->picture().width(), width) && eq(arena->picture().height(), height));
        // The window shows the picture of its state.
        expect(eq(hash_of(arena->picture()), hash_of(drawn(state)))) << "the window's frame is the state's picture";
        // What the window played is the headless game.
        const std::string lines = swarm::window::recorded(*arena->game());
        expect(lines.starts_with("seed 3\n"));
        const auto alone = swarm::window::replayed(lines);
        expect(fatal(alone.has_value())) << (alone ? "" : alone.error());
        std::println("the recording, {} steps, played alone: hash {:016x}, the window's {:016x}", alone->frame, swarm::checksum(*alone), swarm::checksum(state));
        expect(eq(alone->frame, state.frame) && eq(swarm::checksum(*alone), swarm::checksum(state)));
        expect(!swarm::window::replayed("level first\n").has_value()) << "a recording says its seed";
    };

    "a gamepad: the left stick moves, A shoots where the gun points"_test = [] {
        omui::HeadlessOptions options;
        options.level = omui::HeadlessLevel::pixels;
        options.size = {980, 600};
        options.fonts = &fonts();
        omui::Headless host(options);
        const omui::AppSpec spec = swarm::window::app({});
        spec.setup(host.runtime());
        host.mount("main", spec.view());
        host.step();
        const auto* arena = host.runtime().component<swarm::window::Arena>(host.runtime().find("main/arena"));
        expect(fatal(arena != nullptr && arena->game() != nullptr));
        const swarm::State& state = arena->game()->state();
        const auto pad = [](omui::GamepadKind kind, omui::GamepadButton button = omui::GamepadButton::south, omui::GamepadAxis axis = omui::GamepadAxis::left_x, float value = 0) {
            return omui::InputEvent{omui::GamepadEvent{.kind = kind, .device = 1, .button = button, .axis = axis, .value = value}};
        };
        const float y_before = state.player.y;
        host.step({pad(omui::GamepadKind::axis, {}, omui::GamepadAxis::left_y, -1.0f)});
        for (int frame = 0; frame < 20; ++frame) host.step();
        host.step({pad(omui::GamepadKind::axis, {}, omui::GamepadAxis::left_y, 0.0f)});
        const std::int32_t shots_before = state.shots;
        host.step({pad(omui::GamepadKind::down, omui::GamepadButton::south)});
        for (int frame = 0; frame < 12; ++frame) host.step();
        host.step({pad(omui::GamepadKind::up, omui::GamepadButton::south)});
        host.step();
        std::println("a gamepad: moved {:.1f} up, {} shots", y_before - state.player.y, state.shots - shots_before);
        expect(gt(y_before - state.player.y, 5.0f)) << "the left stick up moves up";
        expect(ge(state.shots - shots_before, 1)) << "A shoots";
        const auto alone = swarm::window::replayed(swarm::window::recorded(*arena->game()));
        expect(fatal(alone.has_value())) << (alone ? "" : alone.error());
        expect(eq(swarm::checksum(*alone), swarm::checksum(state)));
    };
}};
