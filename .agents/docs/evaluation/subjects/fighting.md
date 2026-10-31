# Subject: fighting — the fighting game

**What:** `showcase/fighting` (with `mgame/gongfu`: simulation, rules, AI, training, present2d renderer, studio): six fighters, stages, HUD and menus, training mode, sound, voice, music, the move list. Read showcase/fighting/README.md, dev-story.md, the fighting design documents in `.agents/docs/` (including the acceptance rubric, whose eleven items are the first eleven dimensions here), src/, tests/, data files. The images are test pictures and README frames.

**Top 3:** Street Fighter 6 · Guilty Gear -Strive- · Skullgirls 2nd Encore. **Reference:** Them's Fightin' Herds, Pocket Bravery (what an indie team reaches).

**Dimensions:**
- `character_design` — silhouettes, anatomy, hands to the fingers, faces, costume, materials, a style both unified and distinct per fighter.
- `animation` — key poses, line of action, weight, squash and stretch, smears, follow-through, secondary motion, idles and walks with personality.
- `hit_impact` — light/medium/heavy, block, counter, clash each with its own reaction, hitstop, shake, sparks, sound; knockdowns.
- `supers_direction` — level 3 cinematics: camera (push, pan, cut), expressions, impact frames, rhythm with music; each fits its fighter.
- `vfx` — designed shapes, layered glow, trails, grain, decay curves.
- `stages` — composition, light, palette, ambient motion, depth, contrast with the fighters.
- `hud_menus` — health, meters, timer, combo, select, versus, pause, results, training; hierarchy, motion.
- `sound_voice` — per-fighter voice sets, impacts by material and weight, mix.
- `music` — themes, layers, transitions, sync with the action.
- `feel` — 60 fps, input to photon, buffering, cancels, leniency.
- `depth` — distinct game plans, matchups, newcomer accessibility within five minutes.
- `fun_first_minutes` — the first five minutes for a newcomer: controls, opponent, discoverability.
- `framework_showcase` — rollback determinism, agent play, frame meter, the move list: shown naturally, never bolted on.
- `code_tests_docs` — code, tests, README, dev story, signature shot.

**Run:**
- `omui-fight --quiet` (openmui.showcase.fighting)
- `omui-fight --fight kun ni --cpu 1 --level 2 --quiet --agent-name org.openmui.fighting.cpu` (openmui.showcase.fighting)

**Use:** the keys are player 1 W A S D (move), U I O (punches), J K L (kicks); player 2 the arrows, Insert Home PageUp, Delete End PageDown (hold keys with `hold`, in frames; motions as successive keys; buttons together with `keys`; ui_clock pause and step to read frames). Esc (a pad's start) pauses under a card: its items answer the keys, a pad and the pointer.
1. Title to fight with the keys and with the pointer: every screen's controls answer, and say how.
2. Play several fighters, one round each at least: walk, jump, guard, normals, specials by motion, a throw, a super when the meter allows; read the HUD as you go.
3. Training mode: frame data, its tools from the pause card (save and load a state, record player 2 and replay it, fill the meters, the overlays), the move list and a move's demonstration, reset.
4. Watch an exchange frame by frame (ui_clock): anticipation, impact, hit-stop, sparks, reaction, camera.
5. A whole match to its K.O., its results; a rematch, and back to the select screen. Pause a fight and leave it.
6. As an agent: what the semantic tree says of the fight (frame, health, what each is doing, move phases, whether each can act) against what the picture shows.
7. Against the computer (`org.openmui.fighting.cpu`, you are player 1): play a round with the keys; judge how it plays.
8. Read the last duel between two agents (`.agents/docs/evaluation/reports/*-fighting-duel-*.md`): what two players found playing by turns.

**Images:** title, select, versus, training, each stage, clash, KO, each fighter's level 3 frames, README animation frames.
