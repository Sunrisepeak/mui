# Dev story — Kiln

How Kiln was made, and what making it changed below it. Kiln is the vertical slice of M6.2 ([the plan](../../.agents/docs/2026-11-12-foundation-omui-xengine-emergent-world-design.md), T2.6–T2.7, T2.10b, T2.11): a small game whose job is to prove that a world of matter, bodies and cells in flight is something a game can be built on — by a person and by an agent — and to find where it is not. It is a half-made game and says so: there is no window to play it in yet.

![Mui has lit the barricade; the brush on the ledge, which is only scenery, has caught from it](media/barricade-rich.png)

## 1. Design

- **The hook:** everything is matter, and nothing is scripted. Mui has a wand of fire, water and bombs; each obstacle of the cavern is opened by what the matter does, not by a trigger. Wood burns because fire heats it past its ignition point. A crate floats because wood is lighter than water. Water sets lava to stone because that is what lava becomes when it loses its heat.
- **The first cavern** is four of these in a row — a barricade of wood (fire), a pool with a crate afloat (jump or swim), a wall of stone (a bomb), a pit of lava (water) — with a keg of gunpowder by the wall as a hazard rather than a key.
- **What the game adds to the world is small on purpose:** a cat who reads the field (what holds her up, what she swims in, what hurts her), a wand that writes to it (heat, water, a blast), an exit. A rule the game would need that matter does not already give is a finding about the infrastructure, not something to add to the game.

## 2. How each part is built with the framework

| Layer | Package | What it is |
|---|---|---|
| The laws | `xengine.matter` | the standard definition: 17 materials, 22 laws, none naming a material |
| The world | `xengine.cellular` (`rigid`, `particles`, `decor`) | `MatterWorld`: the lattice, bodies made of its cells (Box2D under `xengine.dynamics`), cells in flight — one deterministic value, saved, restored and branched as one |
| Time and the agent | `xengine.runtime`, `mlibs.simulation`, `mlibs.agent` | the game as a `Playable` on a timeline; the subject `world` (observe, act, step, seek, branch) |
| The picture | `xengine.cellular.present` (`world`), `mlibs.pipeline` | `WorldPicture`: the world shown by two ladders a governor moves — the light, the scenery |
| The sound | `xengine.audio` (`matter`) | each material's sound of an event, made from what the material is |
| The player | `xengine.playtest` | a turn of trying ahead by branching; Kiln gives it a table |
| The game | `showcase/kiln/sim`, `headless`, `picture`, `sound` | the cat, the wand, the cavern as rows of characters; the game with no interface; its display list; its world's events handed to the mixer |

- **The state is a value.** `kiln::State` holds a `WorldValue`; a copy is a branch of the whole world. The timeline, the playtest's look-ahead and a seek back are all the same thing: copies.
- **The game never sees the picture.** `Picture::show` takes the state as `const`. What the level calls scenery — roots by the barricade, brush on the ledge — is not in the simulation at all: where the picture has room it is simulated beside the world by the same laws and catches from the barricade's fire; where it has not it is drawn as placed. The game's checksum is the same at each of 520 frames wherever the ladders stand (D-178).
- **No omui in the game.** `headless` links the runtime and the kernels only (lint `closure`); an agent finds it at a kernel endpoint and plays it.

## 3. Assets and how they were made

There are no image files in the game.

- **The cavern** is rows of characters in `sim/src/sim.cpp`, a cell each (`#` stone, `w` wood, `~` water, `l` lava, `C` a crate that is a body).
- **Mui** is 6 × 10 rectangles over her 3 × 5 cells.
- **Matter's colours and light** are the standard appearance (`standard.appearance.omui.ts`): a base colour a row, its grain, tints along channels, what emits.
- **The baked flames** of the low step of the light are frames of the rich step itself: a hearth — a post of wood alight on a floor — run and drawn by the same code, twelve frames, kept by a key of what they were made from.
- **There are no sound files either.** A sound is an event's shape (a strike, a crumble, a sizzle) made from a material's numbers; `sound` is some twenty lines that hand each frame's events to it.
- **README media** are drawn headless by `picture_test`, one PNG a step of the ladder.

## 4. Self-review

**What worked**

- **Branching as the way to play-test.** The player in `playtest_test` is shown what a person is shown and does what a person can; before each move it copies the world and tries ahead. It finishes the first cavern (frame 3469) and, given a cavern with a second wall and one bomb, stops at x 111.5 (frame 6396) and says why: no bomb left, 20 cells in the way, and nothing else opens stone. It needed nothing of the game but `observe`, `act`, `step` and `branch`.
- **The slice found real faults below it,** each fixed where it lay and not in the game:

| Found by | Fault | Fixed in |
|---|---|---|
| the playtest in a release build: health 0 from frame 0 | the rigid world's saved image was written through an object's bytes and came out wrong under gcc -O2 | the image is written a byte at a time, little-endian (`xengine.dynamics`) |
| CI's benchmark step killed at 300 s | the lattice kernel built at -O0 | the kernel's package builds at -O2 (225 s → 39 s) |
| a frame of the world allocated | the coupling made its scratch each frame | kept between frames; a budget of 0 allocations a frame in `tests/bench` |
| the ladder's steps in the wrong order | one ladder of four steps, ordered by guess | measured (glow 42 ms, baked 4.5, none 2.1; scenery alive +0.5) and split in two ladders, the dearer lowered first |
| the world contract's suite, written for a third dimension | a cut image left Box2D's world unusable | Box2D's bytes under a length and a checksum of ours |

