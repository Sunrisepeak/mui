# Analysis: why the fighters come apart in motion (code, data or method)

Written by codex from frame-by-frame captures of 坤's normals (release build, 1920×1080) and the source; the maintainer had seen hands and heads part from the body.

The broken motion has several causes: a faulty skinning heuristic, inconsistent artwork and rig calibration, unsafe angle interpolation, and insufficient pose-specific art. Fixing the implementation will remove substantial damage, but it will not turn the existing cutouts into animation comparable to the named reference games.

There is also an important correction to the reported diagnosis. The committed design identifies **5MP as an elbow strike and 5HP as a shoulder charge**, not two progressively stronger straight punches. The observation that 5HP sends the arm downward and backward is correct; interpreting that alone as an incorrectly authored heavy punch conflicts with the design. See [the move descriptions, line 141](/home/speak/workspace/mui/.agents/docs/2026-10-29-fighting-hardcore-combat-design.md:141).

I inspected the six sheets, closer crops of the supplied full frames, the atlases, pose data, and presentation code. Below, `5HP-04` means the supplied image with that filename, not animation frame four. Samples are two frames apart; exact authored animation times are called `t`. The renderer maps simulation move frame `f` to clip time `f−1` in [present2d.cpp:159](/home/speak/workspace/mui/mgame/gongfu/present2d/src/present2d.cpp:159). Numerical reconstruction below uses the checked-out data and equations; it is not instrumentation of the release executable.

**What the supplied motion shows**

In **5LP**, particularly `5LP-01` and `-02`, the extended arm has a swollen, segmented sleeve, an abrupt sleeve-to-forearm transition, and a fist that reads as a separate attachment. The shoulder does not describe a convincing continuous upper arm emerging from the shirt. A small pale triangular projection also remains behind the opposite shoulder. The torso contributes little rotation or compression, so the jab reads primarily as a limb rearrangement.

The wrist interface is poorly calibrated, but nominal bone-length differences should not be mistaken for measured empty space. At `art_scale=0.32`, the front forearm’s declared anchor-to-tip distance is **33.72 stage units**, against a **38-unit bone**. However, opaque artwork extends beyond that declared tip. My alpha inspection found coverage reaching approximately 38.24 units along the forearm axis. Thus, “4.28 units of missing forearm” would be an incorrect diagnosis. The problem includes the shape and width of the joining surfaces, their landmarks, and the sleeve deformation—not simply absent pixels. The relevant definitions are [kun.look:353](/home/speak/workspace/mui/showcase/fighting/fighters/kun.look.gongfu.toml:353).

In **5MP**, `5MP-02` and `-03` show a folded forearm crossing the chest and an outward elbow. That direction is appropriate for the designed elbow strike. At `t=4`, upper arm `−4°` and forearm `170°` deliberately produce a nearly folded elbow. Changing the forearm to `0°` would change the attack’s identity.

What is defective is the construction: the elbow becomes a large rolled-sleeve bulb, another sleeve edge protrudes below it, and the fist merges awkwardly with the chest/collar region. The recovery in `-04` through `-07` sweeps the forearm upward beside the face before returning to guard, without convincing shoulder or scapular movement. The foot also advances and retreats while remaining apparently planted. These are visible rigging, contact, and choreography problems, not evidence that an elbow must point forward like a fist. See [kun.look:599](/home/speak/workspace/mui/showcase/fighting/fighters/kun.look.gongfu.toml:599).

In **5HP**, `5HP-03` through `-06` show the reported knee-level hands. The opaque front sleeve bulges across the chest, the rear arm exposes an abrupt upper-sleeve edge, and both arms hang in a way that reads as a collapsed guard rather than a forceful shoulder strike. In `-07`, the rear sleeve appears particularly disconnected from its expected shoulder attachment; the recovering front sleeve remains inflated.

At `t=7`, the front upper arm is `−150°`, forearm `−110°`, torso `66°`. Reconstructing forward kinematics places the front shoulder around **(23.5,106.3)** and wrist around **(−14.6,56.0)** relative to the fighter’s root. The renderer is therefore drawing the downward/backward arm specified by the data. For a straight punch this would be wrong data. For the documented shoulder charge, lowered arms are permissible; the failure is that the shoulder, chest, stepping action, and silhouette do not sell the intended contact. See [kun.look:611](/home/speak/workspace/mui/showcase/fighting/fighters/kun.look.gongfu.toml:611).

