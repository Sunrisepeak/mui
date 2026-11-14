Overall: 55/100  
Mean: 66.1/100

The previous review’s input and contrast fixes are real. This remains a prototype: readable state, truthful semantic actions, collision robustness, and a coherent visual identity still prevent release readiness.

Used the supplied headless process; inspected source at `f59bfec`, AGENTS.md, the README, dev story, mgame/S4 design, implementation, tests, and all five README media files. **used** means MCP interaction checked against pixels/tree; **measured** means observed numerical results; **viewed** means inspected code/images; **inferred** means unverified.

Completion used frame assistance and semantic observations to assist catches. It establishes reachability, not natural-speed difficulty, latency, or performance. Audio was unavailable in the supplied quiet session.

## Use log

| Task | Calls, briefly | Expected | Actual | Result |
|---|---|---|---|---|
| Newcomer opening | `ui_capture`, `ui_snapshot`, fresh run; pointer move/click, Space | Understand movement, objective and controls | Launch hint and forecast visible; movement, core objective and banking insufficiently taught | Fail |
| Room 1, pointer | `ui_input(move/click)`, paused `step` batches, captures | Open shield, catch, bank, finish | Cleared at frame 8239, score 860, three lives; one earlier attempt developed a stuck ball | Pass with defect |
| Room 1, keys | Left/Right through real input; Space; assisted steps | Aim, steer and clear after pointer use | Cleared at frame 1696, score 940, three lives | Pass |
| Banking/rewind | Observe BANKED; hold R for 60 input frames; snapshot/capture | Reverse play, pay, show abandoned shot | Ghost appeared; sparks changed from 100 to −60, while HUD showed 0 | Pass mechanically |
| Semantic switch | `ui_act(toggle)`; snapshot | Honest action result | Returned “done,” value false; next snapshot already true again | Fail |
| Reward/casing | Click Wide preset, casing, Take; `ui_invoke(gift.wide)` | Reload rules and continue | Returned half-width 95, duration 1389 steps; Take preserved evaluator actor and restored field focus | Pass |
| Room 2 | Pointer play; inspect charging sliders and dam | Charge → beam → opening | Charging and dam removal observed; cleared at frame 2479 | Pass |
| Room 3 | Hold Z, Tab, pointer play, captures | Discover chamber and inspect machinery | Zoom reached 0.5; cleared at frame 1537; canvas annotations absent | Partial |
| Room 4 | X, repeated `ui_act(scan)`, play | Reveal actionable state; enforce cooldown | Caches outlined; repeat refused with 7.5 seconds remaining; ordinary labels remained faint; cleared at frame 3528 | Fail readability |
| Room 5 | Keys 1–4, snapshots, copilot observations, assisted play | Orders, cooperation, completion | All orders registered; duet reached five; completed after restarts at frame 4653, score 8710, longest banked chain 30. Finisher not witnessed | Partial |
| Inspector | Tab; click inside panel; close; advance | Inspect without affecting gameplay | Paused correctly, but panel click queued a launch that fired after closing | Fail |
| Loss/recovery | Miss catches, relaunch, Enter | Understand loss and restart | Lives decreased; defeat screen and Enter restart worked; completion also restarted correctly | Pass |

**Defects reproduced through use**

