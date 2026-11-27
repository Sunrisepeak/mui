Overall: 48/100

This is a real game with a strong technical premise and substantial regression coverage. It is not yet a polished flagship arcade game. Its weakest dimension is readability: Dark Mode obscures essential information, while several other screens require prior knowledge to interpret.

Scores assess the supplied frames and repository source. Feel and audio scores are **provisional**: I did not play or listen, and I did not execute the tests. Paths below are relative to `showcase/widget-breakout/`.

| Dimension | Score | Evidence | Changes to reach 90+ |
|---|---:|---|---|
| Premise and concept clarity | 76 | `README.md`; `src/rooms.cpp:35`; `breakout-room1__start.png`. The switch-to-shield circuit communicates the premise well. Most surrounding controls look like ordinary destructible labels; the haunting has little visible personality. | Establish “hit controls to change the machine” through an unmistakable opening interaction. Give the panel a coherent haunted identity through reactions, copy, animation and sound. Distinguish functional mechanisms from score fodder. |
| Fun and moment-to-moment feel | 68, provisional | `sim/src/sim.cppm:8` documents swept collisions, spin and chains; simulation tests cover moving-paddle catches and spin (`sim/tests/sim_test.cpp:151`, `:385`). Frames show substantial travel through empty space. | Playtest with newcomers; measure time between meaningful decisions, missed catches and comprehension. Tune paddle acceleration, ball speed and aiming from those sessions. Replace long low-information travel with deliberate shot opportunities. |
| Level / room design and progression | 73 | Five mechanically distinct rooms in `src/rooms.cpp:35–213`; planner/catcher comparisons in `tests/breakout_test.cpp:796`. Rewards branch, rooms do not. | Give each mechanic a safe introduction, a combination challenge and a climax. Revisit early circuits with harder arrangements. Balance rewards with explicit costs: “2 catches” versus “5 catches” currently presents no compensating advantage for the smaller choice. |
| Game juice | 72, provisional | `src/fx.cpp:96`, `:173`; `src/sound.cpp:60`. Implemented trails, squash, hit flashes, shake, particles and shutdown rings. `anim-0080.png` shows the payoff, but ordinary impacts are visually slight. | Build a stronger hierarchy between contact, break, circuit activation and shutdown. Make particles inherit widget material and motion. Coordinate the shutdown’s anticipation, impact, audio and aftermath; provide reduced-motion controls. |
| Art direction and visual consistency | 55 | Compare `breakout-room1__start.png`, `breakout-room2__beam.png`, `breakout-room3__chamber.png`, `breakout-room4__scan.png`, `breakout-room5__copilot.png`. These read as separate theme demonstrations. | Art-direct one haunted Settings world with five states. Preserve shared typography, materials, circuit symbols and feedback shapes. Reduce indiscriminate neon bloom and paper noise; add environmental composition and authored personality. |
| UI and typography detail | 54 | `src/paint.cpp:485`; `src/app.cpp:155`. Gameplay mixes 10–13 px supporting text with largely unlabelled HUD symbols. Reward cards have unequal heights and vertically misaligned controls. | Establish a responsive type/spacing system. Give score, lives, sparks and rewind availability clear labels and hierarchy. Equalize reward-card structure; reserve strong glow for selection or activation. Correct light-theme hint contrast. |
| Controls-as-mechanics integration | 75 | Real component construction at `src/rooms.cpp:266`; layout bounds enter simulation at `src/field.cpp:149`; simulation changes component properties at `:303`. This is substantive integration. However, “Mute”, “Snooze”, “Sounds” and many other buttons share ordinary breakable behavior. | Either give named settings corresponding consequences or identify them clearly as ordinary targets. Expose circuit state, slider charge, core durability and next-hit consequences consistently. Test agreement between component actions, simulation state and semantic output. |
| Inspector / explanation layer | 61 | `breakout-room1__inspector.png`; `src/paint.cpp:557`. Role/ID outlines, explanation, recent changes and checksum exist. `src/field.cpp:144` returns no brick nodes for the canvas room, so this overlay does not inspect its objects equivalently. | Add pause-and-inspect selection, readable properties and highlighted circuit relationships. Support canvas objects. Replace repeated “removed” entries with selectable before/after changes; distinguish game-generated notes from runtime semantic diffs. |
| Sound and music | 60, provisional | `sounds/breakout.sound.omui.toml`; event scheduling at `src/sound.cpp:60`; rendered sample hash/peak test at `tests/breakout_test.cpp:695`. Good event vocabulary and pitched chains. No music implementation was evidenced in the showcase. | Add a composed musical identity or an intentionally designed reactive soundscape with equivalent depth. Audition dense beam/shutdown sequences; prioritize voices and control masking. Supply a synchronized audio capture and listening review, beyond a waveform hash. |
| Difficulty, onboarding, readability of state | **48** | Dark frames make labels, walls and HUD extremely difficult to read. Hints are temporary (`src/field.cpp:451`). Gamepad bindings cover movement/launch (`:20`), leaving scan, rewind, copilot orders and reward interaction without equivalent bindings there. | Keep essential state readable outside darkness and CRT effects. Teach goal, banking, rewind and circuits through staged play. Add persistent contextual help, pause/settings and complete controller navigation. Validate with players who have not read the README. |
| Framework features shown naturally | 82 | Components and layout drive collisions; canvas camera follows simulation; copilot reads semantics and calls actions (`src/copilot.cpp:70`); script answers become recorded step inputs (`src/field.cpp:207`). | Demonstrate semantic inspection across every room and input modality. Make script tuning show its gameplay consequence through a playable preview. Prove accessible and agent operation through complete runs, including reward selection and failure recovery. |
| Code quality and tests | 78 | Clear `.cppm`/`.cpp` separation, generated contract declaration, separate simulation package and disabled FP contraction. Tests cover collision edge cases, replay, circuits, audio, pixels and bots. `src/paint.cpp:214` clears a text cache; semantic construction allocates (`src/field.cpp:534`). | Fix manifest parent/child dotted-key quoting to match AGENTS.md. Replace hot-path temporary allocations/cache flushes with bounded storage or demonstrate the applicable allocation budget. Add resize, full-run input, semantic-action consistency and script reload race tests. Provide current CI results; bot success alone does not establish human fairness. |
| README / dev-story / signature shot | 81 | README explains the loop and controls; dev-story records implementation lessons and deferred work candidly. Headless animation generation exists at `tests/breakout_test.cpp:731`; frame hashes at `:709`. | Reframe the signature sequence so the switch, shield movement and core damage remain legible at 640×360. Include a synchronized sound version, reproducible build/run/test commands and measured human playtest findings. Clearly separate shipped-platform limitations from completed capabilities. |