In **2MK**, `2MK-02` through `-04` show the extended low kick. The leg’s outline breaks into visibly separate trouser sections at the knee; folds rotate with their individual patches instead of changing under tension. The rear leg folds into an uncomfortable kneeling shape, and the forward shoe approaches or penetrates the apparent floor. During `-05` through `-07`, the returning shin and shoe pivot through a conspicuous mechanical recovery.

The pelvis lowers to `rise=−54`, while the attacking leg uses thigh `−26°`, shin `−6°`, and foot `−4°`. Those directions broadly describe a low extension; they are not an obvious sign-convention error. The weakness is the knee deformation, floor calibration, recovery, and lack of an authored sliding body action. See [kun.look:720](/home/speak/workspace/mui/showcase/fighting/fighters/kun.look.gongfu.toml:720).

In **5HK**, the defects are most exposed. In `5HK-01`, the rear arm travels overhead while the front arm drops. In `-02` through `-06`, pale sleeve flaps stand away from the shoulder/chest, the front sleeve crosses the torso as an independent object, and both arms form an awkward backward fan. The kicking leg extends, but its knee and hip retain rigid cutout boundaries. In `-07` through `-09`, the arms take another large route back toward guard while the leg retracts slowly.

Some apparent extra limbs are the gold trail, but the opaque sleeve flaps are real. The starting rear-upper-arm aim changes from `−105°` to `170°`: the current track interpolates **+275°**, sending the arm overhead. A shortest equivalent path would be `−85°`. Recovery also contains `160°→−105°` and `−170°→25°` transitions. These are identifiable interpolation/authoring failures, not missing animation frames. See [kun.look:647](/home/speak/workspace/mui/showcase/fighting/fighters/kun.look.gongfu.toml:647).

In **6MK**, `6MK-02` through `-05` show an unstable rear-leg transition; `-06` produces an extreme high-leg pose, and `-07` through `-10` turn it into a floating, seated-looking descent. The pelvis rises without a convincing coordinated launch, while the same face and shirt view remain attached. The documented “turning back kick” never acquires the torso/back view needed to read as a turn.

There are explicit full-revolution transitions: rear thigh **240°→−120°**, rear shin **262°→−170°**, then recovery **−110°→240°** and **−150°→262°**. These interpolate through −360°, −432°, +350°, and +412° respectively. Releasing and restoring IK exposes and blends these inappropriate paths. Additionally, between `t=17` and `18`, the foot aim changes from `−60°` to the inherited stance value `−8°`, a **52° change in one frame**. This is a concrete consequence of an omitted override, not renderer randomness. See [kun.look:659](/home/speak/workspace/mui/showcase/fighting/fighters/kun.look.gongfu.toml:659).

Across all six moves, the head reads somewhat large against the compressed shirt torso, the forearms look long, shoes are conspicuous, and hand silhouettes change abruptly. Collar edges and neck orientation can make the head look pasted on. I cannot substantiate a completely isolated opaque head in these samples. Likewise, not every hand that looks separate has a large literal alpha gap. The strongest proven attachment failure is the sleeve deformation.

**The implementation defect that pulls the body apart**

The basic rigid attachment math is substantially sound. The renderer subtracts the image anchor, changes image-down coordinates into world-up coordinates, aligns the anchor-to-tip axis with the bone, and applies facing to the transverse coordinate. World-space `aim` is converted into a parent-relative angle before placement. These operations appear in [present2d.cpp:177](/home/speak/workspace/mui/mgame/gongfu/present2d/src/present2d.cpp:177) and [present2d.cpp:871](/home/speak/workspace/mui/mgame/gongfu/present2d/src/present2d.cpp:871). The supplied failures do not require a global rotation-order or mirroring error.

The serious defect is the automatic skinning weight:

`w = clamp(0.5 + along_c / max(1, blend * scale), 0, 1)`

Here `along_c` is measured along the **child bone’s rest direction**, relative to its joint. That is an unsafe substitute for authored skin weights. With a bent rest elbow, the resulting division can run across the sleeve’s width and even assign shoulder vertices to the forearm. See [present2d.cpp:891](/home/speak/workspace/mui/mgame/gongfu/present2d/src/present2d.cpp:891).