1. **Ball lodged under paddle:** after a late lateral catch, the ball remained around `(491,702)` under the paddle. Waiting another 120 input frames advanced simulation from 16026 to 16272 without changing its position. Paddle particles continued; moving away released it. Collision handling is at [sim.cpp:431](/home/speak/workspace/mui/showcase/widget-breakout/sim/src/sim.cpp:431).
2. **Successful action immediately undone:** pause Room 1; toggle `main/field/room.1/brick.5`. The result says false, but the subsequent snapshot says true. The restoration callback is at [field.cpp:85](/home/speak/workspace/mui/showcase/widget-breakout/src/field.cpp:85).
3. **Inspector launch leakage:** with a waiting ball, open Tab, click `(1050,320)` inside its panel, close Tab and advance. The ball launches. Inspector input is not isolated by [field.cpp:487](/home/speak/workspace/mui/showcase/widget-breakout/src/field.cpp:487).
4. **Unreadable reward descriptions:** finish Room 4 and wait for settling. The two dark cards inherit dark text from the liquid theme. Titles and descriptions almost disappear: [app.cpp:155](/home/speak/workspace/mui/showcase/widget-breakout/src/app.cpp:155).
5. **Hidden rewind debt:** earn 100 sparks, rewind across the bank that supplied them. The tree can report negative sparks while the HUD clamps the number to zero: [paint.cpp:512](/home/speak/workspace/mui/showcase/widget-breakout/src/paint.cpp:512).
6. **Dark-room readability:** enter Room 4 and press X. Cache outlines appear, but normal control names remain difficult to read. Faint outlines alone do not resolve this: [paint.cpp:305](/home/speak/workspace/mui/showcase/widget-breakout/src/paint.cpp:305).

## Framework and openas feedback

Stable IDs, bounds, ball state, checksums, cooldown refusals and copilot observations made investigation effective. Input reports identified hit targets. Reward selection now preserves `agent:codex-evaluator;via=service:input`; copilot actions identify `agent:copilot`.

The semantic interface still advertises operations whose apparent success is immediately reversed. Ball-operated bricks should expose authoritative read-only state, or explicitly refuse direct manipulation. Restoring state silently is insufficient.

The inspector’s visible prose is absent from the tree. Room 3’s canvas representation also loses useful state: Wi-Fi appeared as a switch without a checked value, while the core appeared as an article. Add consistent semantic projections across promotion levels.

Gameplay input reported no journaled actions. After extensive play, undo’s dry run still targeted the earlier widget toggle. Distinguish simulation rewind, action effects and runtime undo clearly. No person-only or irreversible workflow was offered, so confirmation handling was not exercised.

## Dimension table

**P** = Peggle Deluxe; **B** = Baba Is You; **R** = Breakout: Recharged. Comparisons are editorial judgments against their published presentation, not fresh comparative playtests. Commercial internal code/tests were unavailable.

