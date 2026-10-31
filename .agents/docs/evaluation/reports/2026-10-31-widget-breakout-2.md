Overall: 45/100  
Mean: 62.6/100

The mechanics form a playable prototype, but input conflicts, misleading component state, and unreadable overlays prevent release readiness.

Evaluated the supplied running application, source at HEAD `d4f7012`, documentation, tests, and all five README media files. **used** means MCP interaction followed by pixel/tree verification; **measured** means observed numeric results; **viewed** means inspected source/images. Audio judgments remain **inferred/provisional**.

Play used clock stepping and semantic observations to assist catches. Completion establishes functional reachability, not natural-speed difficulty or latency. No fresh mcpp tests or audio audition were performed.

## Use log

| Task | Calls, briefly | Expected | Actual | Result |
|---|---|---|---|---|
| Newcomer opening | `ui_capture`, `ui_snapshot`, restart, pointer move, Space | Understand movement, goal, controls | Fresh start teaches Space; shield/core relationship needs discovery. Movement, banking and goal lack persistent guidance. | Fail |
| Room 1, pointer | `ui_input(move/click/Space)`, clock steps, repeated captures | Aim, open shield, bank chains, clear | Cleared at simulation frame 3378; score 860, three lives. | Pass |
| Banking and rewind | Observe BANKED; hold/release R; step and snapshot | Bank resources, reverse state, pay | Sparks reached 120; rewind displayed backward movement; release reduced sparks to 20. | Pass |
| Room 1, keys | Right/Left/A, clock steps; move pointer outside; repeat | Keys work after pointer use | Initially stationary; outside-window workaround restored movement. Subsequently cleared at frame 3665, score 1060, three lives. | Fail |
| Shield switch | Click Do Not Disturb; inspect component and shield | One consistent switch/circuit state | Component toggled independently of simulation. | Fail |
| Room 2 | Pointer play; inspect slider values and dam | Charge → beam → opening | Charge updates appeared; dam broke; room cleared at frame 1304. Sliders lack visible names. | Pass mechanically |
| Reward and casing | Click casing, Wide preset, Take; `ui_invoke(gift.wide)` | Editable, understandable reward | Reload reported 49 ms; next room used half-width 95 and duration 1389 steps. Cards misalign. | Pass with defects |
| Room 3 | Pointer play, hold Z, Tab, captures | Discover chamber; inspect components | Zoom reached approximately 0.5003; cleared at frame 12122. Inspector prose became nearly unreadable; canvas bricks lacked annotations. | Fail readability |
| Room 4 | X; repeat `ui_act(scan)`; click Dark mode; play | Reveal obstacles, explain cooldown, consistent lighting | Scan revealed caches; repeat refused with 7.5 seconds remaining. Switch became false while field remained `dark:true`. Cleared at frame 2498. | Fail |
| Room 5 | Keys 1–4; Tab; pointer play | Orders affect copilot; finish run | Tree recorded all four orders and `agent:copilot` actions. Cleared at frame 4521; completion screen showed score 6700, longest chain 19. | Pass with readability defects |
| Inspector interaction | Tab, click `(1050,320)` | Inspect without activating covered gameplay | Click activated the underlying Remind me button. Inspector text/controls absent from snapshot. | Fail |
| Ball loss, defeat, recovery | Miss catches; launch again; Enter | Clear loss and restart | Lives decreased; Out of balls showed score/chain; Enter restarted successfully with field focused. | Pass |
| Continuing with widget focus | Enter after focusing switch/reward button | Advance room | Enter toggled switch or reactivated reward button instead. Refocusing field restored progression. | Fail |

**Reproducible defects from use**

