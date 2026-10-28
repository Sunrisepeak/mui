# 格斗游戏 — the fighting game

`omui-fight` is gongfu's match (`mgame/gongfu`) on openmui's stage: two fighters at 60 frames a second, from the keyboard or an agent, drawn in 2D by `gongfu.present2d`, with a training mode that shows every box and a frame meter (D-85 §8.7). Its name will come later. It has two fighters so far, chosen by the stage's `left` and `right` properties:
- Kata, the training fighter;
- 墨 (Mo), the calligrapher. She is the first of the five designed in the [characters design](../../.agents/docs/2026-10-28-gongfu-characters-design.md): keep-away with written glyphs at three speeds, a long brush and an anti-air.

```sh
mcpp run -p openmui.showcase.fighting
mcpp run -p openmui.showcase.fighting -- --training
mcpp run -p openmui.showcase.fighting -- --headless --png fight.png
```

| | Directions | Punches (L M H) | Kicks (L M H) |
|---|---|---|---|
| Player 1 | W A S D | U I O | J K L |
| Player 2 | ← ↑ → ↓ | Insert Home PageUp | Delete End PageDown |

- **Fighters are files** built into the program, in the formats a mod will use:
  - `fighters/<name>.fighter.gongfu.toml` is what the fighter does (`gongfu.data`);
  - `fighters/<name>.look.gongfu.toml` is how it looks and how its hits feel (`gongfu.present2d`): palettes for the two sides, bones, parts, clips and impact profiles.
  - `fighters/<name>.boxes.gongfu.toml` holds its hurtboxes, baked from the look's animation by `gongfu-studio bake`. The build (`gongfu.rules`) refuses a stale one, and training draws them: a jab's arm can be hit.

  A file that does not read appears in the stage's semantic value with the problem's line and column; the program doesn't crash.
- **The match runs on the runtime's clock.** A timer plays every frame that is due: a late frame catches up, and a stall of more than four frames is skipped, not replayed. Headless, a second is exactly 60 frames. Pausing is a property. While paused, `step` plays one frame.
- **An agent plays through the stage's actions:**
  - `press` holds a pad written in numpad notation (`6`, `2LK`, `5LP+LK`) until the next press; `release` gives the keyboard back.
  - The stage's semantic value is the match as an agent follows it: the frame, the seconds left, and each fighter's name, health, combo, position and what it is doing (`idle`, `walk_forward`, `5LP`, `hitstun`, …).
- **Training** (`mode = "training"`) adds, from `gongfu.training`:
  - the push, hurt, throw and active hit boxes of the current frame, lined up with the picture's camera (shake included);
  - the frame meter: the last 80 frames of each player, hitstop not counted;
  - each player's input history, with arrows, the buttons' colours and the frames each pad was held;
  - the readings under the timer: player 1's startup, the frame advantage and the combo's hits and damage;
  - actions to save and load a state, and to record a player and replay the recording looped (the dummy).

  Agents read the same values in the semantic value's `training`.
- **The HUD shapes almost no text.** The health bars drain from the middle toward each player's side, and numbers are seven-segment bars. The few words are shaped once and kept.

`tests/fighting_test.cpp` covers:
- the roster read;
- 60 frames a second on the clock, exactly;
- the keyboard walking both players;
- pause and step;
- an agent walking in and landing a jab, which the semantic value reports;
- training's readings as an agent reads them (a blocked jab is +3, a jab cancelled into a fireball is a combo of 900);
- a saved state coming back, and the dummy replaying a recorded walk exactly;
- the versus and training frames at the jab's active frame, with pinned pixels;
- 墨 as designed: every normal's, glyph's and anti-air's startup and its advantage on hit and on block, measured in the simulation against the design's table.