Kun’s front upper arm rests at approximately `−48°` world-space; its forearm rests at `32°`. With the configured `[1,4]` grid, the computed child weights are approximately **0 on every left-edge vertex and 1 on every right-edge vertex**. The entire sleeve width becomes a shoulder-to-forearm blend. The rear upper arm is worse: its rest elbow bends about 95°, and even the analytical shoulder anchor receives full child influence.

Reconstructing the actual triangle interpolation moves the rear sleeve’s declared shoulder anchor approximately **19.8 stage units in the jab/elbow poses, 22.4 in 5HP, and 29.2 in 5HK** away from its bone joint. These are mesh-anchor displacements, not measured visible gap widths. They explain why a mathematically connected skeleton can have artwork that tears away from it.

This is a code/rigging-system defect. More grid rows alone will not repair the wrong influence field. The implementation needs explicit bind-space weights, protected attachment regions, and meshes whose vertices follow the anatomical deformation regions.

**Artwork and binding defects remain after that fix**

The art is already a collection of generated part drawings, rather than demonstrably one complete painting cut apart. Nevertheless, it has the same fundamental limitation: each fragment contains a particular painted pose, perspective, and lighting arrangement.

Inspecting [kun-parts.webp](/home/speak/workspace/mui/showcase/fighting/fighters/art/kun-parts.webp) reveals that the torso already includes substantial sleeve forms. Separate upper-arm pieces contain another complete rolled sleeve. [kun-arms.webp](/home/speak/workspace/mui/showcase/fighting/fighters/art/kun-arms.webp) adds forearms with another painted cuff/sleeve segment. Rotating these together produces duplicate cloth volumes. A connected-component cutter cannot decide which painted sleeve belongs to the torso, upper arm, or forearm.

The forearm anchor `[118,20]` sits high in that sleeve fragment, rather than being a carefully established anatomical elbow landmark. The head anchor `[38,100]` sits in the neck/collar region; the head picture includes its own shirt collar while the torso also has one. These are plausible initial placement guesses, not production-ready articulation interfaces. The cutter itself reports principal axes near component ends; those axes are geometric suggestions, not anatomical joints. See [studio/main.cpp:106](/home/speak/workspace/mui/mgame/gongfu/studio/src/main.cpp:106).

The global scale also preserves inconsistencies between separately produced pictures. Kun’s nominal front upper-arm span is **26.40 versus bone 29**; forearm **33.72 versus 38**; thigh **44.55 versus 47**; shin **40.28 versus 42**. These differences affect where corresponding landmarks meet, although painted overlap can conceal some of them.

The terminal foot is different: its painted ankle-to-tip span is **35.65**, against a 14-unit bone. Since no downstream joint depends on that endpoint, this is principally a size/contact-landmark issue, not automatically an attachment bug. Setting `fit=true` everywhere would shrink shoes and distort other intentional dimensions. Calibrate anatomy and contact landmarks explicitly.

Hand variants also lack a consistent apparent scale. Their anchor-to-tip spans range from approximately **10.12 to 20.24 stage units**; the base fist is 12.26. Fingers extending farther is legitimate, but palm breadth, wrist width, and attachment location still need normalization. The stance uses hand variant 4 and the jab switches to variant 0, producing a conspicuous silhouette change. The renderer correctly selects each variant’s own region, anchor, and tip; it does not mistakenly retain the base picture’s pivot. See [kun.look:364](/home/speak/workspace/mui/showcase/fighting/fighters/kun.look.gongfu.toml:364) and [present2d.cpp:861](/home/speak/workspace/mui/mgame/gongfu/present2d/src/present2d.cpp:861).

The head’s nominal span, **27.47 against bone 27**, is comparatively well calibrated. Its problems are collar construction, pose suitability, and overall proportion—not evidence of an enormous numerical head-scale error.

**Layering, constraints, and timing**

Parts are sorted by one fixed depth. Kun’s rear arm stays behind the torso, and the front arm stays above it. There is no keyed draw-order track. Thus, sleeves retain the same visibility relationship while crossing the chest or moving behind the body. Correct masks, overlapping joint caps, and pose-dependent order are required; sorting faithfully by unsuitable data cannot solve the overlap. See [present2d.cpp:754](/home/speak/workspace/mui/mgame/gongfu/present2d/src/present2d.cpp:754).