1. **Input priority:** move the pointer inside the field, then hold Right or A. Paddle position remained exactly `0.4057435834833827` through separate 60-step checks. Move outside the window and keys work.
2. **State divergence:** in Room 4, click Dark mode while paused. The switch reports false, but field `dark` remains true and simulation checksum remains unchanged. Room 1’s switch similarly changes without operating its shield.
3. **Focus interception:** focus a switch, finish the room, press Enter. The switch consumes Enter. Focus retained on Take also intercepted subsequent progression.
4. **Inspector click-through:** open Tab in Room 1; click `(1050,320)` inside its panel. Input reports activation of covered `brick.17`, Remind me.
5. **Theme contrast:** open Tab in Rooms 3 or 5. Dark text is drawn on the inspector’s dark background. Their hint pills have the same problem.
6. **Darkness:** enter Room 4 and scan. Cache outlines appear, but ordinary targets, labels and boundaries remain too faint.
7. **Canvas inspection:** open Tab in Room 3. The explanation appears, but individual brick role/ID overlays do not.

## Framework and openas feedback

Stable IDs, action arguments, field checksums, scan refusals and copilot observations made investigation effective. The input tool correctly exposed hit targets and actor attribution. Read-only `gift.wide` returned the selected script’s rules.

However, the central “one semantic tree” promise breaks at the component/simulation boundary. The UI can report a changed switch while gameplay still uses the old value. The bindings in [rooms.cpp:266](/home/speak/workspace/mui/showcase/widget-breakout/src/rooms.cpp:266) provide no callbacks feeding these interactions into simulation.

Actor propagation also needs correction: clicking Take produced an outer action attributed to `agent:codex-evaluator;via=service:input`, followed by `choose` attributed to `person:player`. [app.cpp:115](/home/speak/workspace/mui/showcase/widget-breakout/src/app.cpp:115) explicitly constructs that person identity. This demonstrates misattribution; authorization bypass was not tested.

Add semantic inspector content, input-blocking panel bounds, focus recovery when overlays disappear, explicit input-device arbitration, and actor propagation through callbacks. Separate simulation rewind from runtime undo: after extensive field actions, undo’s dry run still targeted an earlier widget toggle.

## Dimension table

**P** = Peggle Deluxe; **B** = Baba Is You; **R** = Breakout: Recharged. Comparisons are editorial assessments, not fresh comparative playtests. Commercial internal code/tests were unavailable.