| Dimension | Score | Evidence and basis | Individual comparison | To reach 90, highest impact first |
|---|---:|---|---|---|
| `concept_clarity` | 72 | **used, viewed:** switch–shield–core causality works and sliders are now named. However, the objective needs explanation; Mute/Snooze remain ordinary targets rather than meaningful settings. [rooms.cpp:35](/home/speak/workspace/mui/showcase/widget-breakout/src/rooms.cpp:35) | **P: behind:** goal recognition is weaker. **B: behind:** displayed operations are less trustworthy. **R: behind:** target categories require more interpretation. | Teach the core objective through an opening sequence; distinguish mechanisms from filler; remove misleading actions; give ordinary controls meaningful consequences. |
| `fun_feel` | 66 | **used, measured:** both control methods cleared Room 1; device switching works. Spin and steering offer agency, but the lodged-ball failure interrupts play. [sim.cpp:371](/home/speak/workspace/mui/showcase/widget-breakout/sim/src/sim.cpp:371) | **P: behind:** reward is less dependable. **B: behind:** deliberate decisions have less legible consequences. **R: behind:** collision robustness undermines arcade control. | Fix underside/overlap contacts; test late saves and moving catches; tune steering and repetitive trajectories with unfamiliar players at normal speed. |
| `level_design` | 73 | **used, measured, viewed:** all five rooms cleared; circuits, beams, chamber, darkness and cooperation provide real variety. Clear frames are strategy-dependent, not difficulty benchmarks. [rooms.cpp:69](/home/speak/workspace/mui/showcase/widget-breakout/src/rooms.cpp:69) | **P: behind:** less authored progression. **B: behind:** fewer systematic rule combinations. **R: behind:** narrower challenge variety. | Give each mechanic introduction, combination and climax; validate the three-life run with people; clarify reward tradeoffs; add layout variations. |
| `juice` | 72 | **used, viewed:** banking, trails, cracks, pulses, breakthrough and rewind ghost communicate events. Ordinary breaks remain generic particle bursts. Sound synchronization unverified. [fx.cpp:173](/home/speak/workspace/mui/showcase/widget-breakout/src/fx.cpp:173) | **P: behind:** weaker anticipation and payoff. **B: behind:** feedback is less economical. **R: behind:** effects are less cohesive. | Differentiate widget impacts and debris; choreograph shutdown; preserve labels during beams; audition synchronization; provide reduced motion. |
| `art_direction` | 55 | **used, viewed:** shared wiring and cores help, but glass, neon, paper grain, CRT and liquid read as separate theme demonstrations. [app.cpp:235](/home/speak/workspace/mui/showcase/widget-breakout/src/app.cpp:235) | **P: behind:** less personality. **B: behind:** less disciplined visual vocabulary. **R: behind:** less unified presentation. | Establish one haunted visual language, shared materials and symbols; constrain room treatments; make the panel visibly react as a character. |
| `ui_typography` | 58 | **used, viewed:** inspector foreground and slider labels improved. Reward-card contrast still fails; cards misalign; HUD symbols lack explanation. [paint.cpp:578](/home/speak/workspace/mui/showcase/widget-breakout/src/paint.cpp:578) | **P: behind:** weaker reward hierarchy. **B: behind:** essential text is less reliable. **R: behind:** spacing and styling are less consistent. | Fix every overlay foreground/background pair; align card sections; label resources; show selected/default presets; enlarge annotations; use monospace for scripts. |
| `onboarding_readability` | 55 | **used, viewed:** launch guidance exists, input switching is repaired, and inspection pauses. Movement, winning and banking remain under-taught; darkness obscures names. [field.cpp:465](/home/speak/workspace/mui/showcase/widget-breakout/src/field.cpp:465) | **P: behind:** opening guidance is incomplete. **B: behind:** rules are harder to inspect. **R: behind:** actionable state is harder to see. | Stage movement → switch → core → bank → rewind; retain contextual help; make scan reveal readable actionable information; verify final composited contrast. |
| `sound_music` | 60 provisional | **viewed, inferred:** event sounds, pitched chains and positional pan exist; no continuous music system found. Quiet/headless use supplied no listening evidence. [sound.cpp:60](/home/speak/workspace/mui/showcase/widget-breakout/src/sound.cpp:60) | **P: provisionally behind:** musical payoff unverified. **B: provisionally behind:** sonic identity unestablished. **R: behind:** no equivalent authored score evidenced. | Supply synchronized audible footage; audition dense chains and losses; compose a reactive identity; measure device synchronization and masking. |
| `framework_showcase` | 72 | **used, viewed:** components, canvas, rewind, scripted rewards and actor-tagged copilot are substantial. Silent action reversal and nonsemantic inspection weaken the central promise. [copilot.cpp:124](/home/speak/workspace/mui/showcase/widget-breakout/src/copilot.cpp:124) | **P: ahead:** more exposed runtime instrumentation. **B: behind:** rule/state explanations are less coherent. **R: ahead:** editable scripted mechanics are directly inspectable. | Make advertised actions truthful; expose inspector content and canvas values; isolate overlay input; demonstrate complete accessible/agent workflows with useful provenance. |
| `code_tests_docs` | 78 | **viewed:** clean simulation separation, mechanism/replay tests, bots, scripts and signature-media generation. New regressions improve coverage, but observed failures escape it. [breakout_test.cpp:468](/home/speak/workspace/mui/showcase/widget-breakout/tests/breakout_test.cpp:468) | **P: behind:** demonstrated release confidence. **B: behind:** demonstrated release confidence. **R: behind:** platform-validation evidence. Their internal implementation quality is unranked. | Add overlap, overlay-input, semantic-result and contrast regressions; publish fresh mcpp/platform results; regenerate stale media; conduct human playtests. |

`mcpp test -p openmui.showcase.widget_breakout.sim` failed before testing because the read-only environment prevented writing compiler metadata. No fresh passing test result is claimed.

