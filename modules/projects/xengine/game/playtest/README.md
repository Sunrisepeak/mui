# xengine.playtest

A game played by trying ahead: a player that is shown what a person is shown and does what a person can, and tries a thing on a branch before doing it. It is a part any game on [xengine.runtime](../runtime) can be given (the foundation plan's §6.14, D-179: a general part, not a work's test). A game gives it a table; the order of what it does, and where it stops, is here.

```cpp
import xengine.playtest;

xengine::playtest::Hands hands(session);                  // a session holding the game's `world`
const xengine::playtest::Report report = xengine::playtest::play(hands, game);
// report.won, or report.why: "stuck at x 111.5, frame 4226: 20 in the way ahead; 0 bombs, 100 mana; …"
// report.log: each thing done, at the frame it was done
```

## A turn

The first that applies:

| | When | What |
|---|---|---|
| 0 | hurt (`Policy::hurt`) | back off and stand until mended; afloat, up and out instead |
| 1 | a way forward gets further, unharmed | each of the game's ways, tried a stride ahead and — where the stride is safe — further on; the one that gets furthest is taken for a stride |
| 1b | a stride gained nothing, and nothing hurt | the same ways tried for as long as the look further on, and the one that gets somewhere taken for that long — out of a pool, through what slows her — before anything is spent |
| 2a | the way is open but hurts, and the game says what hurts passes | back off and wait, a number of times. The whole way back is tried first; where that is not safe (water behind her, or she is in what hurts already) she backs a piece at a time, for real, and at each place tries whether standing there is safe — and waits only where it is |
| 2 | blocked | a few steps back, for real; then each tool that suits, cheapest first, at each aim, over the tool's own time, on a branch — used for a moment and then standing, as it will be done (`world.branch`'s `then`); the first that leaves less in the way and her well is used |
| 3 | nothing helped | wait, where a wait tried ahead costs little, a few times; then stop |

It stops having won, having lost, or stuck — and then says where, at what frame, how much is in the way, what she has left, and (in the log) what each try came to.

## What a game gives (`Game`)

| | |
|---|---|
| `View`, `read(data)` | an observation's `data`, as the game reads it |
| `vitals(view)` | progress (forward is more), health, frame, won, lost, afloat |
| `ways()`, `back()`, `stay()`, `surface()` | actions, each the members of a `world.act` request |
| `passes(view)` | whether what would hurt ahead passes with time |
| `in_the_way(view, blocked)` | how much stands in the way of where she was blocked, as a view shows it |
| `tools()`, `suits(tool, view)`, `ready(tool, hands)` | what to try when blocked, cheapest first, each with the time it takes to work; whether she has it and it answers what is ahead; taking it in hand |
| `aims()`, `use(tool, aim, blocked)`, `aim(i)` | where a tool is pointed, the action that uses it there, and the aim's name for the report |
| `where(view)`, `has(view)` | the report's words for a place and for what she has left |

`Policy` holds the player's numbers: the stride and the look further on, the health a stride, a tool or a wait may cost, how often it waits.

## Agent surface

A consumer of one, not a subject: part of [xengine.runtime](../runtime)'s surface (`agent = { part = "xengine.runtime" }`).

| | |
|---|---|
| Subject and addresses | none of its own; it asks the game's `world` |
| State | none kept between turns but how often it has waited and how far it has been |
| Observation | `world.observe` only, on the session's tier: it never reads the game's state |
| Control | `world.act`, with the game's own actions |
| Time | `world.step` |
| Preview | `world.branch` before every choice; a choice is taken for real only after its branch |
| Causes | its own: `Report::log` says what was done at each frame and why, and what each try left |
| Identity and authority | the session's; nothing is asked that a person's tier does not allow |

## Limits

- One direction of progress, a number. A game where forward is not a line gives its own measure (distance to a goal); searching a space of places is not here.
- A policy, not a planner: it does not combine two tools, or use one to prepare another. Where a game needs that, the finding is the report's "stuck", and the answer is an agent's session (the plan's T2.8), not more cases here.
- A game's table is code (a type). As data — so a game's catalog of actions could give it — it waits for a second game that needs it.

## Tests

`tests/playtest_test.cpp`: a corridor that is not Kiln — a door opened by the cheapest tool that opens it, a fire waited out, the same play twice the same line for line, a second door with one key reported as stuck with what was tried, mending first when hurt, water behind the fire (she backs only to where she can stand), and a fire that never burns out never walked into. Kiln's own playtest ([showcase/kiln/headless](../../../../../showcase/kiln/headless)) is this player with Kiln's table.