| Dimension | Score | Evidence and basis | Individual comparisons | To reach 90, in priority order |
|---|---:|---|---|---|
| `concept_clarity` | 66 | **used, viewed:** switch → shield → core works through ball collisions, but direct switch operation contradicts it. Generic settings remain ordinary targets: [rooms.cpp:57](/home/speak/workspace/mui/showcase/widget-breakout/src/rooms.cpp:57). | **P: behind.** Objective recognition is weaker. **B: behind.** Displayed state does not reliably govern consequences. **R: behind.** Target categories require more interpretation. | Unify component and simulation actions; teach the core objective; distinguish mechanisms from ordinary targets; give haunting an observable personality. |
| `fun_feel` | 64 | **used, viewed:** assisted pointer/key clears verified catches and steering. Pointer persistence disables keys; long vertical exchanges occurred without score progress. Priority is explicit at [sim.cpp:73](/home/speak/workspace/mui/showcase/widget-breakout/sim/src/sim.cpp:73). | **P: behind.** Exchanges produce less reliable reward. **B: behind.** Deliberate choices are less legible. **R: behind.** Input switching undermines arcade control. | Fix device arbitration and focus; evaluate natural-speed catches with unfamiliar players; tune repetitive trajectories and steering from recordings. |
| `level_design` | 73 | **used, measured, viewed:** all five rooms cleared, with distinct circuits, beams, chamber, darkness and cooperation. Definitions: [rooms.cpp:35](/home/speak/workspace/mui/showcase/widget-breakout/src/rooms.cpp:35). Observed clear frames are strategy-dependent, not difficulty benchmarks. | **P: behind.** Progression has less authored breadth. **B: behind.** Each rule receives less systematic exploration. **R: behind.** Challenge variety remains limited. | Give each mechanic introduction, combination and climax; validate three-life progression with humans; balance reward presets; add meaningful layout variations. |
| `juice` | 72 | **used, viewed:** banking text, circuit pulses, trails and rewind tint communicate events. Ordinary impacts are slight; README `room-2-beam.png`, top row, washes through labels. Event hierarchy: [fx.cpp:173](/home/speak/workspace/mui/showcase/widget-breakout/src/fx.cpp:173). Sound synchronization unverified. | **P: behind.** Celebrations lack equivalent anticipation/payoff. **B: behind.** Feedback is less economical and precise. **R: behind.** Effects lack comparable visual cohesion. | Strengthen contact/break distinctions; use widget-shaped debris; choreograph shutdown; preserve labels during beams; audition synchronization and add reduced motion. |
| `art_direction` | 55 | **used, viewed:** five skins remain visually separate demonstrations. Shared wiring and cores help, but paper grain, neon bloom and CRT distortion lack a common haunted-world treatment. Style switching: [app.cpp:235](/home/speak/workspace/mui/showcase/widget-breakout/src/app.cpp:235). | **P: behind.** Less distinctive personality. **B: behind.** Less disciplined visual vocabulary. **R: behind.** Less unified presentation. | Establish shared materials, symbols and haunting behavior; constrain room variations; reduce treatments that obscure play. |
| `ui_typography` | 50 | **used, viewed:** unreadable light-theme inspector/hints; unlabeled visible sliders; symbolic HUD; misaligned reward rows. [paint.cpp:529](/home/speak/workspace/mui/showcase/widget-breakout/src/paint.cpp:529), [app.cpp:155](/home/speak/workspace/mui/showcase/widget-breakout/src/app.cpp:155). | **P: behind.** Weaker reward hierarchy. **B: behind.** Essential text is less readable. **R: behind.** HUD styling and spacing are less consistent. | Fix overlay foreground/background pairs; show slider names; label resources; align card sections; enlarge inspector annotations and clarify preset selection. |
| `onboarding_readability` | 45 | **used, viewed:** opening teaches launch but insufficiently explains movement, goal, banking or inspection. Darkness hides collision information. Hints expire: [field.cpp:451](/home/speak/workspace/mui/showcase/widget-breakout/src/field.cpp:451). | **P: behind.** Opening guidance is incomplete. **B: behind.** Rules cannot always be inspected reliably. **R: behind.** Actionable state is harder to see. | Repair final composited contrast; stage switch/core and banking lessons; retain contextual help; make input switching and recovery dependable. |
| `sound_music` | 60 provisional | **viewed, inferred:** event sounds, positional pan and pitched chains exist at [sound.cpp:60](/home/speak/workspace/mui/showcase/widget-breakout/src/sound.cpp:60). No continuous music system evidenced. Supplied quiet/headless run cannot establish listening quality. | **P: provisionally behind.** Musical payoff is unverified. **B: provisionally behind.** Complete sonic identity is unestablished. **R: behind.** No equivalent authored score is evidenced. | Provide audible synchronized capture; audition dense sequences and losses; compose a reactive musical identity; measure device latency and masking. |
| `framework_showcase` | 65 | **used, viewed:** real copilot actions, scripts and rewind are substantial. Contradictory widget state, actor substitution, nonsemantic inspector and canvas omissions compromise the demonstration. [field.cpp:281](/home/speak/workspace/mui/showcase/widget-breakout/src/field.cpp:281), [copilot.cpp:124](/home/speak/workspace/mui/showcase/widget-breakout/src/copilot.cpp:124). | **P: ahead** in exposed semantic instrumentation. **B: ahead** in demonstrated runtime/actor inspection. **R: ahead** in editable scripted mechanics exposed through MCP. These are capability advantages, not superior play quality. | Make every offered action affect authoritative state; preserve actors; expose inspector content and canvas entities; demonstrate complete accessible and agent runs. |
| `code_tests_docs` | 76 | **viewed:** clean simulation separation; collision, replay, banking, scripts, bots and signature-media coverage. Tests mostly drive field actions, missing observed input defects: [breakout_test.cpp:240](/home/speak/workspace/mui/showcase/widget-breakout/tests/breakout_test.cpp:240). No fresh pass results. | **P: behind** demonstrated release readiness; internals unavailable. **B: behind** that readiness bar; internal quality unranked. **R: behind** platform-validation evidence; internals unavailable. | Add mixed-input/focus/state-consistency regressions and contrast checks; publish current mcpp/platform results; audit presentation allocations; document reproducible native launch/capture commands. |

