Overall: 62/100  
Mean: 71.5/100

mgame is a useful foundation for deterministic arcade games; gongfu is a substantially developed fighting simulation. Production authoring, integrated networking, boundary hardening, and demonstrated performance remain far below the named engines.

Reviewed source: `2f04e46`, against the previous `126cb48` report. Read the specified READMEs, designs, implementations, and tests. Used both running applications. Their executable revision was not independently verified.

**Evidence:** **used** = live interaction and captures; **measured** = observed frame counts/checksums; **viewed** = inspected source; **inferred** = unexecuted consequences. **P** marks provisional scores. `mcpp test -p mgame.net` stopped at unwritable module-cache metadata: **zero unit tests executed**. Quiet execution supplied no listening evidence; headless stepping supplied no physical input-to-display or real-time frame-budget measurements.

**Use log**

| Task | Calls, briefly | Expected | Actual | Result |
|---|---|---|---|---|
| Inspect both applications | `ui_apps`, `ui_snapshot`, `ui_capture` | Pixels and readable state | Both available; fighting exported one canvas beneath the application; breakout exported individual bricks | Pass, limited semantics |
| Enter a fight | Click visible Versus at `(637,489)`; Enter; advance clock in individual frames | Select fighters, then fight | Both became ready; frame stepping completed VS transition | Pass |
| Move through real input | Fighting `key_down D`, 25 clock steps, `key_up D` | Movement on next sampled frame | Frame 5→6: Mo x760→763.600006 | Pass |
| Attack through real input | Training checkpoint; `key_down U`; `step`; release; three more steps | Jab begins immediately; contact at declared startup | Move frame 1 on next tick; contact at frame 4: 300 damage, seven-frame hitstop | Pass |
| Inspect presentation | `demonstrate 5HP`, `demo_frame 8`, wait, capture | Ni’s heavy contact readable | Eight-frame startup, 800 damage, nine-frame hitstop; whole-body drawing and boxes visible | Pass |
| Pointer play | Breakout move/click `(420,685)`; clock steps | Paddle follows and ball launches | Launch on first simulation step; paddle approached pointer with bounded movement | Pass |
| Hold breakout keys | Release agent steering; hold Right for 12 simulation steps | Keyboard takes over from pointer | Paddle 0.299997→0.431247 | Pass |
| Check collision/semantics | Restart; `move_to .3`, `aim 0`, launch; step to frame 70; capture/find | Ball toggles switch and opens shield | Switch false; four shield plates hidden; ball reflected | Pass |
| Repeat breakout shot | Repeat identical setup and 70 steps | Identical state | Both hashes `d7af3af41d7c7e4a` | Pass |
| Repeat training checkpoint | Save at frame 82; same real-key jab; load and repeat | Same state/checksum | Both frame-86 hashes `c6fc9d2ef587244f` | Pass |
| Repeat versus checkpoint | Save frame 39; hold `6` for 20 steps, `5LP` for seven; load and repeat | Same CPU continuation | Hash changed `8ad9cbe640d34249`→`a75544c6bd1d63ac`; second run knocked Mo down for 850 damage | **Fail** |
| Check refusals | Invalid fighting pad; breakout brick-toggle dry-run; rewind without sparks | Explain invalid/forbidden actions | Clear refusals; brick response directed agent to aim the ball; rewind required 100 sparks | Pass |
| Inspect and pause | Escape, click visible Resume; breakout Tab; wait/capture | Controls respond and inspection preserves play | Both worked; breakout inspector showed IDs, changes and checksum | Pass |

**Defects found during use**

- **Versus save/load omits CPU continuation.** Reproduce the frame-39 sequence above. The bot’s decisions differ after loading; this is a game-checkpoint failure, not evidence that identical complete simulation inputs produce different results.
- **Fighting choices lack individual semantic nodes.** Capture the title/select screen, then snapshot it: visible mode choices and fighter tiles are represented by one canvas and aggregate JSON.
- **Training timer disagrees across renderings.** At the captured frame-86 jab, pixels show `∞`, while the tree reports `"seconds":99`.
- **Saved-state indicator survives fighter changes.** After saving Mo/Ni and selecting Ni/Mo through `fighters`, the tree still reports `"saved":true`; loading that checkpoint is incompatible.

**Framework and openas feedback**

