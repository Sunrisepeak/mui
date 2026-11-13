// Kiln's simulation, each piece of play on the first cavern with no interface: Mui stands, walks into the
// barricade and burns it with fire (the wood catches by the laws), swims and runs out of breath, blasts a hole in
// the wall, cools lava into a floor with water; the same actions are the same frames, bit for bit.
import std;
import omui.showcase.kiln.sim;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

namespace {
kiln::Input walking(float axis) {
    kiln::Input input;
    input.player.axes[0] = static_cast<std::int16_t>(axis * 32767);
    return input;
}
// A cast at a cell, as a pointer over the field.
kiln::Input casting_at(const kiln::State& state, float x, float y) {
    kiln::Input input;
    input.player.pressed = 1u << kiln::cast_button;
    input.player.held = input.player.pressed;
    input.player.pointer_x = static_cast<std::int16_t>(x / static_cast<float>(state.world->shape().width) * 32767);
    input.player.pointer_y = static_cast<std::int16_t>(y / static_cast<float>(state.world->shape().height) * 32767);
    return input;
}
void run(kiln::State& state, int frames, const kiln::Input& input = {}) {
    for (int frame = 0; frame < frames; ++frame) kiln::step(state, input);
}
std::size_t count_in(const kiln::State& state, std::string_view name, std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1) {
    const auto material = *kiln::matter().material(name);
    std::size_t found = 0;
    for (std::uint32_t y = y0; y <= y1; ++y)
        for (std::uint32_t x = x0; x <= x1; ++x) found += state.world->cells()[std::size_t{y} * state.world->shape().width + x] == material;
    return found;
}
}  // namespace

UTest cases{"kiln.sim", [] {
    "Mui stands on her ledge at the start, whole and still"_test = [] {
        auto state = kiln::start(kiln::level::first());
        expect(fatal(state.has_value())) << (state ? "" : state.error());
        run(*state, 30);
        expect(state->cat.on_ground) << std::format("feet at {}", state->cat.y);
        expect(std::abs(state->cat.y - 30.0f) < 0.01f);
        expect(eq(state->cat.health, 100));
        expect(state->outcome == kiln::Outcome::playing);
    };

    "the barricade stops her; fire on it burns it away by the laws, and the way is open"_test = [] {
        auto state = *kiln::start(kiln::level::first());
        run(state, 120, walking(1));
        expect(state.cat.x < 25.0f - 1.4f) << std::format("stopped at {} by the wood", state.cat.x);
        const std::size_t wood = count_in(state, "wood", 25, 21, 27, 29);
        expect(eq(wood, std::size_t{27}));
        run(state, 40, walking(-1));                       // back off from it
        kiln::step(state, casting_at(state, 26.5f, 26.0f));
        // What matters is the way through: the barricade gone at her height. Its base, on the stone, keeps a
        // cell or two that the fire does not reach (the stone takes their heat): a step she climbs.
        int frames = 0;
        for (; frames < 1800 && count_in(state, "wood", 25, 21, 27, 27) > 0; ++frames) kiln::step(state, {});
        expect(eq(count_in(state, "wood", 25, 21, 27, 27), std::size_t{0})) << std::format("burnt open after {} frames", frames);
        expect(count_in(state, "wood", 25, 28, 27, 29) <= 4u) << "what is left at the base is no more than a step";
        expect(state.cat.health > 0) << std::format("and she kept back from it: health {}", state.cat.health);
        run(state, 240);                                   // the fire out, the air cooled
        run(state, 120, walking(1));
        expect(state.cat.x > 30.0f) << std::format("through, at {}", state.cat.x);
    };

    "in the pool she swims; with water over her head her breath runs down"_test = [] {
        auto state = *kiln::start(kiln::level::first());
        // Put her in the middle of the pool's water.
        state.cat.x = 36.5f, state.cat.y = 54.0f, state.cat.vy = 0;
        run(state, 5);
        expect(state.cat.swimming);
        const auto before = state.cat.breath;
        run(state, 60);
        expect(state.cat.breath < before) << std::format("breath {} → {}", before, state.cat.breath);
        // Holding jump, she swims up and out of breath's way.
        kiln::Input up;
        up.player.held = 1u << kiln::jump_button;
        run(state, 120, up);
        expect(state.cat.y < 42.0f) << std::format("up at {}", state.cat.y);
    };

    "a bomb holes the stone wall; water poured on lava makes stone of it"_test = [] {
        auto state = *kiln::start(kiln::level::first());
        // On the far ledge, facing the wall.
        state.cat.x = 70.5f, state.cat.y = 34.0f;
        state.cat.spell = kiln::Spell::bomb;
        run(state, 10);
        const std::size_t wall = count_in(state, "stone", 76, 22, 79, 33);
        kiln::step(state, casting_at(state, 77.0f, 30.0f));
        run(state, 240);
        expect(count_in(state, "stone", 76, 22, 79, 33) + 8 < wall) << std::format("the wall had {} cells of stone, now {}", wall, count_in(state, "stone", 76, 22, 79, 33));
        // The game turned nothing into anything: the stone is sand by the law that shatters (D-182).
        expect(eq(state.world->converted(), kiln::start(kiln::level::first())->world->converted())) << "the bomb wrote over no cell";
        // Over the lava: water.
        auto other = *kiln::start(kiln::level::first());
        other.cat.x = 83.5f, other.cat.y = 34.0f, other.cat.spell = kiln::Spell::water, other.cat.mana = 100;
        const std::size_t lava = count_in(other, "lava", 86, 36, 111, 43);
        for (int cast = 0; cast < 5; ++cast) {
            kiln::step(other, casting_at(other, 88.0f + static_cast<float>(cast) * 4.0f, 37.0f));
            run(other, 60);
        }
        run(other, 300);
        expect(count_in(other, "lava", 86, 36, 111, 43) < lava) << std::format("lava {} → {}", lava, count_in(other, "lava", 86, 36, 111, 43));
        expect(count_in(other, "stone", 86, 36, 111, 43) > 5u) << "stone where the water met it";
        expect(eq(other.world->converted(), kiln::start(kiln::level::first())->world->converted())) << "water was poured only where there was air";
    };

    "the same actions are the same frames, bit for bit"_test = [] {
        const auto play = [] {
            auto state = *kiln::start(kiln::level::first());
            run(state, 60, walking(1));
            kiln::step(state, casting_at(state, 26.5f, 26.0f));
            run(state, 300, walking(-0.5f));
            return kiln::checksum(state);
        };
        expect(eq(play(), play()));
    };
}};
