# Dev story — the fighting game

How `omui-fight` was made, told from the developer's side: what was built, at which layer, with which of the framework's capabilities, and how fast it runs. It ends with a self-review: what was awkward, what is missing, and what generalises. The quality bar the game is held to, with an honest grade of where it stands today, is in [the fighting game's quality design](../../.agents/docs/2026-10-29-fighting-indie-quality-design.md).

![墨's level-3 super](media/mo-level-3.png)

## 1. What we set out to make

A serious 2D fighting game, minimal but not a toy: five designed fighters, each with its own way of winning and its own feel, plus Kata, the training dummy. Systems a fighting-game player recognises: cancels, throws, a posture meter that breaks, Sekiro-style clashes where two attacks meet, and supers at one, two and three bars, the third with a cinematic. A training mode with boxes, a frame meter and recordable dummies. Rollback netcode. It is also a showcase (D-63): each fighter shows one capability of the engine, in the place where it belongs in the game.

## 2. The layers

The game is the top of a stack, and every layer below it is usable without the one above. Nothing was pushed down a layer for the game's convenience, and nothing general was written inside the game.

| Layer | Package | What it is | Depends on |
|---|---|---|---|
| Fighting framework, core | `mgame/gongfu` (`gongfu`) | The match as a pure function: state, rules, frame data, Q16.16 fixed point | the standard library only (it can leave the repository unchanged) |
| Fighting framework, data | `gongfu.data`, `gongfu.rules` | Fighter files (`*.fighter.gongfu.toml`) read and checked with located diagnostics; the build rule that bakes hurtboxes and embeds fighters | gongfu, toml++ |
| Fighting framework, presentation | `gongfu.present2d` | A frame of a match drawn: skeletons, looks, sparks, cinematics | gongfu, `openmui.render.vector`, hanim |
| Fighting framework, tools | `gongfu.training`, `gongfu.ai`, `gongfu.studio` | Readouts and dummies; seeded bots; bake, check, contact sheets, the balance report | the above |
| Game engine services | `mgame.net`, `mgame.sim`, `mgame.input` | Rollback netcode, timelines, input sampling | the standard library, openmui for the UI glue |
| Independent components | `openmui.audio`, `openmui.render.vector`, `hanim`, `openmui.image` | Sound, vector rendering, animation, images | the standard library, each other |
| UI framework | openmui | The stage as a component, its semantic node, its actions, the window | — |
| The game | `showcase/fighting` | The stage, the HUD, the sound library, the roster, the fighters' files | all of the above |

The rule we kept to: **general systems below, specialised systems in the middle, the application on top.** The stage needed a picture turned grey for 刻's time stop. `DisplayList::recolor` went into the vector renderer, because recolouring a range of drawn commands is a renderer capability. It did not go into the game as a special case. The same applies to `DisplayList` clipping and transforms used by the cinematics.

## 3. How each part was built

### The match: deterministic, rollback-ready

- The state is one plain struct (`gongfu::State`), copied to save and hashed to compare. Numbers are Q16.16 fixed point, so the match is the same bytes on GCC, LLVM, ARM and wasm. The tests pin the hashes of whole recorded matches on every platform.
- A frame is `step(state, pads) → state`. Nothing outside the state matters: no clocks, no allocation, no virtual calls, so rolling back 8 frames is just copying and stepping again.
- The rules arrived as numbered requirements from the character designs (G-1 … G-22, P-1 … P-8). Each has a test in `mgame/gongfu/tests` and a hash in the rollback run.

### Fighters as data

- A fighter is three files: `*.fighter.gongfu.toml` (moves, frame data, cancels, supers), `*.look.gongfu.toml` (bones, palettes, keyframed clips, impacts, cinematics), and the generated `*.boxes.gongfu.toml` (hurtboxes baked from the animation).
- The build rule (`gongfu.rules`) embeds all three in the program and refuses a stale bake, naming the command to run. Animation and collision cannot drift apart.
- Frame data is measured, not trusted: the tests play each move in the simulation and compare startup, active, recovery and advantage with what the file declares.

### Presentation: a pure function of the match

- `gongfu.present2d` draws a frame from `(state, frame)` alone. Sparks are seeded by the event's frame and entity, and cinematics are timelines keyed to the lock's frame. Rollback therefore never saves any presentation state, and a replay renders identically.
- Everything is vector (`openmui.render.vector`), so any resolution is sharp and the headless host renders exact, pinnable pixels. The cinematics (墨's 永 written stroke by stroke, 璃's panes, 刻's stopped clock) are the same machinery: paths, gradients, clips, transforms and hanim curves.
- 墨's projectile is a character being written. That is glyph-level animation from openmui's text stack (`glyph_motion`), used because a calligrapher's projectile should be a character, not as a demo.

### The stage as an openmui component

- The stage is a contract-first component (`contract/stage.contract.omui.toml`): its props, events and actions are generated, and the same actions serve the keyboard, an agent (`press`, `step`, `fill` …) and the tests.
- Its semantic node states the match: health, posture, meter, what each fighter is doing, and the training readouts. An agent can play from that, and the tests assert against the same thing a screen reader would read.
- The match runs at exactly 60 Hz on the runtime's clock. The headless clock is virtual, so a headless second is exactly 60 frames, and tests are exact.

### Sound

- `sounds/fight.sound.omui.toml` is a library of synthesised sounds: thuds, cracks, glass, ink, neon, a temple bell, a clock. Each fighter's look names the sounds of its impacts (attack, body, tail).
- `openmui.audio` mixes them sample-accurately with no libm and no floating-point contraction. An offline render of a match's sound is the same bits on GCC and LLVM, pinned in a test. The device (`openmui.audio.device`, miniaudio) only plays what the mixer renders.

### Balance and the AI

- `gongfu.ai` reads a fighter's kit from its data (pokes by reach, anti-airs, projectiles, throws) and plays it with a 14-frame reaction, seeded.
- `gongfu-studio versus` plays every pair for hundreds of rounds and reports win rates and where each fighter's damage comes from. Those numbers drove the first balance pass (characters design 2, §9.5), and the tests hold the result as gates on every run.

## 4. Performance

| What | Measured | How |
|---|---|---|
| A frame of the match | well under a microsecond | `tests/bench`, budgets in `budgets.bench.omui.json` |
| Rollback, 8 frames | 196 ns a frame, nothing allocated | the rollback bench |
| Sound, 48 voices | about 0.045 ms per 128-frame block (budget 0.5 ms) | `audio/mix-48` |
| Determinism | the same match hash on six platforms; the same pixels on GCC and LLVM | pinned in the tests |

What made it fast is mostly what made it deterministic: plain data, no allocation in a frame, no virtual calls on the hot path, fixed point. Performance work was rarely a separate step.

## 5. Assets, and how they were made

After the maintainer rated the first version 1/10, 墨 was rebuilt on a new art pipeline ([design](../../.agents/docs/2026-10-29-fighting-art-pipeline-design.md)): generated art goes in, and the framework does the rest — cutting it up, skinning it, animating it, directing it — to deterministic frames.

| Asset | How it was made | With which part of the framework |
|---|---|---|
| 墨's art: concept, side view, a parts sheet on green | codex's image generation, from prompts recorded in the design notes, judged against the rubric | — (an input, as an artist's layers would be) |
| Her parts | `gongfu-studio cut` keys out the green, finds each part (pixels joined across a gap), masks it, packs an atlas; `gongfu-studio picture` scales and encodes to WebP | `openmui.image` (key out, pieces, pack, WebP through `mcpplibs.mwrap.libwebp`) |
| Her rig | each part on a bone (`anchor`, `tip`, one `art_scale` for the whole picture), a grid mesh bent at the elbows and knees, the back hand gripping the brush and the feet planted by two-bone IK | `gongfu.present2d` image parts and grips; `DisplayList::draw_mesh` (CPU reference and GPU, the same pixels) |
| Key poses | every clip re-keyed in world angles (`aim.<bone>`), feet stepping by `grip_at`; judged in contact sheets at any frame and size (`gongfu-studio sheet --clips --frames --cell`); hit boxes set to where the brush's head actually is, then the balance gates re-run | `gongfu.present2d` clips; `gongfu.studio`; the bots' balance report |
| Ink effects | a generated sheet of strokes, slashes, splashes, droplets, cut into an atlas; effects defined as data — sprites that grow and fade, brush strokes laid along a curve and drawn out, sprays of droplets — played by impacts, projectiles, clips and cinematics | `[fx.<name>]` in the look; `Painter::fx` |
| The level 3 | a directed cinematic: the eyes cut-in in the flash, eight cuts of ink from every side as the camera pushes and pulls, a strike cut-in, her finishing illustration in place of the rig, the slam | cut-in panels (`[cutin]`, `[[cinematic.X.panel]]`), cinematic effects and `hide` |
| Hurtboxes | baked from the animation (`gongfu-studio bake`); the brush is now part of her hurtbox, so a whiffed poke can be punished | `gongfu.studio` |
| Sounds | synthesised sounds as TOML data (voices from CC0 packs to come) | `openmui.audio` |
| Music | none yet | a sequencer on `openmui.audio` (the art pipeline §2) |
| The README loop | recorded headless as animated WebP (494 KB for 52 frames; the same as an APNG was over 1 MB) | `Headless::record`, `openmui.image` |

Along the way the work found a framework bug, fixed where it lived: an edge running out of a clip was drawn tilted toward its clamped end (`openmui.raster`), so a long textured triangle lost half of itself in a contact sheet's cell.

## 6. Self-review

**What worked**

- Determinism first made almost everything else simpler: rollback, replays, pinned pixels and sound, a bot-driven balance report, and an agent playing through the same actions.
- Data files with located diagnostics, plus the build refusing stale bakes, caught real mistakes early. Examples: a stale hurtbox, and a notation (`22K`) read as a held direction.
- Contract-first components let the keyboard, agents and tests share one set of actions.

**What was hard, and what it says to improve**

- **Making the art was the bottleneck, and code alone could not reach the bar.** Geometric parts posed by numbers stayed mannequins however long they were tuned. Generated art plus framework tools changed that in a day. What is still slow: placing each part (anchor, tip, depth) and keying poses is numbers in a file, judged from a contact sheet — this round a throwaway script turned world-angle poses into keys before `aim` keys existed. The graphical `gongfu.studio` (drag a part's pivot, pose by dragging bones with IK, a timeline with onion skins) is the next tool, as an openmui application (D-61).
- **Generated art has seams the rig must hide.** Parts came with skin-coloured cut ends at every joint; overlap, depth order and cropping the source region hide them. The parts prompt should ask for joints covered by cloth, and the cut tool could find and trim cut ends itself.
- **Black ink disappears on a dark stage.** Effects need a light rim or a "colour by alpha" image paint (an image used as a mask, coloured) — a renderer capability both paths should get.
- **No hot reload for fighters.** A changed look needs a rebuild because the files are embedded. Tuning wants the opposite: a development mode where the stage watches the files and reloads them in place. The rollback design makes this safe, because the state does not hold presentation data.
- **Debugging rendering with pinned hashes is coarse.** When a pin changes, the PNG in `/tmp/openmui-tests` is the only clue. Pixel diffs against the last accepted frame, with the regions that changed highlighted, would make pin updates reviewable. That is a `openmui.testing` capability, not a game feature.
- **Two bugs came from looking a projectile up by its owner** (a segfault on LLVM, silent on GCC). Identity should be explicit (`from`) wherever an entity can change hands; this belongs in gongfu's design notes.
- **No music, no menus, no camera work.** The game lacks these, not the engine; the quality design plans them as a vertical slice of 墨 and 霓 first.

## 7. Practices that generalise

These are collected in [best practices](../../docs/en/best-practices.md):

- Keep simulations as plain data and pure steps (fixed point, or doubles with exactly rounded operations only). Rollback, replay, tests and netcode follow from that.
- Draw presentation as a pure function of (state, frame). Never save rendering state for rollback.
- Make components contract-first, so a person, an agent and a test act through the same actions.
- Keep assets as data with located diagnostics, and have the build check what was derived from them (bakes, embeds).
- Write performance-critical code (simulation, mixing, rendering) in C++. Tunable content (fighter data, sounds) belongs in data files that can be reloaded, and scripts (TS, Lua) only where logic itself needs to be tuned live — never in a frame.
