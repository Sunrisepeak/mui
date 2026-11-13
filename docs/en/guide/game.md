# Games

> Early: a 2D game that is played, replayed and played by an agent can be made; the whole course in a window, and tools for scenes and levels, are not there yet.

A game here is three things: a **state** (data copied whole), a **step** (a state and an input give the next state), and **what a player is shown**. It depends on no interface framework and runs with no window — on a server, in a thousand play-tests, in CI.

## Writing one

```cpp
import xengine.runtime;

struct Play {
    using State = …;  using Input = …;                        // plain data, copied whole
    static void step(State&, const Input&);                   // a step: a pure function
    static constexpr std::int32_t steps_per_second = 60;
    static std::uint64_t checksum(const State&);              // the same play, the same number
    static std::string   observe(const State&, bool full);    // what a player is shown, as JSON
    static void          play(Input&, const xengine::Actions&);  // buttons, axes and a pointer into a step's input
    static bool          over(const State&);
};

xengine::runtime::Runtime<Play> game(initial);
```

Written so, these are there with nothing more to make:

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

The window, input and interface are omui's; the game and the interface meet at a bridge (`xengine.ui`): the game steps as it does, and the interface reads its state to draw. A HUD, menus and settings are ordinary omui interfaces.
