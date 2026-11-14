# Swarm

A small top-down shooter with no window: the player moves with two axes, aims with the pointer and shoots with a button; enemies arrive in waves from one side of the arena and chase the player; a bullet that meets an enemy removes it and scores; an enemy that touches the player costs a life; with none of three left the game is lost. It is a game on its own (D-162): its rules as [xengine.runtime](../../modules/projects/xengine/game/runtime)'s `Playable`, stepped, sought back and branched ahead with no omui in the program, and played by an agent as a person plays.

**State of it**: the rules, the game at the runtime and the program that plays it over lines are done and tested. It has no picture and no sound: nothing draws it yet, so this README shows no signature shot (the showcase table's other works do).

| Package | What |
|---|---|
| [headless](headless) | The rules (`src/sim.cpp`) and the game as the runtime plays it (`swarm::Play`, `swarm::Standalone`). The state is plain data — the enemies and bullets in one block of entities ([xengine.world](../../modules/projects/xengine/game/world)), the player, the wave, a random stream (`mlibs.math`'s SplitMix64) — so a copy is a branch and its checksum (`mlibs.hash`) is the same for the same play. The game says its own controls (`left`, `right`, `up`, `down`, `shoot`, `aim`) and three tasks to begin with |
| [play](play) | `swarm-play run --session FILE`: a JSON request a line on standard input, the records of its answer on standard output; the session's file is the game (its requests played again are the same bits), so an agent with a pipe plays across runs. A member of its own because a program with a command line and standard input is for the native systems only, and `headless` also builds for the Web (D-99) |

```sh
echo '{"op":"world.state"}' | swarm-play run --session game.jsonl      # the controls, each with the world.act that holds it
echo '{"op":"world.act","buttons":[0],"pointer":[1,0.5]}
{"op":"world.step","steps":600}
{"op":"world.observe"}' | swarm-play run --session game.jsonl           # aim east, hold fire: the first wave shot, score 40
```

`--tier human` (default: a person's reactions and rate) or `assist` (also shown the random stream, no limit); `--actor agent:name`; `--seed N` (which game: the waves after the first come from where the seed says). Exit 0 done, 1 a request failed (the record says why and what to send instead), 2 a wrong command.

How it was made — by one agent session from one prompt, in nine minutes, with no person in between — is in the [dev story](dev-story.md).

## The game

- **Arena**: 96 × 54 units, x to the right, y down. 120 steps a second.
- **Player**: 30 units a second; the gun points where the pointer last was; with the button held a bullet leaves every tenth of a second, at 120 units a second.
- **Waves**: wave *n* has 2 + 2*n* enemies (at most 40), one every third of a second, all from one side — the first wave from the east, the others from where the random stream says. Its enemies go straight at the player at 14 + *n* units a second (at most 26: always slower than the player) and are worth 10*n* each. The arena empty, a second of rest, then the next wave.
- **A touch**: costs a life and removes the enemy; for three quarters of a second after, nothing costs a life.

## Agent surface

The subject is the runtime's (`world`, `xengine.world@0.1`): `world.observe`, `world.act`, `world.step`, `world.seek`, `world.branch`, `world.state`. Nothing has to be known beforehand: `{"op":"subjects"}` lists what is here, `{"op":"world.state"}` the controls by name, `{"op":"describe","subject":"world"}` the tasks to begin with (the game's own first), `{"op":"find","q":"fire a bullet"}` finds a control by what is wanted. The view (`world.observe`) carries a legend of its names and numbers. The agent contract's suite ([mlibs.agent.cts](../../modules/base/agent/cts)) runs on the game in `headless/tests/swarm_test.cpp`.

```sh
mcpp test -p omui.showcase.swarm.headless   # the same inputs the same checksum; a seek back replayed bit for bit; an enemy shot; the game lost; each task as written; a player that clears waves; the agent contract
mcpp test -p omui.showcase.swarm.play       # the program run twice on one session file: the same frame, hash and score
```

## What the engine did not have

Noted while making it; each is substituted inside this member, not copied from elsewhere:

- **Vector length and normalisation**: `mlibs.math`'s `Vec2` has `+`, `−`, scaling, `dot` and `cross`, no `length` or `normalize`. Here: `std::sqrt(dot(v, v))` in `src/sim.cpp` (√ is correctly rounded, so the bits are the same everywhere).
- **A component with floats in `xengine.world`**: `Vec2` does not say `no_padding`, so a component holding one has to vouch for it itself (`Body`).
- **Circles meeting**: no public overlap test for two circles was found in `mlibs.geom` for a game's use; the two tests here are a squared distance against a squared sum of radii.
- **A game-over reason in the runtime's answers**: `world.step` says `over`, not why; the view's `outcome` says it.
