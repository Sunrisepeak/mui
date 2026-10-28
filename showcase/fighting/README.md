# 格斗游戏 — the fighting game

`omui-fight` is gongfu's match (`mgame/gongfu`) on openmui's stage: two fighters at 60 frames a second, from the keyboard or an agent, drawn in 2D by `gongfu.present2d`, with a training mode that shows every box and a frame meter (D-85 §8.7). Its name will come later. For now it has one fighter: the training fighter, Kata.

```sh
mcpp run -p openmui.showcase.fighting
mcpp run -p openmui.showcase.fighting -- --training
mcpp run -p openmui.showcase.fighting -- --headless --png fight.png
```

| | Directions | Punches (L M H) | Kicks (L M H) |
|---|---|---|---|
| Player 1 | W A S D | U I O | J K L |
| Player 2 | ← ↑ → ↓ | Insert Home PageUp | Delete End PageDown |

- **Fighters are files.** `fighters/*.fighter.gongfu.toml` (the format of `gongfu.data`, the one a mod will use) is built into the program. A file that does not read appears in the stage's semantic value with the problem's line and column; the program doesn't crash.
- **The match runs on the runtime's clock.** A timer plays every frame that is due: a late frame catches up, and a stall of more than four frames is skipped, not replayed. Headless, a second is exactly 60 frames. Pausing is a property. While paused, `step` plays one frame.
- **An agent plays through the stage's actions:**
  - `press` holds a pad written in numpad notation (`6`, `2LK`, `5LP+LK`) until the next press; `release` gives the keyboard back.
  - The stage's semantic value is the match as an agent follows it: the frame, the seconds left, and each fighter's name, health, combo, position and what it is doing (`idle`, `walk_forward`, `5LP`, `hitstun`, …).
- **Training** (`mode = "training"`) adds:
  - the push, hurt and active hit boxes of the current frame, lined up with the picture's camera (shake included);
  - the last 60 frames of each player: startup, active, recovery, stun or free.
- **The HUD needs no font.** The health bars drain from the middle toward each player's side, and the timer is two seven-segment digits.

`tests/fighting_test.cpp` covers:
- the roster read;
- 60 frames a second on the clock, exactly;
- the keyboard walking both players;
- pause and step;
- an agent walking in and landing a jab, which the semantic value reports;
- the versus and training frames at the jab's active frame, with pinned pixels.