### Top 7 fixes

1. **Repair Dark Mode visibility.** Render HUD, instructions, gate timing and essential collision boundaries above darkness/CRT effects; keep caches as the hidden discovery.
2. **Ship an understandable first minute.** Teach switch → shield → core, then chain → bank → rewind through actual play, with persistent contextual help.
3. **Unify the art direction.** Define one visual system for the haunted panel and carry its materials, typography and mechanism symbols across all five rooms.
4. **Make every named mechanic honest.** Give settings-specific buttons meaningful consequences, and display core health, charge and circuit state before players commit shots.
5. **Validate feel with humans.** Test first-time players using keyboard, pointer and controller; tune travel time, catches, aiming and difficulty from observed failures.
6. **Rebuild reward selection.** Align cards and actions, show active presets, explain tradeoffs and preview each reward in play.
7. **Strengthen release evidence.** Produce current CI results, complete-run input tests and synchronized audiovisual captures; address manifest and hot-path allocation concerns.

### Detail defects

- `anim-0000.png`: app-row labels and room subtitle are barely readable at animation size.
- `anim-0000.png`: inactive circuit wiring has very low contrast against the purple field.
- `anim-0011.png`: the launch instruction persists while the ball is already flying.
- `anim-0023.png`: the chain indicator crowds the Weather target at the upper right.
- `anim-0034.png`: the banked-chain message is faint and detached from the HUD it updates.
- `anim-0046.png`: ordinary impact particles are tiny, scattered marks with little material identity.
- `anim-0080.png`: the enormous shutdown circle dominates the frame while the destroyed core’s local detail is difficult to read.
- `breakout-room1__start.png`: glass cards and ordinary buttons have substantially different finishes without an explained gameplay distinction.
- `breakout-room1__start.png`: the launch forecast competes with the switch-to-shield wire near their intersection.
- `breakout-room1__played.png`: the empty shield frame remains visually prominent after its core disappears.
- `breakout-room1__played.png`: large empty regions make the remaining switch and paddle feel disconnected.
- `breakout-room1__inspector.png`: the explanation is a dense paragraph; repeated removal entries convey little causal information.
- `breakout-room1__inspector.png`: role/ID annotation above the switch is too small for comfortable reading.
- `breakout-run__choice.png`: card tops, preset rows and action rows do not align.
- `breakout-run__choice.png`: preset selection lacks an unmistakable persistent selected state.
- `breakout-run__choice.png`: cyan glow outlines nearly every action, flattening hierarchy.
- `breakout-casing__casing.png`: the script uses proportional-looking text without syntax hierarchy or line numbers.
- `breakout-room2__beam.png`: the beam washes through target labels, weakening hit readability.
- `breakout-room3__chamber.png`: the uppermost brick row is clipped by the frame.
- `breakout-room3__chamber.png`: file labels disappear at this zoom, weakening the chamber’s “files” identity.
- `breakout-room3__chamber.png`: paper grain competes with small text and fine outlines.
- `breakout-room3__chamber.png`: dark hint text on a dark translucent pill has poor contrast.
- `breakout-room4-dark__dark.png`: HUD, control labels and room geometry are nearly black.
- `breakout-room4__scan.png`: X-ray reveals cache outlines, but adjacent labels remain hard to read.
- `breakout-room4__scan.png`: scanlines and display distortion soften already faint text and edges.
- `breakout-room5__copilot.png`: the long instruction pill has insufficient text/background contrast.
- `breakout-room5__copilot.png`: slider charge marks are pale and their relationship to activation is unclear.
- `breakout-room5__copilot.png`: boost arrows overlap their labels; the pale ball trail nearly disappears against the background.