Angle tracks are ordinary scalar tracks. The loader transfers pose angles without unwrapping, and hanim linearly interpolates scalar values. The shortest-angle helper is used for IK adjustment, not for authored aims. This makes the 5HK and 6MK rotations predictable. Fix the offending data immediately, then introduce an explicit distinction between shortest-path angles and intentional multi-turn rotation. Silently applying shortest-path interpolation everywhere would break deliberate spins. See [look.cpp:1059](/home/speak/workspace/mui/mgame/gongfu/present2d/src/native/look.cpp:1059) and [motion.cppm:88](/home/speak/workspace/mui/modules/base/hanim/src/motion.cppm:88).

Foot planting is root-relative: the target is `fighter.pos + authored offset`. Consequently, a fully held foot travels with a moving root. Kun’s 5HP sets horizontal velocity to 6 between its step commands, while the grips also move from stance offsets to new offsets. This produces sliding rather than a planted support foot transferring weight into a step. See [present2d.cpp:239](/home/speak/workspace/mui/mgame/gongfu/present2d/src/present2d.cpp:239) and [kun.fighter:58](/home/speak/workspace/mui/showcase/fighting/fighters/kun.fighter.gongfu.toml:58).

There is an unreachable standing target too. Rear hip attachment approximately `(−5,92)` must reach `(−40,14)`: **85.49 units**, while its two bones total **83**. The solver clamps the scalar distance for the cosine calculation but computes the lower direction toward the original target. It should solve against a consistently projected reachable target and report the residual.

Floor contact is also calibrated to ankle height rather than the visible sole. At front-foot aim `−8°`, alpha inspection puts the opaque sole approximately **19.97 units below its anchor**. A target ankle height of 14 therefore places it about **5.97 units below the nominal floor**. IK success alone cannot establish visual grounding.

Kun has no configured `follow` entries and only foot grips. Therefore hand IK and secondary-motion lag are not the cause of his arm failures. Gold duplicates come from `trail={frames=[3,6],alpha=0.22}` and are intentional historical silhouettes. Applying that trail to every move nevertheless makes damaged poses harder to read.

**Hitboxes are not consistently describing the visible attack**

The sheets have no box overlay, so these findings come from the authored boxes and reconstructed landmarks:

| Move | Active simulation frames | Relationship to the drawing |
|---|---:|---|
| 5LP | 3–4 | Box ends at x=75. At clip `t=2`, wrist is around x=84.6, with the fist farther forward. The box largely describes the forearm rather than the leading fist. |
| 5MP | 5–7 | Elbow around `(49.5,117.2)` lies inside `[15,100,70,30]`. That supports the intended elbow strike. Its forward generosity needs deliberate tuning, not a hand-distance failure. |
| 5HP | 8–10 | `[10,70,70,70]` extends substantially forward of the shoulder landmark. A shoulder/contact-surface check is appropriate; a fist check would diagnose the wrong move. |
| 2MK | 7–9 | Ankle around `(89,17)` broadly agrees with `[15,0,100,25]`; the remaining problems include sole penetration and deformation. |
| 5HK | 10–12 | Ankle around `(82,137.5)` agrees broadly with `[10,110,90,50]`, but the long shoe extends beyond the ankle and requires surface-based verification. |
| 6MK | 18–19 | Ankle descends from about `(83.5,95.2)` to `(73,71.6)` while the box stays at y=90–140. On the second active frame, the flattened shoe is substantially below the designated contact region. |

Definitions are in [kun.fighter:17](/home/speak/workspace/mui/showcase/fighting/fighters/kun.fighter.gongfu.toml:17), [line 110](/home/speak/workspace/mui/showcase/fighting/fighters/kun.fighter.gongfu.toml:110), and [line 204](/home/speak/workspace/mui/showcase/fighting/fighters/kun.fighter.gongfu.toml:204).

Baked hurtboxes do not validate these attack boxes. Furthermore, `reach()` estimates image thickness from the base rectangle around bones; it does not reproduce selected swaps and deformed alpha geometry. Matching the bake hash establishes reproducibility, not visual agreement. See [present2d.cpp:367](/home/speak/workspace/mui/mgame/gongfu/present2d/src/present2d.cpp:367).

**The problem repeats across fighters, but unevenly**

Mo’s front upper-arm art span is approximately **28.88 against bone 34**, and forearm **23.89 against 28**. His art also contains large prepainted sleeve volumes. Ke has smaller discrepancies: **26.48 against 28**, and **27.86 against 29**. Li and Yue match many main limb spans much more closely. Thus, this is not one universal scale bug affecting everyone equally. See [Mo’s binding](/home/speak/workspace/mui/showcase/fighting/fighters/mo.look.gongfu.toml:448) and [Ke’s binding](/home/speak/workspace/mui/showcase/fighting/fighters/ke.look.gongfu.toml:427).