The combination of pixels, explicit game state, real input, and semantic actions made meaningful independent verification possible. Breakout’s actual switch state and layout-derived collision are particularly convincing. Refusals explain the permitted route instead of merely failing.

The tooling needs an atomic “advance N simulation frames and return state/capture” operation: clock calls acknowledge pending work, while game ticks and visual transitions use different clocks. Fighting needs proper semantic children for HUD values and menu choices. Device-style game input reports no journaled action, limiting per-frame actor/effect auditing. No irreversible or person-only action was encountered, so those authorization paths earn no credit here.

**Dimension assessment**

Comparisons below concern the delivered engine and workflows, not feature-count parity with a general-purpose 3D engine.

| Dimension | Score / evidence | Basis | Individual comparisons | To reach 90, highest impact first |
|---|---|---|---|---|
| `scope_architecture` | **76**, viewed | Fixed columns, ordered queries and copyable worlds in [world.cppm:61](/home/speak/workspace/mui/mgame/world/src/world.cppm:61); simulation/UI separation is coherent. No equivalent scheduler, asset service or scene lifecycle was found. | **UE5: behind** in orchestration. **Unity 6: behind** in reusable production workflows. **Godot 4: behind** in scene composition. | Define the complete engine lifecycle and system dependencies; implement asset ownership/reloading; validate public services through an independently authored game. |
| `simulation_determinism` | **80 P**, used, measured, viewed; portability inferred | Both replay checks passed. Flat-state assertions and extensive rollback tests exist ([gongfu_test.cpp:510](/home/speak/workspace/mui/mgame/gongfu/tests/gongfu_test.cpp:510)). CPU checkpoint failure and unchecked fixed-point boundaries prevent a production claim. | **UE5: behind** in complete predictive integration, with a narrower inspectable-state advantage. **Unity 6: behind** in demonstrated deployment reliability. **Godot 4: ahead narrowly** in supplied fighting rollback machinery, without proven overall superiority. | Save all gameplay drivers; define overflow/division policies; publish identical replay artifacts across supported platforms; version canonical replay formats. |
| `rendering_presentation` | **74**, used, viewed | Live cameras, impact feedback, layered art and training overlays work. Mesh skinning, cel exposures and cinematic cameras are implemented ([present2d.cpp:879](/home/speak/workspace/mui/mgame/gongfu/present2d/src/present2d.cpp:879)). Ni’s heavy is a useful improvement; production animation/effect authoring remains incomplete. | **UE5: behind** in cinematic authoring. **Unity 6: behind** in integrated 2D animation workflows. **Godot 4: behind** in accessible animation/effects tooling. | Complete reusable pose, camera and effect authoring; review contact transitions across the roster; verify full GPU gameplay and renderer parity. |
| `physics_collision` | **66**, used, viewed | Switch collision and reflection worked. [sweep.cpp:20](/home/speak/workspace/mui/mgame/physics/src/sweep.cpp:20) handles continuous circle–box contacts; gongfu supplies specialized combat boxes. This remains a narrow collision library. | **UE5: behind** in solver/query breadth. **Unity 6: behind** in 2D bodies, joints and authoring. **Godot 4: behind** in shapes, filtering and diagnostics. | Publish supported workloads; add broadphase, filtering and spatial queries; provide an optional established backend for broader physics requirements. |
| `input_net` | **63 P**, used, measured, viewed; packet consequences inferred | Next-tick input and device takeover worked. Options are now clamped, but [net.cppm:123](/home/speak/workspace/mui/mgame/net/src/net.cppm:123) mutates bookkeeping before payload validation and lacks a bounded future receive window. No online game session was demonstrated. | **UE5: behind** in integrated sessions and diagnostics. **Unity 6: behind** in deployable device/network workflows. **Godot 4: behind** in usable transport integration. | Harden packet/window arithmetic; introduce typed wire codecs and validation; deliver handshake, transport, timeout, pacing and desync reporting in the fighting game. |
| `audio` | **70 P**, viewed, inferred | Mixer buses, synthesis, recordings, effects and adaptive sequencing are substantial. Combat schedules layered sounds in [sound.cpp:122](/home/speak/workspace/mui/showcase/fighting/src/sound.cpp:122). Listening, device latency and rollback-safe playback were unverified. | **UE5: behind** in procedural sound authoring. **Unity 6: behind** in mixer/import/profiling workflows. **Godot 4: behind** in integrated audio editing. | Add rollback event identities, deduplication and cancellation; measure device behavior; conduct listening review; provide graphical mixing/music inspection. |
| `content_pipeline_tools` | **62**, viewed | Located diagnostics, baking, stale-bake rejection and bot balance reports are valuable. [studio/README.md:3](/home/speak/workspace/mui/mgame/gongfu/studio/README.md:3) explicitly defers graphical authoring. | **UE5: behind** in integrated content iteration. **Unity 6: behind** in import/inspection workflows. **Godot 4: behind** in visual composition and animation editing. | Ship studio timelines, box/pose editing, undo and live diagnostics; add asset dependency/version tracking; measure an external creator’s workflow. |
| `ui_integration` | **81**, used, viewed | Breakout’s component state, collision and tree genuinely agree. Fighting exposes actionable combat/training data, but mostly through aggregate JSON ([stage.cpp:487](/home/speak/workspace/mui/showcase/fighting/src/stage.cpp:487)). | **UE5: behind overall** versus UMG, with distinctive agent inspection. **Unity 6: behind overall** versus UI Toolkit production workflows. **Godot 4: behind overall** versus Control composition/navigation. | Export meaningful HUD/menu children; verify controller and assistive navigation; supply reusable settings/dialogs; enforce pixel/semantic consistency through checkpoints. |
| `performance` | **68 P**, used, viewed; timing inferred | Frame progression behaved predictably. [game.cpp:118](/home/speak/workspace/mui/tests/bench/src/game.cpp:118) and budgets cover useful kernels, but presentation excludes rasterization and uses one roster entry. AI and input recording allocate. | **UE5: behind** in demonstrated profiling. **Unity 6: behind** in deployment evidence. **Godot 4: behind** in complete-loop diagnostics. Relative execution speed is unknown. | Publish full-frame CPU/GPU/audio/network percentiles; cover worst-case roster/cinematics; instrument complete-loop allocations and world scaling. |
| `code_quality_tests` | **75 P**, viewed; unexecuted properties inferred | Extensive rule, rollback, parser and presentation tests; stale mutation and option handling improved. Remaining unchecked arithmetic, packet state changes and CLI conversions weaken boundary contracts. | **UE5: behind** in demonstrated platform hardening. **Unity 6: behind** in validation coverage. **Godot 4: behind** in established integration maturity. | Add adversarial packet/data fuzzing and numeric properties; test complete checkpoints; publish successful sanitizer/platform results tied to revision. |