## Benchmark summary

[Peggle Deluxe](https://store.steampowered.com/app/3480/Peggle_Deluxe/) is the reward and guided-progression reference. [Baba Is You](https://store.steampowered.com/app/736260/Baba_Is_You/) makes manipulable rules the game itself. [Breakout: Recharged](https://atari.com/blogs/newsroom/atari-s-breakout-recharged-fires-away-onto-pc-consoles-nintendo-switch-and-atari-vcs-today) supplies the closest audiovisual arcade reference, including its original score.

Openmui’s distinctive advantage is inspectable, editable machinery. Its largest deficits are trustworthy interaction, readable state and visual identity. The interface premise also needs more authored reactions to approach the requested *There Is No Game* reference.

## Change from the previous record

Only four test image hashes changed within the subject since `126cb48`; gameplay-source improvement is not established. Score reductions below reflect stronger evidence from use.

| Dimension | Previous → current | Reason |
|---|---:|---|
| `concept_clarity` | 76 → 66 | Direct control/state contradiction observed. |
| `fun_feel` | 68 → 64 | Device switching failure and repetitive exchanges observed. |
| `level_design` | 73 → 73 | Variety confirmed; progression limitations remain. |
| `juice` | 72 → 72 | Visual feedback confirmed; synchronization still unverified. |
| `art_direction` | 55 → 55 | Same fragmented visual system. |
| `ui_typography` | 54 → 50 | Inspector contrast failure confirmed across light themes. |
| `onboarding_readability` | 48 → 45 | Focus, input and darkness failures compound onboarding. |
| `sound_music` | 60 → 60 | Still provisional without listening. |
| `framework_showcase` | 79 → 65 | State divergence, actor misattribution and inspection gaps. |
| `code_tests_docs` | 79 → 76 | Existing tests miss consequential real-input defects. |

## Highest-priority changes

1. Route widget operations into authoritative simulation state.
2. Let keyboard/controller input supersede stale pointer position.
3. Repair Room 3/5 overlay contrast and Room 4 visibility.
4. Recover field focus after choices; make progression keys dependable.
5. Block inspector click-through and expose its contents semantically.
6. Preserve originating actors through reward callbacks.
7. Teach goal, movement, banking and rewind through staged play.
8. Unify art direction, slider labels, HUD and reward-card layout.
9. Add real-input regressions, human playtests and audible signature footage.

## Detail defects

Beyond the reproduced failures above:

- **Room 2/5 sliders:** Brightness, Volume and Signal names exist semantically but are absent visually.
- **Room 1 HUD:** chain display competes with Weather; resource diamonds have no persistent explanation.
- **Inspector:** 10 px role/ID annotations crowd the shield; the panel covers active gameplay and does not pause it.
- **Room 3:** zoomed files become anonymous colored bars; grain weakens fine outlines.
- **Room 5:** Boost arrows overlap labels; pale charge pips and ball forecast fade into the background.
- **Reward cards:** unequal heights misalign preset/action rows; pervasive glow flattens hierarchy; code editor lacks monospace presentation.
- **Rewards:** two versus five magnet catches have no expressed compensating cost ([rooms.cpp:218](/home/speak/workspace/mui/showcase/widget-breakout/src/rooms.cpp:218)).
- **Tests:** “no endless orbits” counts paddle contacts as progress, allowing repetitive unproductive exchanges ([breakout_test.cpp:184](/home/speak/workspace/mui/showcase/widget-breakout/tests/breakout_test.cpp:184)).
- **Documentation:** Web scripting remains unavailable; human playtesting and reduced motion are explicitly unfinished in `dev-story.md:65` and `:86`.

```json
{"overall":45,"mean":62.6,"dimensions":{"concept_clarity":66,"fun_feel":64,"level_design":73,"juice":72,"art_direction":55,"ui_typography":50,"onboarding_readability":45,"sound_music":60,"framework_showcase":65,"code_tests_docs":76}}
```