All share the same unsafe skinning machinery and fixed-depth limitation. Conversely, Ni’s 5HP uses forward arm directions, and Yue explicitly names a folded-arm `pose.elbow`. These counterexamples argue against concluding that every negative or backward aim reflects a misunderstood convention.

**What fixing it would buy**

Correct weights, calibrated joints, repaired angular paths, and grounded feet would make a large improvement. The character could become coherent, readable, and suitable for a modest cutout-style fighting game.

That would still fall well short of **Street Fighter 6, Guilty Gear Strive, or Skullgirls** in motion. Kun lacks pose-specific torso rotation, foreshortening, changing shoulder anatomy, corrective cloth shapes, convincing hand perspective, and the back view required for his turning attacks. A frozen three-quarter painting cannot supply those forms through rotation and a few triangles.

This is a ceiling of the current asset strategy, not of 2D rendering or skeletal animation generally. A hybrid production can succeed: rigs for continuity, replacement drawings for major poses, corrective meshes for joints, and specifically authored transitions. Skullgirls’ animation account describes roughly 1,300 fully hand-drawn frames per character; Strive’s creators describe handcrafted animation and deliberate control of individual parts’ appearance. Both embody extensive pose authorship. [Skullgirls animation account](https://skullgirls.com/2011/04/on-animation-in-skullgirls/), [Arc System Works developer interview](https://www.unrealengine.com/developer-interviews/how-guilty-gear--strive--hits-an-ultra-combo-with-groundbreaking-visuals-and-gameplay?lang=en-US).

The repository’s own design already recognizes this distinction: automatic weights, deformers, and keyed depth are described as additional work, not completed capabilities. See [math/fighter design:109](/home/speak/workspace/mui/.agents/docs/2026-10-29-math-fighter-tech-design.md:109).

**Fixes, in priority order, with checks**

1. **Fix skinning before polishing poses.** Replace the child-axis heuristic with explicit weights and protected shoulder/hip attachment regions. Test that designated attachment vertices stay on their joints through representative rotations. Include bent rest poses above 90°; the current straight-arm synthetic test misses this failure.

2. **Repair angular paths and inherited overrides.** Correct 5HK and 6MK, including the one-frame foot reset. Lint adjacent aims for unexplained changes above 180° and excessive angular velocity; require explicit multi-turn intent. Report resolved poses so inheritance mistakes are visible.

3. **Rebuild Kun’s articulation artwork.** Remove duplicate sleeves from the torso, separate anatomical regions consistently, repaint hidden overlap, and calibrate wrists and necks. Render per-part identification masks every frame. Measure joint-local alpha gaps, overlap width, attachment displacement, and mesh inversion. Bone endpoints alone cannot detect detached artwork.

4. **Normalize replacement drawings.** Give every hand and face anatomical landmarks and consistent wrist/palm or neck registration. Test swaps at an identical pose for attachment movement, wrist-width discontinuity, and unintended scale changes. Permit intentional foreshortening explicitly.

5. **Repair grounding and contact phases.** Add world-space planted-foot targets or deterministically derived contact anchors; project unreachable IK targets consistently. Test target reachability, support-foot world velocity, sole height, and floor penetration across every frame.

6. **Author the intended attacks as whole-body actions.** Preserve the elbow and shoulder identities; give 6MK an actual turning pose. Add keyed depth, corrective sleeves, torso/back replacements, and breakdown poses. Use pairwise occlusion assertions and silhouette diagnostics, followed by full-speed artist review.

7. **Validate attack surfaces against boxes.** Label fist, elbow, shoulder, shin, or shoe per attack. Measure distance and coverage on every active frame in both facings, with documented gameplay allowances. Recheck cancel transitions and hitstop; do not constrain every attack to its hand.

8. **Make motion review the acceptance gate.** Export complete 60-fps moves, transitions, masks, and overlays. Keep approved visual baselines only after review. Restrict trails to moves where they improve readability. Deterministic snapshots can faithfully preserve broken anatomy; passing them cannot establish animation quality.

The immediate repair is therefore both engineering and art work. The longer-term requirement is a production method that authors convincing poses, rather than expecting detailed fragments to generate them.