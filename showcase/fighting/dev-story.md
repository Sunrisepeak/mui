# Dev story — the fighting game

How `omui-fight` is made, told from the developer's side: what was built, at which layer, with which of the framework's capabilities, how every asset was made and with which tools, how fast it runs, and what the making taught us. The quality bar is the [strict acceptance rubric](../../.agents/docs/2026-10-29-fighting-acceptance-rubric.md); the combat design is in [the hardcore combat design](../../.agents/docs/2026-10-29-fighting-hardcore-combat-design.md).

![墨's level-3 super](media/mo-level-3.webp)

**Contents**

1. [What we set out to make](#1-what-we-set-out-to-make)
2. [The layers](#2-the-layers)
3. [The fighters](#3-the-fighters)
4. [Combat systems](#4-combat-systems)
5. [How a fighter is made: the workflow](#5-how-a-fighter-is-made-the-workflow)
6. [Tools](#6-tools)
7. [The frame: presentation as a pure function](#7-the-frame-presentation-as-a-pure-function)
8. [Sound and music](#8-sound-and-music)
9. [Performance](#9-performance)
10. [Self-review](#10-self-review)
11. [Practices that generalise](#11-practices-that-generalise)

## 1. What we set out to make

A serious 2D fighting game at the level of a top independent release — not a demo with a fighting theme. Five designed fighters plus a sixth (坤), each with their own way of winning; the systems a fighting-game player expects (levels, strengths, cancels, throws, posture and guard breaks, clashes, three levels of super); a training mode; rollback netcode.

It is also a showcase (D-63), but the framework shows through what the game needs, never as a lecture: deterministic rollback is why replays and pinned pictures are free; the semantic tree is why an agent can play; data files with located diagnostics are why fighters can be modded.

**The reset.** The first version drew fighters from geometric parts posed by numbers. The maintainer rated it 1/10 (codex, against a strict rubric, 2/10): mannequin fighters, slideshow supers, effects that looked like presentation software, a glyph written as a super. We changed the method rather than polishing it: the art now comes from generated (or an artist's) layered pictures, and the framework does the cutting, rigging, animating, directing, effects, sound and music — all as data, all deterministic. The second direction from the maintainer made the combat **unarmed and hardcore**: hands, feet, body and joints for every normal; signature props only in special moments; supers that are interactive and different for everyone.

## 2. The layers

Every layer is usable without the one above it. Nothing was pushed down for the game's convenience, and nothing general was written inside the game.

| Layer | Package | What it is | Depends on |
|---|---|---|---|
| Fighting framework, core | `mgame/gongfu` | The match as a pure function: state, rules, frame data, Q16.16 fixed point | the standard library only |
| Fighting framework, data | `gongfu.data`, `gongfu.rules` | Fighter files read and checked with located diagnostics; the build rule that bakes hurtboxes and embeds fighters | gongfu, toml++ |
| Fighting framework, presentation | `gongfu.present2d` | A frame drawn: rigs and image parts, key poses, grips, reactions, effects, cut-ins, cinematics | gongfu, `openmui.render.vector`, hanim |
| Fighting framework, tools | `gongfu.training`, `gongfu.ai`, `gongfu.studio` | Readouts and dummies; seeded bots; cut, picture, sheet, bake, check, versus | the above, `openmui.image` |
| Independent components | `openmui.image`, `openmui.audio`, `openmui.raster`, `openmui.render.vector`, `hanim` | Images (PNG, WebP, cutting), sound and music, rasterising, vector rendering, animation | the standard library, each other, wrapped C libraries |
| UI framework | openmui | The stage as a component, its semantic node, its actions, the window | — |
| The game | `showcase/fighting` | The stage, the HUD, the roster, the fighters' files and art, the music | all of the above |

Examples of the rule at work: recolouring a range of drawn commands (刻's grey time stop) went into the renderer; cutting a sheet into pieces went into `openmui.image`; a piece's long axis (where a limb's joint lies) went into `openmui.image` too; music became an `openmui.audio` capability, not game code.

## 3. The fighters

Each fighter is three files in `fighters/` — what they do (`*.fighter.gongfu.toml`), how they look (`*.look.gongfu.toml`), and their hurtboxes baked from the look (`*.boxes.gongfu.toml`) — plus pictures in `fighters/art/`.

### 墨 (Mo)

| | |
|---|---|
| **Concept** | A calligrapher: calm, precise, keep-away. Paper-white robe with charcoal and vermilion, a high bun with a pin, a long ink brush |
| **Fighting style** | Being rebuilt unarmed (the hardcore design §3): palm and sleeve "strokes" — point, horizontal knife-hand, vertical chop, diagonal palms — driven from the waist; long kicks. The brush is slung on her back and comes out only for her specials (a wave of flung ink), supers and idle flourishes |
| **Art** | codex generated a concept, a side view, a parts sheet on green and six expressions; cut into a 987 × 440 parts atlas (94 KB WebP) and a 490 × 279 faces atlas (40 KB) |
| **Rig** | 21 bones; 18 image parts at one art scale (0.36 stage units per pixel); grid meshes bent at elbows and knees; sleeves and skirt panels on trailing bones; the back hand gripping the brush and both feet planted by two-bone IK; the head swaps between six drawn expressions |
| **Animation** | 48 clips keyed by 11 named key poses (`[pose.stance]`, `crouch`, `air`, `hit`, `guard`, `down`…), each key changing only what it names; four hit reactions (head, body, low, heavy) |
| **Effects** | Ink cut from a generated sheet: splash bursts, crescent slashes, dry-brush strokes laid along curves, droplets — played by her impacts, her projectiles and her clips |
| **Super (level 3)** | The eyes cut-in in the flash; eight cuts of ink from every side as the camera pushes and pulls, the struck one reacting to each; a strike cut-in; her finishing illustration in place of the rig; the slam. To be redone as an unarmed, interactive sequence (the hardcore design §4) |
| **Music** | The study: guzheng in D minor pentatonic, 128 BPM, 4-bar intro and 32-bar loop, layers by intensity |

### 霓 (Ni)

| | |
|---|---|
| **Concept** | A rushdown street fighter from a rainy neon city: magenta undercut fringe, cropped bomber with a lit zip, fingerless gloves with lit knuckles, cargo trousers, white-soled sneakers |
| **Fighting style** | Street boxing and kicks: jab, cross, hook, uppercut; low sweep, side kick, high kick; a dash that closes the screen |
| **Art** | Generated with the joints covered by her clothes (the lesson from 墨's parts); a 1017 × 438 parts atlas; eyes and strike cut-ins; a finishing illustration |
| **Rig** | 16 bones; her head drawn facing left, so its part is mirrored across its axis; her fringe and her jacket's flap trail her; an open-hand drawing swaps in for the front fist |
| **Animation** | 48 clips by 7 named poses (a boxer's guard as the stance) |
| **Super (level 3)** | The eyes cut-in; a flurry in the neon city; the strike cut-in; her finishing illustration. To be redone with a grab (the defender held by her hand: the cinematic's grab anchor) |
| **Music** | The street: drum and bass in F minor, 168 BPM, 8-bar intro and 40-bar loop |

### 坤 (Kun) — in progress

An original character built on an internet meme's signs — a clean centre part, a white shirt with rolled sleeves, grey suspender trousers, white-and-gold high-tops, a basketball — played completely straight: cold, quiet, precise. Bajiquan close-range bursts (铁山靠, the shoulder driven in with a step; 顶肘, the elbow) on a dancer's footwork; the ball appears only in his specials and supers. His fighter file is drafted; his art is generated (a second pass for the centre part and separate hands); rig and clips follow the same workflow. Research and the reasons for the contrast are in the hardcore design §5.

### 璃, 岳, 刻 and Kata

The first three still use the original geometric looks; they will be rebuilt with the same workflow. Kata, the training dummy, stays deliberately plain, but acts: it has the four hit reactions, so a super's cuts land visibly on it.

## 4. Combat systems

The match is a pure step `step(state, pads) → state` over one plain struct in Q16.16 fixed point: no clocks, no allocation, no virtual calls, so rolling back 8 frames is copying and stepping again, and a whole recorded match hashes the same on GCC, LLVM, ARM and wasm. The rules arrived as numbered requirements (G-1 … G-22, P-1 … P-8), each with a test. Frame data is measured, not trusted: the tests play every move and compare startup, active, recovery and advantage with the file.

A hit during the defender's startup is a counter: 120 % damage and 2 more frames of stun. A hit during a whiffed move's recovery is a punish counter: 130 % and 4 more frames. The tests check this by punishing a whiffed heavy.

The hardcore design lists every detail with its rule, its look and its acceptance: high, mid and low; light, medium and heavy; attacks and reactions; combos; the three levels of super; knockdowns; defence; neutral; whiff punishes (limbs' hurtboxes are baked from the animation, so an extended limb can be hit); jumps; cross-ups (inputs are recorded relative to facing, so blocking a cross-up means holding the other way); throws. The bots (`gongfu.ai`) play every pair for hundreds of rounds; the tests hold each fighter to 38–62 % wins.

## 5. How a fighter is made: the workflow

Each step is a tool with a command line (JSON out, an exit code) so an agent can run it; the steps are the same for an artist's layers.

| Step | What happens | Tool |
|---|---|---|
| 1. Concept | A concept, a side view and a parts sheet (plain green, parts apart, joints covered by clothing), expressions, cut-ins, a finishing picture, an effects sheet (black, for light) | codex image generation, prompts kept with the art; judged against the rubric |
| 2. Cut | Key out the green; find each piece (pixels joined across a small gap); mask it to its own pixels; scale; pack an atlas; report each piece's region and its long axis | `gongfu-studio cut sheet.png --out dir --scale 0.5 --webp 92` |
| 3. Pictures | Cut-ins and illustrations keyed, trimmed, scaled and encoded | `gongfu-studio picture in.png --out x.webp --key 0,255,0 --trim --width 640` |
| 4. Rig | In the look file: `[[picture]]`s, `[[bone]]`s (joints and lengths from the art at one `art_scale`), `[[part]]`s (a picture's region, its anchor on the joint and its tip along the bone — the long axis from step 2 gives both), meshes and bends, `[[grip]]`s for hands and feet | the look format (`gongfu.present2d`) |
| 5. Key poses | `[pose.<name>]` tables (bones' angles to the world, rise, grips, drawings), clips keyed by them with per-key changes | the look format |
| 6. Look at it | Any clips at any frames at any size, as a contact sheet | `gongfu-studio sheet fighters mo --clips 5MP,5HP --frames 1,8 --cell 300` |
| 7. Bake | Hurtboxes from the poses; the build refuses a stale bake | `gongfu-studio bake fighters` |
| 8. Balance | Bots play every pair | `gongfu-studio versus fighters --rounds 200` |
| 9. In the game | The tests draw pinned frames and the README loop headless | `mcpp test` in `showcase/fighting` |
| 10. Grade | codex grades the frames against the rubric (one independent source); the maintainer's grade is final | — |

Throwaway scripts were used once — to turn world-angle key poses into tracks before `aim` keys and named poses existed. Both are now features of the look format, and the looks are plain data again with nothing beside them.

## 6. Tools

| Tool | What it does | Where |
|---|---|---|
| `gongfu-studio cut` | Sheet → masked pieces, packed atlas (PNG or WebP), JSON manifest with regions, atlas regions, long axes and an alpha histogram | `mgame/gongfu/studio` |
| `gongfu-studio picture` | Key out, trim, scale, encode a picture (PNG or WebP by its name) | same |
| `gongfu-studio sheet` | Contact sheets of clips at chosen frames, with boxes, the look's drawn art included | same |
| `gongfu-studio bake` / `check` | Hurtboxes from the animation; checks that bakes are current | same |
| `gongfu-studio versus` | Every pair played by bots: win rates and where damage comes from | same |
| `omui-music check / render / stats / fmt` | A piece checked, rendered to WAV at an intensity, measured (loudness, bands, per instrument) | `modules/media/audio` |
| `tools/ci/readme-media.sh` | Copies what the tests drew into each showcase's `media/`; in CI, checks they match | `tools/ci` |
| `openmui.image` | PNG, WebP (still and animated), key out, pieces, pack, crop, scale, principal axis | `modules/base/image` |

The tools' openmui interfaces (a rig editor, a pose editor with IK handles, a timeline with onion skins, an impact tuner with slow motion) are the next step — the command lines came first so agents and CI could use them.

## 7. The frame: presentation as a pure function

`gongfu.present2d` draws a frame from `(state, frame)` alone; rollback never saves presentation state, and a replay renders the same pixels.

- **Rigs and image parts.** A part is a picture's region skinned over a grid mesh to its bone and to the bones it bends at; `DisplayList::draw_mesh` draws it on the CPU reference renderer and on the GPU the same (max difference 0).
- **Key poses and IK.** Clips are keyed by named poses in world angles (`aim`); grips solve two-bone IK for a hand on the brush or a foot on the ground, and clips let go and step.
- **Reactions.** The latest blow's height and weight choose `hitstun.head`, `.body`, `.low` or `.heavy`; a cinematic names a reaction for each of its hits; a grab anchor holds the defender at the attacker's hand, playing `held`.
- **Effects.** `[fx.<name>]`: sprites that grow, turn and fade; brush strokes laid along a curve and drawn out; sprays of droplets from a seed — named by impacts, projectiles, clips and cinematics.
- **Cut-ins.** `[cutin.<move>]` over a super's flash; panels in a cinematic; a cinematic can hide its attacker while an illustration stands in.
- **Materials.** A picture may have a `relief`: each region's silhouette rounded in, with folds where it is painted darker. `openmui.image`'s `normal_map` makes it once at load, in integers, so it is the same everywhere. A look's `[material.<name>]` sets how its image parts take the stage's light: the bands, how soft their edges are, and the shadow's and the rim's strengths. The light's direction and colours are the stage's, so both fighters are lit alike. Each fighter has a style:
  - 墨: two soft washes toward a cool ink.
  - 霓: hard bands with a hot pink rim.
  - 坤: clean cel shading with a warm rim.
  - Faces and hands use a softer, warmer `skin`.

  The renderer's lit mesh fill turns each normal with the part, so a mirrored or rotated arm still faces the light. The CPU reference and the GPU agree to 0 levels.
- **Afterimages and clones** are the part's shape in one colour (`draw_mesh_mask`), not a faded copy, so 霓's trail reads as neon.
- **Hit feel**, the five parts (hardcore design §2.6), each checked by a test:
  - **Hitstop:** light < medium < heavy for every fighter's normals. A counter adds 2 frames and a punish counter 4, for both fighters.
  - **The struck shakes:** every frame of the hitstop moves the struck fighter's picture, and the shaking stops when the hitstop ends.
  - **Effects by tier:** a counter's effect is the hit's own, larger and drawn in the counter colour. A punish's is stamped twice and flashes white, unless the look gives `fx_counter` / `fx_punish` effects of its own.
  - **Camera:** it shakes only for medium and heavier hits (a light hit's shake is 0), and harder for a counter or a punish.

## 8. Sound and music

- **Sounds**: synthesised sounds as data (`sounds/fight.sound.omui.toml`), named by each impact (attack, body, tail), mixed sample-accurately; an offline render of a match is the same bits on GCC and LLVM. Voices per fighter (CC0 packs, processed per character) are next.
- **Music**: `music/*.music.omui.toml` pieces — patches, patterns as readable note text, sections, an arrangement, and layers by intensity — played by `openmui.audio`'s sequencer. The stage starts its home's piece with the round; the music rises as the lower health falls, and plays fully under a super.

## 9. Performance

| What | Measured | How |
|---|---|---|
| A frame of the match | well under a microsecond | `tests/bench`, budgets in `budgets.bench.omui.json` |
| Rollback, 8 frames | 196 ns a frame, nothing allocated | the rollback bench |
| Sound, 48 voices | about 0.045 ms per 128-frame block (budget 0.5 ms) | `audio/mix-48` |
| Determinism | the same match hash on six platforms; the same pixels on GCC and LLVM | pinned in the tests |

Skinning runs on the CPU per frame for two fighters of a few thousand triangles; the plan (math and fighter technology design §5.3) is a mesh render feature with prebuilt meshes and GPU skinning, benchmarked before it is claimed.

## 10. Self-review

**What worked**

- Determinism first made almost everything else simpler: rollback, replays, pinned pixels and sound, a bot-driven balance report, an agent playing through the same actions.
- Data files with located diagnostics, and the build refusing stale bakes, caught real mistakes early.
- Generated art plus framework tools reached in a day what weeks of tuning geometric parts did not.

**What was hard, and what it changes**

| Finding | What it says to do | Status |
|---|---|---|
| Code alone could not reach the art bar | Art comes from pictures; the framework cuts, rigs and animates them | done (墨, 霓) |
| Generated parts had skin-coloured stumps at every joint | Ask for joints covered by clothing; crop and order parts to hide the rest | done in the prompts |
| Placing parts was guesswork | The cut tool reports each piece's long axis | done |
| Key poses were numbers produced by a script | Named key poses and `aim` in the look format | done |
| A long textured triangle lost half of itself in a clipped cell | The rasteriser cut edges at the clip's sides instead of clamping their ends (`openmui.raster`) | fixed |
| The brush became the weapon, and too big | Unarmed normals; props in special moments; believable proportions | in progress (hardcore design) |
| Black ink disappears on dark stages | "Colour by alpha" and additive image paints in both renderers | planned |
| Fighters need a rebuild to change | Hot reload of fighter files in a development mode | planned |
| A changed pin is hard to review | Pixel diffs against the last accepted frame in `openmui.testing` | planned |
| Every cinematic grew its own special fields | Generic primitives (panels, effects, holds, reactions); the old fields to be rewritten and removed | in progress |

## 11. Practices that generalise

Collected in [best practices](../../docs/en/best-practices.md):

- Keep simulations as plain data and pure steps (fixed point, or doubles with exactly rounded operations only). Rollback, replay, tests and netcode follow from that.
- Draw presentation as a pure function of (state, frame). Never save rendering state for rollback.
- Make components contract-first, so a person, an agent and a test act through the same actions.
- Keep assets as data with located diagnostics, and have the build check what was derived from them (bakes, embeds).
- Give every production step a command line with JSON before its graphical tool: agents and CI can use it the day it exists.
- Bring art in as pictures and let the framework do the rest; ask the generator for what the rig needs (separate parts, covered joints, one scale).
- Write performance-critical code (simulation, mixing, rendering) in C++; tunable content in data files that can be reloaded; scripts (TS, Lua) only where logic itself needs tuning live — never in a frame.