- **Why, asked of the game's own world.** `kiln::Causes` is some twenty lines: the world's lattice, the matter subject on it, and the runtime told to say when the game stepped or went back. The simulation was not touched — Mui's wand writes a pressure and a heat to the world — yet an agent shown everything asks of a grain of sand and is told the whole of it: it landed there, thrown by the pressure (its 146th flight: the pressure lasted), and it was sand because stone shattered at frame 40 by the law `shatters`, which read a toughness of 200000 under a pressure of 600000 — the pressure the player applied a frame before. What made that possible was below the game: a world that says what a frame did besides its laws, and a subject that stands on an interface, not on one kind of field.
- **Sound for nothing, and it told something.** The world had no way to say what a frame's laws did without being watched, so the lattice's step was given one (a list of what became what and what caught, a few a tile). With it Kiln's sound needed no rule: the bomb at the wall is stone's crumble to Mui's right at the frame the wall shatters. And the cavern with nothing done is not silent — the lava's top is setting, 113 cells in two seconds. Nobody had written that; it was happening all along.
- **The first numbers the sound gave were not real, and the test said why.** Mui stood beside the keg, died of her own bomb at frame 57, and the game stopped stepping the world; the test went on reading "the last step's events" and heard the same frame two hundred times. Two things came of it: Kiln's sound keeps the world's frame it last heard, and the engine's documents say that events are a step's and not a state. The simulation's own bomb test had had her dead all along — it counted the wall and never looked at her.
- **One governor.** The picture's ladders are moved by the same `mlibs.pipeline` governor that lowers omui's motion and effects; a decision is a recorded input.

**What was hard, and what it says to improve**

- **A bomb's rubble shut the hole it made, and the first answer was the game's.** Stone thrown by a blast came down as stone, so the game turned the stone about a bomb to sand itself — a work converting a material, which is the thing the plan says a work does not do (D-182). It is now a law of the standard definition: a solid has a `toughness` and what it `breaks_into`, and shatters where the pressure on it is past its toughness. Kiln's bomb is a pressure and a heat written to a disc of cells, and nothing else; the stone breaks to sand, the sand is thrown by the pressure and slumps into a heap, and the keg by the wall goes off because it was heated. Every game with the standard definition has the same bomb for nothing, and a pack that gives glass a toughness has glass that shatters. The spells themselves then went down a level too: an explosion, a heat and a pour are the engine's (`xengine.cellular.verbs`), and Kiln's wand is three sets of numbers; its test holds it to writing over no cell.
- **The player was the hard part, not the game.** Four of its faults were policy: counting regained ground as progress, losing sight of the way when it walked back, treating a hazard as an obstacle, drowning in the pool with small steps. It was a policy in a test, where a change to one step moved every other; it is now one level up, a part any `Playable` can be given (`xengine.playtest`, the plan's §6.14): the order of a turn and its numbers are the engine's, with tests of their own on a game that is not Kiln, and what is left here is a table of some seventy lines — how the view is read, walking and jumping, three spells and five aims, what counts as in the way. The move changed no frame of either play. What came next did: the part's own toy game, a corridor, showed that a branch played an action at once where a real one waits out a person's reaction — the preview was not the future. That was the runtime's and was fixed there; and with previews honest Mui stood a cell nearer the wall, was buried by her bomb's rubble, and rose through the ceiling and out of the world, three cells a frame: Kiln's own rule for pushing her out of what moves into her never looked where it put her. Three faults at three levels, each found by the level above being made a thing of its own.
- **The baked step shows what it was baked from.** Its frames are of a flame, so lava glows as small fires there. Each state that emits needs its own baked scene, written in the appearance data and not in a picture.
- **What the playtest found, and what is left of it.** She used to spend a bomb on the pool's far wall before finding the swim; with the bomb a pressure and not a blast she no longer does (one bomb, at the wall). She still crosses the lava without water — its top has set by the time she arrives — though the crust is hot: standing on it to wait would cost her all her health, which the player now tries ahead before waiting. A thing that goes on — lava setting, wood burning — said so every frame and was a patter; the engine's recipes now say how soon an event may sound again, which is a limit and not yet the lasting sound that swells and dies with how much of it there is. Hearing the bomb also measured what the next finding only described: 7213 landings in the four seconds after it. And a grain is thrown 146 times by one bomb: the pressure lasts some 260 frames, far longer than a blast does. That one is the standard definition's (how pressure spreads and is lost), to be settled where the push of pressure becomes a law of its own (the plan's T2.14c).

## 5. What it leaves as practice

1. **Build the game without a window first.** State as a value, a step as a pure function, a checksum: the playtest, the replay and the picture's proof all came from that for nothing.
2. **When the game needs a rule, ask whose it is.** A fault in save and restore, an allocation in a frame, a ladder in the wrong order were none of them Kiln's to work round.
3. **Measure before ordering.** The ladder was wrong until each step was timed.
4. **Let the player be shown only what a person is.** A playtest that reads the whole state finds nothing a person would.
5. **Say what is not done.** No window; one cavern; a player that is a policy and not an agent's session (T2.8).
