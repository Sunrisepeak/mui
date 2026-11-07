# Subject: fighting-presentation — the fight's craft in detail, at Street Fighter 6's level

**What:** the fighting game looked at frame by frame and joint by joint. It covers:
- how the fighters are built and bound: the rig, the parts on the bones, the hands;
- how they move and strike;
- how a hit feels (打击感);
- the effects;
- the supers: level 1 `236236P`, level 2 `214214P`, level 3 `236236K` with its cinematic;
- the UI a player meets in a fight.

Read:
- showcase/fighting/README.md and dev-story.md;
- `mgame/gongfu/present2d` (the skeleton, the parts and their binding, the renderer, effects and cinematics: src/*.cpp);
- showcase/fighting/src/hud.cpp;
- the fighters' `*.look.gongfu.toml` (parts, bones, poses), `*.fighter.gongfu.toml` (moves, frames, boxes) and `*.boxes.gongfu.toml`;
- the fighting design documents in `.agents/docs/`.

**Top 3:** Street Fighter 6 (the bar) · Guilty Gear -Strive- · Skullgirls 2nd Encore.

**The bar (the maintainer):** Street Fighter 6's quality, in every detail. The only allowance is 2D against 3D. What SF6 does with a camera moving in space and a modelled body, this game must do with 2D's means:
- for the camera: cuts, pans, pushes and rotations on painted planes;
- for the body: authored poses and parts bound exactly;
- for impact: held impact frames, drawn smears and effects, the picture's own timing.

A small team, generated art or "for a 2D game" excuses nothing beyond that.

The maintainer finds the attacks, the feel of a hit, the supers and the effects failing today. Say exactly why, frame by frame and part by part, and what each would need to stand beside the top 3.

**Detail "to the joint, even the finger" is a matter of craft, not of move design.** It means three things:
- the binding is exact: parts meet at their joints with no gap, seam, overlap or pop; a limb bends where its joint is; a wrist turns the hand it carries;
- hands take the pose each moment asks: a fist closed right for a punch, fingers spread or wrapped for a grab, a palm flat for a palm strike, relaxed in idle;
- effects are placed precisely on what they come from.

It does NOT mean attacks made with fingers. A fighter whose strikes are finger jabs or finger-themed, where a real martial artist would use a fist, palm, elbow, knee or foot, is a design fault. Name such moves, for example 墨's "two fingers to the face".

**How the top 3 do the details (compare each finding with these; correct anything you know better):**
- **Street Fighter 6.**
  - Bodies, hands and fingers: modelled bodies with fully articulated hands. Every move has its own hand shape.
  - Secondary motion: cloth, hair and muscle follow through.
  - Hitstop grows with strength; heavies hold longer, and the victim shakes while the attacker stays still.
  - Reactions differ by height (head, body, low) and strength. Knockdowns spin or crumple.
  - Sparks are layered: a white-hot core, coloured streaks along the blow's direction, particles. Drive moves throw splashes of paint in the fighter's colours.
  - Punish Counter stops the picture longer, with its own call and colour.
  - The camera pushes in on big hits and on Drive Impact.
  - Critical Arts are cinematics of several cuts. The blows land visibly on the victim's body, timed to the music, with expressions.
  - The HUD is clean and legible, with motion on every change.
- **Guilty Gear -Strive-.**
  - Animation and effects: limited animation (held key poses, few in-betweens) with smears and impact frames. Effects look hand-drawn.
  - Counter hit: slow motion, a large coloured COUNTER, extra hitstop.
  - Big moments: wall break is a cinematic of its own, and Overdrives are staged with camera moves and full-screen type.
  - Poses: every pose is a strong silhouette.
- **Skullgirls 2nd Encore.**
  - Animation: hand-drawn at a high frame count, with thousands of frames per fighter, squash and stretch, smears and overlapping action.
  - Hits and effects: hit sparks are drawn per fighter, and the line art is consistent.
  - Readability: every move reads from its silhouette alone, and contact is drawn where it happens.

**Dimensions:**
- `rig_binding`:
  - parts bound to the skeleton: no gaps, seams, overlaps or pops at the neck, shoulders, elbows, wrists, hips, knees or ankles in any pose;
  - limbs bend where joints are, and feet stay planted without sliding;
  - props are held by the hand that holds them;
  - checked across idle, walk, every normal, a special, a throw, a knockdown.
- `hands` — the hands' poses per moment (fist, palm, grab, relaxed, held prop): drawn and bound precisely, fingers placed by the pose, consistent across the six fighters.
- `attack_design`:
  - every normal and special reads as a real martial strike with the limb that strikes (fist, palm, elbow, knee, foot, shoulder);
  - the poke reads as a poke, the anti-air as an anti-air, the low as a low;
  - the hitbox matches the limb drawn;
  - finger-themed strikes are faults.
- `pose_and_spacing` — key poses (anticipation, contact, follow-through, recovery), line of action, spacing and timing between frames, smears, overlapping and secondary motion (hair, cloth, props).
- `hit_feel` — 打击感 as a player feels it:
  - hitstop by strength;
  - shake;
  - the victim's reaction pose by height and strength;
  - pushback;
  - sparks and sound on the same frame;
  - the difference between a whiffed, a blocked and a landed blow.
- `hit_tiers` — light, medium, heavy, counter, punish counter, block, clash, throw: each told apart at a glance.
- `hit_contact` — where the spark lands against where the limbs meet, effects never hiding the contact, readability on every stage.
- `vfx_craft` — effects as craft: designed silhouettes, timing and decay curves, layering of core, glow, streaks and debris, colour and value control, consistent style across the six fighters.
- `super_staging` — the cinematic as direction:
  - shot list, framing and safe areas (no actor cut by a panel);
  - camera grammar on 2D planes;
  - cut rhythm, holds and the climax;
  - the return to the fight;
  - judged per fighter.
- `super_acting` — inside the cinematic: poses, expressions, anticipation, blows landing on a body, the victim's reaction, each fighter's character.
- `knockdown_ko` — knockdowns, bounces, the K.O.'s slow-down, freeze, camera and call.
- `fight_ui` — health, meters, timer, combo and counter calls, round and K.O. type, pause and results cards: hierarchy, motion, type, readability in action.

Score strictly: 90 is "stands beside SF6 in this detail", 70 is "a competent prototype". Every score names the frames and parts it rests on.

**Run:**
- `omui-fight --training --fight mo ni --quiet --size 1920x1080` (openmui.showcase.fighting)
- `omui-fight --fight kun yue --quiet --size 1920x1080 --agent-name org.openmui.fighting.versus` (openmui.showcase.fighting)

**Use:**

The two programs:
- **The first is training.** Its stage actions:
  - `fighters {player1, player2}` chooses who fights;
  - `fill` fills both meters;
  - `press {player, pad}` holds a numpad pad for a player, and `release` gives it back;
  - `restart`.
- **The second is a versus match.** Esc pauses it under a card.

How to look: pause the clock (ui_clock) and step it to read every frame you judge. Capture the key frames: anticipation, the first active frame, contact, the hitstop's peak, the reaction, the recovery. The windows are 1920×1080 (the 960×540 picture at twice its size); look closer at joints and hands with ui_capture's `region`.

1. **Binding and hands.** Do this for each of the six fighters (mo, ni, kun, li, yue, ke):
   - step through idle, a walk, `5LP`, `5HP`, `2MK`, `6HP` or its forward heavy, a jump attack and a throw;
   - at each key frame, look at every joint and both hands.
2. **Attack design.** For each fighter, list its normals and specials (its fighter file) beside what the picture shows; name moves that read weak, wrong or finger-themed.
3. **Hits.** Land each of these, step it through its hitstop and capture it:
   - a light, a medium and a heavy normal;
   - a counter and a punish counter (hit during the other's startup and recovery);
   - a blocked heavy;
   - a clash (two heavies at once);
   - a throw, a knockdown, a K.O.
4. **Supers.** Each fighter's level 3 (`236236K`): `fill`, then the motion pressed frame by frame; step through the whole cinematic and capture its shots. Do the same for level 1 and level 2 of at least three fighters.
5. **The UI.** In the versus match: a combo's count and damage, a counter's call, a round's end, the K.O., the results card. Pause and read its card.
6. **For each finding:**
   - the frame (the stage's `frame`) and the part;
   - the capture;
   - what SF6, Strive or Skullgirls does in the same moment;
   - what this needs, concretely enough to make.

End the report with a list of every move, every super and every hit tier, each with its verdict (stands beside / behind / failing) and its single most important fix.

**Images:** the README's level 3 frames, test pictures of the pause and results cards.
