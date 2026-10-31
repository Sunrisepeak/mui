**Overall: 25/100**  
**Mean: 41.8/100**

The fight does not meet the presentation bar. Its most serious failure is **physical contact**: the simulation registers blows and throws that the drawn bodies do not convincingly perform. More effects or longer freezes will not repair that.

Evaluated at `f59bfec`, using both running programs and over 200 captures. I read the named presentation sources, HUD, six fighters’ move/look/box files, README, dev-story and relevant fighting designs. Visual coverage is sampled, not an exhaustive certification of every joint in every move. Unplayed move verdicts below are explicitly provisional. Audio and controller vibration were not assessed in these quiet, headless sessions.

**Use log**

`f` means the stage’s frame; cinematic `t` means elapsed cinematic frames. Capture references identify the corresponding `ui_capture` outputs in this session.

| Task | Calls / method | Expected | Actual | Result |
|---|---|---|---|---|
| Person input | `ui_input` click, Escape, Resume, Rematch; keyboard J | Immediate response through focus/input | Initial Escape missed; clicking stage restored it. Resume, Rematch and J worked | Partial |
| Frame inspection | `ui_clock pause`; stage `pause`, repeated `step`; snapshots and regional captures | Exact inspection | Stage stepping worked; a large clock step advanced only four simulation frames | Pass, with limitation |
| Six fighters | `fighters`, `press`, neutral/release; idle, walk, required normals, jump, throw, sweep | Joined bodies and appropriate hands | Seams, reversed gaze, sliding, disconnected grabs and floor penetration | Fail |
| Hit tiers | Light/medium/heavy; startup counter, recovery punish, held-back block, simultaneous heavies | Distinct contact and outcomes | Rules distinguished them; pictures frequently did not | Fail |
| Supers | `fill`; motions one frame each; all six L3 timelines, Mo/Ni/Kun L1–L2 | Directed, readable bodily action | Occlusion, misplaced sparks, weak acting and broken exits | Fail |
| Versus flow | Two-jab combo; repeated heavies through two rounds; capture results; click Rematch | Legible calls and complete flow | 2 hits/500 damage, counter, K.O., next round, 2–0 results and rematch worked | Pass |

Usage defects and reproduction are detailed below. The missing supplied pause PNG was replaced by inspecting the live pause card. Yue’s requested `236236K` produces a normal; her documented L3 is **720K**, which I successfully executed.

**Framework and openas feedback**

The shared state exposes useful health, move phase, hitstop, combo and frame information. Reversible actions and readable refusals worked: versus `fill` correctly refused because it belongs to training.

However, the fight exposes essentially one canvas. Painted menu buttons, health bars and calls lack individual semantic nodes and bounds. Clicking Rematch changes the match while the input journal reports no action. Add semantic card controls, HUD values, contact/socket inspection and cinematic time. Actor ownership and irreversible confirmation were not exercised; they receive no credit here. `ui_wait(settled)` also cannot settle an actively animating fight, so inspection required explicit pausing.

**Dimension scores**

Evidence: **U** used; **M** measured; **V** viewed code/capture; **I** inferred. “Provisional” marks incomplete experiential evidence.