**Benchmark summary**

UE5’s strongest relevant targets are integrated presentation, [procedural audio authoring](https://dev.epicgames.com/documentation/en-us/unreal-engine/metasounds-in-unreal-engine), and [multiplayer lifecycle/debugging](https://dev.epicgames.com/documentation/en-us/unreal-engine/networking-and-multiplayer-in-unreal-engine).

Unity 6 supplies the most directly comparable broad [2D production workflow](https://docs.unity.com/en-us/engine/6000.0/manual/unity2d), alongside runtime/editor [UI Toolkit](https://docs.unity.com/en-us/engine/6000.0/manual/uitoolkits/uielements).

Godot 4 is the closest compact-engine benchmark: dedicated [2D rendering/physics/animation](https://docs.godotengine.org/en/stable/tutorials/2d/index.html), [network transports](https://docs.godotengine.org/en/stable/tutorials/networking/high_level_multiplayer.html), and graphical [audio buses](https://docs.godotengine.org/en/stable/tutorials/audio/audio_buses.html).

openmui’s strongest differentiation is inspectable, controllable semantic gameplay connected to reproducible simulation. Its largest deficits are authoring completeness and operational proof. Compared with [Bevy](https://bevy.org/learn/quick-start/getting-started/ecs/), its world lacks comparable system orchestration. Compared with [GGPO](https://github.com/pond3r/ggpo), the rollback algorithm still lacks demonstrated complete online integration. No Z-Engine internals were inspected; no proprietary architecture claims or superiority credit are assigned.

**Changes since the previous evaluation**

| Dimension | Previous → current | Reason |
|---|---:|---|
| `scope_architecture` | 76→76 | Same coherent specialist scope and missing services |
| `simulation_determinism` | 82→80 | Core repeatability verified; complete CPU checkpoint failure discovered |
| `rendering_presentation` | 73→74 | Live presentation verified; cel/contact support improved |
| `physics_collision` | 64→66 | Actual collision consequences verified |
| `input_net` | 61→63 | Option clamps and device takeover improved; receive defects remain |
| `audio` | 75→70 | Corrected to the no-use evidence ceiling; no demonstrated audio regression |
| `content_pipeline_tools` | 62→62 | Graphical studio still deferred |
| `ui_integration` | 81→81 | Live semantic advantage confirmed; granularity defects remain |
| `performance` | 68→68 | No new end-to-end timing evidence |
| `code_quality_tests` | 73→75 | Stale mutation and configuration fixes; no newly executed tests |

Overall rises **61→62**; mean remains **71.5**.

**Highest-priority changes**

1. Restore CPU/dummy and round-management state with checkpoints, or reject unsupported saves.
2. Validate complete packets before mutation; bound receive windows, acknowledgements and frame arithmetic.
3. Ship a real online fighting session with recovery and desync diagnostics.
4. Define safe numeric domains and fixed-point failure behavior.
5. Complete graphical studio authoring and live iteration.
6. Implement rollback-safe audio/rumble event handling.
7. Export accessible HUD/menu structure and consistent timer/checkpoint semantics.
8. Measure complete gameplay frames, allocations and physical input latency.
9. Run revision-linked sanitizer, hostile-input and cross-platform replay checks.

**Specific defects**

Runtime reproductions are above. Additional source findings are **inferred**, unless stated otherwise:

- [stage.cpp:980](/home/speak/workspace/mui/showcase/fighting/src/stage.cpp:980): checkpoint excludes bot state; loading also omits round wins and related progression.
- [fixed.cppm:26](/home/speak/workspace/mui/mgame/gongfu/src/fixed.cppm:26): unchecked construction/arithmetic overflow and zero division.
- [net.cppm:123](/home/speak/workspace/mui/mgame/net/src/net.cppm:123): malformed payload can update acknowledgement/frame bookkeeping; excessive sequential future inputs can overwrite ring slots.
- [net.cppm:106](/home/speak/workspace/mui/mgame/net/src/net.cppm:106): native input bytes remain the wire schema; representation restrictions do not provide protocol versioning or semantic validation.
- [input.cpp:13](/home/speak/workspace/mui/mgame/input/src/input.cpp:13): releasing one source clears a button another source still holds.
- [input.cpp:119](/home/speak/workspace/mui/mgame/input/src/input.cpp:119): recording parser narrows unchecked values and accepts trailing material.
- [world.cppm:117](/home/speak/workspace/mui/mgame/world/src/world.cppm:117): `get` still ignores generation, presence and bounds.
- [input.cppm:90](/home/speak/workspace/mui/mgame/gongfu/src/input.cppm:90): `motion()` can write past its six-element array.
- [ai.cpp:196](/home/speak/workspace/mui/mgame/gongfu/ai/src/ai.cpp:196), `:297`: temporary candidate vectors allocate during decisions; stage input history also grows during play.
- [present2d.cpp:948](/home/speak/workspace/mui/mgame/gongfu/present2d/src/present2d.cpp:948): mesh topology is rebuilt each draw; glyph helpers also allocate.
- [main.cpp:65](/home/speak/workspace/mui/mgame/gongfu/studio/src/main.cpp:65): malformed numeric arguments lack normal JSON error handling; `:42` bypasses the required OS abstraction.
- [match.cpp:27](/home/speak/workspace/mui/mgame/gongfu/src/match.cpp:27): duplicated hash implementation; `world/README.md` also incorrectly claims standard-library-only dependencies.
- Breakout’s “removed” change wording describes nodes retained as hidden ([field.cpp:321](/home/speak/workspace/mui/showcase/widget-breakout/src/field.cpp:321)).

```json
{"overall":62,"mean":71.5,"dimensions":{"scope_architecture":76,"simulation_determinism":80,"rendering_presentation":74,"physics_collision":66,"input_net":63,"audio":70,"content_pipeline_tools":62,"ui_integration":81,"performance":68,"code_quality_tests":75}}
```