## Benchmark summary

[Peggle Deluxe](https://store.steampowered.com/app/3480/Peggle_Deluxe/) supplies the clearest guided arcade/reward reference. [Baba Is You](https://hempuli.com/baba/) makes visible rules authoritative and manipulable. [Breakout: Recharged](https://atari.com/blogs/newsroom/atari-s-breakout-recharged-fires-away-onto-pc-consoles-nintendo-switch-and-atari-vcs-today) supplies the closest cohesive audiovisual arcade reference, including its electronic soundtrack.

Openmui’s advantage is inspectable, editable machinery. Its largest deficits are visual coherence and readable, trustworthy state. Against *There Is No Game*, the haunted-interface premise needs more authored reactions and surprise.

## Change from the previous record

Compared with `d4f7012`:

| Dimension | Previous → current | Reason |
|---|---:|---|
| `concept_clarity` | 66 → 72 | State restoration and visible slider names help. |
| `fun_feel` | 64 → 66 | Device arbitration fixed; lodged-ball failure limits improvement. |
| `level_design` | 73 → 73 | Same rooms and progression. |
| `juice` | 72 → 72 | Same feedback system; audio still unverified. |
| `art_direction` | 55 → 55 | No coherent art-direction change. |
| `ui_typography` | 50 → 58 | Inspector and labels repaired; reward contrast remains broken. |
| `onboarding_readability` | 45 → 55 | Focus, input and inspection improved; teaching/darkness remain weak. |
| `sound_music` | 60 → 60 | Still provisional without listening. |
| `framework_showcase` | 65 → 72 | Actor propagation fixed; authoritative state retained, though action results mislead. |
| `code_tests_docs` | 76 → 78 | Useful regressions added; important gaps remain. |

## Highest-priority changes

1. Resolve paddle overlap and underside contacts; add late-save regressions.
2. Repair reward-card and dark-room contrast after all rendering effects.
3. Teach movement, core shutdown, banking and rewind through staged play.
4. Replace silently reversed widget actions with truthful read-only state or explicit refusals.
5. Make the inspector semantic and block/clear gameplay input while inspecting.
6. Display rewind debt and explain its effect on subsequent banking.
7. Establish one haunted art direction and stronger shutdown/reward choreography.
8. Clarify reward defaults, selected presets and tradeoffs.
9. Add normal-speed human playtests, audible captures and current platform test evidence.

## Detail defects

- **Room 1, upper right:** chain counter occupies the Weather tile’s area.
- **Inspector, shield area:** 10 px role/ID labels crowd one another.
- **Inspector, bottom edge:** return instruction sits against the panel boundary.
- **Room 3:** inspector omits individual canvas annotations; distant files become anonymous bars.
- **Room 5:** Boost arrows overlap their labels; pale forecast and charge cues weaken on the light background.
- **Reward casing:** proportional script text impairs code scanning.
- **Reward cards:** no clear selected/default preset; two versus five magnet catches lacks an expressed tradeoff.
- **Tests:** “no endless orbits” counts paddle contacts as progress, allowing repeated ineffective contacts: [breakout_test.cpp:184](/home/speak/workspace/mui/showcase/widget-breakout/tests/breakout_test.cpp:184).
- **Tests:** the showcase test promising keyboard steering checks clicks, restoration and focus, but never presses a movement key. Lower-level sampler coverage does exist.
- **README media:** Room 2 lacks the current visible slider names; Room 3 still shows the old dark-text hint.
- **Documentation:** reduced motion and unfamiliar-player testing remain explicitly unfinished at [dev-story.md:65](/home/speak/workspace/mui/showcase/widget-breakout/dev-story.md:65); Web scripting remains unavailable at line 86.

```json
{"overall":55,"mean":66.1,"dimensions":{"concept_clarity":72,"fun_feel":66,"level_design":73,"juice":72,"art_direction":55,"ui_typography":58,"onboarding_readability":55,"sound_music":60,"framework_showcase":72,"code_tests_docs":78}}
```