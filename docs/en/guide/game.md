# Games

> Preview: a 2D game that is played, replayed and played by an agent can be made; the whole course in a window, and tools for scenes and levels, are not there yet.

A game here is three things: a **state** (data copied whole), a **step** (a state and an input give the next state), and **what a player is shown**. It depends on no interface framework and runs with no window — on a server, in a thousand play-tests, in CI.

## Writing one

A game is a type with its state, its input, a step and what a player is shown. A small one from the runtime's own tests, a chase on a line:

<!-- code: modules/projects/meng/game/runtime/tests/runtime_test.cpp#chase -->
```cpp
// The player walks a line after a mark that jumps when reached; five catches end it.
struct Chase {
    struct State {
        std::int64_t  frame = 0;
        std::int32_t  player = 0, mark = 40, caught = 0, spare = 0;
        std::uint64_t rng = 0x9e3779b97f4a7c15ull;
    };
    struct Input {
        meng::Actions player{};
    };
    static constexpr std::int32_t steps_per_second = 120;
    static void step(State& state, const Input& input) {
        ++state.frame;
        if (input.player.axis(0) > 0.5) ++state.player;
        if (input.player.axis(0) < -0.5) --state.player;
        if (state.player == state.mark) {
            ++state.caught;
            state.rng = state.rng * 6364136223846793005ull + 1442695040888963407ull;
            state.mark = static_cast<std::int32_t>((state.rng >> 40) % 61) - 30;
        }
    }
    static std::uint64_t checksum(const State& state) noexcept {
        // Field by field (no padding hashed): the same play, the same number.
        const std::array<std::uint64_t, 5> parts{static_cast<std::uint64_t>(state.frame), static_cast<std::uint64_t>(static_cast<std::uint32_t>(state.player)),
                                                 static_cast<std::uint64_t>(static_cast<std::uint32_t>(state.mark)), static_cast<std::uint64_t>(state.caught),
                                                 state.rng};
        return mlibs::hash::fnv1a(std::as_bytes(std::span(parts)));
    }
    // A player is shown where they and the mark stand; only the full view shows where the mark jumps next.
    static std::string observe(const State& state, bool full) {
        std::string out = std::format("{{\"player\":{},\"mark\":{},\"caught\":{}", state.player, state.mark, state.caught);
        if (full) out += std::format(",\"rng\":\"{:016x}\"", state.rng);
        return out + "}";
    }
    static void play(Input& input, const meng::Actions& actions) { input.player = actions; }
    static bool over(const State& state) noexcept { return state.caught >= 5; }
    // What the game says of itself: what its controls mean, and a task to begin with.
    static constexpr std::string_view about = "A chase on a line: walk onto the mark and it jumps elsewhere; five catches end the game";
    static constexpr std::array<runtime::Control, 2> controls{{{.name = "left", .summary = "Walk toward lower numbers", .axis = 0, .toward = -1},
                                                               {.name = "right", .summary = "Walk toward higher numbers", .axis = 0, .toward = 1}}};
    static constexpr std::array<runtime::Move, 2>    to_the_mark{{{.hold = {"right"}, .steps = 60}, {.steps = 1}}};
    static constexpr std::array<runtime::Task, 1>    tasks{{{"catch-the-first-mark", "Walk right to the first mark (it stands at 40) and stop", to_the_mark}}};
};
```

`meng::runtime::Runtime<Chase>` (`import meng.runtime;`) runs it. Written so, these are there with nothing more to make:

- **Replay and rewind**: each step's input is kept, and any frame can be returned to.
- **Branches**: a copy of the state is played ahead, and the game being played is untouched.
- **Determinism**: the same input is the same result on every platform, and can be pinned in a test.

## An agent plays

The game is something an agent operates as it is, with six operations:

| Operation | Meaning |
|---|---|
| `world.observe` | look: what a player of this tier is shown |
| `world.act` | do: the buttons, axes and pointer a person has |
| `world.step` | play some steps |
| `world.seek` | return to a frame |
| `world.branch` | what would be, some steps on, if this were done (the game itself untouched) |
| `world.state` | the frame, the state's hash, whether it is over |

As whom an agent plays is yours to say: the **human tier** is shown what a person is and has a reaction time and a limit of actions a second; the **assist tier** is shown everything. So an agent can play fairly beside a person, or be a play-tester.

## A game of a world of matter

Put [a world of matter](world.md) in the state (it is a value: a copy is a branch) and the game adds only what is its own: how a character reads the world, how its tools write to it, what winning is.

[Kiln](../../../showcase/kiln) is a small game made so: Mui the pixel cat goes through caverns with spells of fire, water and bombs, and nothing is scripted — a barricade of wood burns open, a crate floats, a wall of stone is blown through, water sets lava to stone. Its tests have a play-tester that uses only the six operations above: it finishes the cavern, and in a cavern broken on purpose it stops and says where it is stuck.

The [widget breakout](../../../showcase/widget-breakout) is another: its bricks are real interface components, and an agent copilot plays beside you.

## A game with a window

The window, input and interface are omui's; the game and the interface meet at a bridge (`meng.ui`): the game steps as it does, and the interface reads its state to draw. A HUD, menus and settings are ordinary omui interfaces.

## When something goes wrong

- **A replay differs**: the same input is the same result; `world.state` gives each frame's state hash, so step two runs side by side and the first frame whose hashes part is where they diverge.
- **An agent plays too well**: it plays at the assist tier, which is shown everything; the human tier is shown what a person is and has a person's reaction time and actions a second.