| Dimension | Score | Evidence and basis | Individual benchmark comparison | To reach 90, highest impact first |
|---|---:|---|---|---|
| `rig_binding` | 38 | U/V: Mo L3 f278 sleeve sections separate; Kun f239 wrist gap; six sweep captures f147/159 penetrate the floor | Behind SF6’s connected anatomy; behind Strive’s controlled deformations; behind Skullgirls’ pose continuity | Correct joint registration, corrective drawings, floor placement and world-space foot locks |
| `hands` | 38 | U/V: Ni throw f96 retains fists; Yue f195 grabs empty air; other fighters have useful swaps but unreliable attachment | Behind SF6’s contextual grips; behind Strive’s expressive hands; behind Skullgirls’ drawn contact | Give Ni independent wrists and grip drawings; author both hands through every contact |
| `attack_design` | 42 | U/V/I: Mo jab f95 and Ke f119 use fingers; Mo j.HP f134 hits far below its hands | Behind SF6’s strike mechanics; behind Strive’s unmistakable silhouettes; behind Skullgirls’ limb/contact agreement | Replace finger attacks; align striking limb, silhouette and attack box |
| `pose_and_spacing` | 40 | U/V: walk f0/9/18 slides; Ni special f127 faces away; repeated interpolated limb arrangements lack convincing weight transfer | Behind SF6’s body mechanics; behind Strive’s held poses; behind Skullgirls’ breakdowns and overlap | Author support changes, breakdowns, recoil, smears and corrective silhouettes |
| `hit_feel` | 50, provisional | U/M: Mo LP/MP/HP stop 7/9/14 frames; counter/punish add hold. Contact and reactions undermine weight; sound unverified | Behind SF6’s synchronized impact; behind Strive’s counter punctuation; behind Skullgirls’ deformation | Fix visible contact and reaction first; then audition timing, sound and vibration together |
| `hit_tiers` | 55 | U/M/V: counter and punish calls differ; blocked heavy f126 is subdued; clash f126 snaps to guard-like poses under radial spokes | Behind SF6’s outcome clarity; behind Strive’s counter hierarchy; behind Skullgirls’ distinct impact drawings | Preserve clash contact poses; author separate block, counter, punish and throw presentations |
| `hit_contact` | 25 | U/V: Mo j.HP f134 phantom contact; Kun L3 f271 spark in the gap; Ke f217 spark near feet | Behind SF6’s body contact; behind Strive’s directional impact; behind Skullgirls’ drawn contact points | Resolve effects against actual drawn contact sockets; remove arbitrary spark displacement |
| `vfx_craft` | 45 | U/V: character materials differ, but Kun’s spokes resemble a spinner; Ni’s bolts are flat; Mo f294 stroke ends rectangularly | Behind SF6’s layered sparks; behind Strive’s authored shapes; behind Skullgirls’ consistent linework | Design directional cores, silhouettes and decay; replace generic spokes and visible rectangular bounds |
| `super_staging` | 38 | U/V: Ni f267 attacker hidden; Kun f225 heads covered; Yue f360 interaction covered | Behind SF6’s shot continuity; behind Strive’s cinematic emphasis; behind Skullgirls’ readable staging | Reboard shots around actor safe areas, visible impacts and continuous exits |
| `super_acting` | 30 | U/V: Mo f218 throat fingers; Ni f233 headbutt misses; Yue f268 lift lacks a credible grip | Behind SF6’s performance; behind Strive’s character poses; behind Skullgirls’ expressive deformation | Animate paired attacker/victim performances with expressions and actual bodily contact |
| `knockdown_ko` | 32 | U/V: all sweep victims flatten poorly; cinematic exits stand victims upright; versus f1282 clips Yue offscreen | Behind SF6’s falls; behind Strive’s dramatic landings; behind Skullgirls’ drawn collapse | Repair animation clocks, ground alignment, cinematic handoff and corner framing |
| `fight_ui` | 68 | U/V: versus f205 combo, f1282 K.O., f1635 results are readable; initial focus fails and jumping heads disappear behind HUD | Behind SF6’s hierarchy; behind Strive’s motion integration; behind Skullgirls’ combat-safe readability | Fix focus and semantic controls; reserve actor safe areas; strengthen thin posture/meter information |

Relevant implementation anchors: [animation clocks](/home/speak/workspace/mui/mgame/gongfu/present2d/src/present2d.cpp:136), [foot IK](/home/speak/workspace/mui/mgame/gongfu/present2d/src/present2d.cpp:228), [part deformation](/home/speak/workspace/mui/mgame/gongfu/present2d/src/present2d.cpp:859), [camera interpolation](/home/speak/workspace/mui/mgame/gongfu/present2d/src/present2d.cpp:1127), [cinematic grips](/home/speak/workspace/mui/mgame/gongfu/present2d/src/present2d.cpp:1360), [spark displacement](/home/speak/workspace/mui/mgame/gongfu/present2d/src/present2d.cpp:1681), [panels](/home/speak/workspace/mui/mgame/gongfu/present2d/src/fx.cpp:162), [HUD/cards](/home/speak/workspace/mui/showcase/fighting/src/hud.cpp:530).

**Benchmark summary**

SF6 remains the strongest reference here for bodily contact, reaction and cinematic continuity. Strive demonstrates how held silhouettes and decisive punctuation can carry limited animation; its [official systems guide](https://www.guiltygear.com/ggst/en/battle/systems/) also describes wall-break transitions. Skullgirls is the clearest reference for authored deformation and overlapping action. Its developers described approximately **1,300 frames per character on average**, rather than universally “thousands.” [Developer animation article](https://skullgirls.com/2011/04/on-animation-in-skullgirls/).

No evaluated craft dimension demonstrates a lead over any of them.

**Highest-priority changes**

1. Finish one complete fighter/contact sequence to the target standard before expanding effects.
2. Register every shoulder, elbow, wrist, hip, knee and ankle; add corrective drawings.
3. Replace Mo/Ke finger strikes with credible fists, palms or other martial strikes.
4. Author paired throw poses and persistent grip sockets.
5. Fix thrown/knockdown clocks and cinematic exit continuity.
6. Align attack boxes and sparks with the drawn striking surface.
7. Reboard every L3 with actor safe areas and visible climaxes.
8. Replace generic radial effects with authored directional impact drawings.
9. Tune reactions, sound and vibration together in an audible playable session.
10. Fix initial focus and expose actual semantic HUD/menu controls.

**Detail defects**

These are **used/viewed** findings; the captures supply reproducible fighter, move and frame identifiers.

- **Binding:** Mo’s upper arm, sleeve and cuff separate during brush motion, L3 f278. Kun’s forearm and palm separate at f239; his rear arm appears between his legs at f207. Li’s elbow/cuff articulation reads as disconnected pieces. Ni’s neck forms a rigid kink and her face looks away from her opponent during attacks, including special f127. Her [head mirroring](/home/speak/workspace/mui/showcase/fighting/fighters/ni.look.gongfu.toml:377) deserves immediate inspection.
- **Feet/falls:** walk samples f0/9/18 move planted feet with the root. At sweep f147/159, all six use awkward horizontal falls; Li’s head drops out of view below the floor/HUD. Jump captures around f36–39 put several heads behind the top HUD.
- **Ordinary contact:** Mo j.HP f134 registers a hit with substantial air between hands and victim. Li 5LP f119 drives through the neck silhouette. Yue 5HP f127 separates palm and spark. Ni’s blocked-heavy pose guards high while the blow arrives lower.
- **Throws:** Mo/Kun/Li/Ke f120, Ni f96 and Yue f195 place victims horizontally near the floor while grabbing hands remain above them. Yue f202 then raises the victim again. `resolve()` uses match time for `thrown`; ordinary throws lack the cinematic grip-placement path. This is a concrete animation-clock/attachment failure.
- **Tier effects:** clash f126 replaces the two attacks with guard-like poses and pale spokes. Mo’s small black contact marks disappear against dark clothing; counter/punish rely too heavily on colour, scale and text.

Each L3 fails differently:

| Fighter / captures | Specific failure | Required correction |
|---|---|---|
| Mo f206/218/278/294/378 | Arm/body penetration; two fingers to throat; sleeve separation; rectangular ink edge; upright exit | Choreograph connected palm/brush strikes, redraw the smear boundary and preserve the final fall |
| Ni f205/233/267/323/357 | Knee placement weak; headbutt lacks forehead contact; lower panel hides attacker; later framing loses her; upright exit | Stage a visible clinch, headbutt and kick sequence with a continuous victim performance |
| Kun f207/225/239/271/357 | Rear-arm placement breaks; panel covers heads; wrist gap; shoulder spark floats; upright exit | Build the sequence around readable ball-to-body and shoulder-to-body contact |
| Li f239/284/319/329/349 | Whole-body flip substitutes for a turn; cracks obscure limbs; panel covers heads; whiteout replaces climax | Draw the turn and final strike; subordinate glass overlays to the actors |
| Yue f268/310/345/360/410/450/460 | Unsupported lift; victim changes orientation abruptly; flat cartoon clouds; summit panel hides action; whiteout; timer covers poster head | Author the lift/release/descent as one paired action on painted planes |
| Ke f217/257/307/327/367 | Frozen sparks float away from blows; accumulated effects hide contact; panel covers heads; whiteout and upright exit | Keep frozen sparks at actual contacts and show the deferred-damage release bodily |

For Ke, the renderer deliberately displaces held sparks by up to approximately **±70 world units on each axis**. That directly conflicts with precise contact.

Mo’s “two fingers to the face” is explicit in [her fighter file](/home/speak/workspace/mui/showcase/fighting/fighters/mo.fighter.gongfu.toml:1); Ke’s throat-point jab is explicit in [her look](/home/speak/workspace/mui/showcase/fighting/fighters/ke.look.gongfu.toml:703). These are attack-design faults. Kun balancing a ball on a finger is an appropriate prop gesture.

Initial Escape reproduction: launch directly into versus, press Escape before clicking the stage. Focus remained on selection; clicking stage then Escape worked. [Selection mounting takes focus unconditionally](/home/speak/workspace/mui/showcase/fighting/src/select.cpp:108). Repeated corner pressure also leaves Yue partly offscreen during K.O. f1282 and results f1635.

**Complete move and hit-tier verdict inventory**

**B = behind; F = failing.** Grouped entries each inherit the stated verdict and fix. Unplayed entries are **provisional, inferred from their authored data and shared rendering path**; they are not claimed as visually verified.

Every fighter has:  
`5LP, 5MP, 5HP, 5LK, 5MK, 5HK; 2LP, 2MP, 2HP, 2LK, 2MK, 2HK; j.LP, j.MP, j.HP, j.LK, j.MK, j.HK`.

All are **B—author stronger contact silhouettes and body mechanics**, except Mo/Ke `5LP, 2LP, j.LP`: **F—replace finger strikes**; Mo `j.HP`: **F—repair phantom contact**.

| Fighter | Remaining moves: verdict — most important fix |
|---|---|
| All six | `66, 44`: B—synchronize support feet and root travel. `throw, back throw`: F—paired grips and correct victim clock |
| Mo | `236LP/MP/HP, j.236P, 623LP/MP/HP, 214LK/MK`: B—bind brush action and effects to a convincing body action |
| Ni | `6MK, 236LK/MK/HK, 623LK/MK/HK, 214LP/MP/HP`: B—correct facing, neck and strike silhouettes |
| Kun | `6MK, 236LP/MP/HP, 623LP/MP/HP, 214LK/MK`: B—connect wrists, ball handling and weight transfer |
| Li | `shard, glint, 236LP/MP/HP, 214P, 623LK/MK/HK, 22K`: B—clarify hand-to-pane/effect causality |
| Yue | `6HP, 214LK/MK/HK, 236LP/MP/HP`: B—show shoulder/elbow/foot contact. `63214LP/MP/HP, 623P`: F—anchor the victim through the throw |
| Ke | `236LP/MP/HP, 214K, 623P, 22P`: B—separate clock gestures from the actual striking surface |

| Fighter | L1 verdict / fix | L2 verdict / fix | L3 verdict / fix |
|---|---|---|---|
| Mo | `236236P` B: readable projectile contact | `214214P` B: connected brush performance | `236236K` F: connected bodily choreography |
| Ni | `236236P` B: distinct successive blows | `214214P` F: animate three actual strikes | `236236K` F: visible clinch/contact |
| Kun | `236236P` B: replace flat cut-in/weak body action | `214214P` B: authored ball impact | `236236K` F: ball/shoulder contact |
| Li | `236236P` B, provisional: expressive pane placement | `214214P` B, provisional: readable shatter origin | `236236K` F: visible final strike |
| Yue | `360P` F, provisional: paired throw performance | `214214P` B, provisional: convincing pushing contact | `720K` F: continuous supported lift/slam |
| Ke | `236236P` B, provisional: legible rewind transition | `214214P` B, provisional: distinguish clone action | `236236K` F: exact frozen contacts |

Hit tiers: **light B—sharper contact; medium B—stronger recoil; heavy B—weight transfer; counter B—distinct impact drawing; punish counter B—distinct bodily collapse; block B—guard at contact; clash F—retain colliding attack poses; throw F—establish grip before displacement.** None stands beside the benchmarks.

```json
{"overall":25,"mean":41.8,"dimensions":{"rig_binding":38,"hands":38,"attack_design":42,"pose_and_spacing":40,"hit_feel":50,"hit_tiers":55,"hit_contact":25,"vfx_craft":45,"super_staging":38,"super_acting":30,"knockdown_ko":32,"fight_ui":68}}
```