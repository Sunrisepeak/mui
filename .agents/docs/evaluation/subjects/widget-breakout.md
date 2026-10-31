# Subject: widget-breakout — 《组件打砖块：闹鬼的设置》

**What:** `showcase/widget-breakout` and its simulation `showcase/widget-breakout/sim`: a breakout game where the haunted Settings panel's controls are the mechanics (charge a slider into a beam, switch a shield, bank chains to buy a rewind), five rooms, an inspector on Tab. Read its README, dev-story.md, the mgame/S4 design document, src/, sim/, tests/.

**Top 3:** Peggle Deluxe (juice, feedback, reward) · Baba Is You (rules as mechanics, concept clarity) · Breakout: Recharged (a modern breakout's feel and presentation). **Reference:** There Is No Game (the interface as the game).

**Dimensions:**
- `concept_clarity` — the premise understood without reading; every control a mechanic that matters.
- `fun_feel` — moment-to-moment control, ball and paddle feel, risk and reward.
- `level_design` — the five rooms, progression, variety, difficulty curve.
- `juice` — hit feedback, particles, shake, sound sync, reward moments.
- `art_direction` — one visual system across rooms, materials, readability.
- `ui_typography` — HUD, labels, menus, choices, type and layout detail.
- `onboarding_readability` — first minute, teaching through play, state always readable (dark mode, x-ray).
- `sound_music` — effects, music, sync.
- `framework_showcase` — components, canvas, time/rewind, agents, scripts woven into play; the inspector's explanations.
- `code_tests_docs` — code, tests, README, dev story, signature shot.

**Run:**
- `omui-breakout --quiet` (openmui.showcase.widget_breakout)

**Use:** the pointer or ← → (A D) move the paddle, Space or a click launches, R held rewinds, X scans, Z held shows a tall room whole, 1–4 direct the copilot or choose a contraption between rooms, Tab opens the inspector, Enter goes on.
1. The first minute as a newcomer: is it clear what to do, what the panel's controls are for, how to win the room.
2. Play room 1 to the end with the pointer and with the keys: aim, launch, the slider's beam, the shield switch, chains banked, a rewind.
3. Each room you can reach: what is new, whether it reads.
4. The inspector (Tab): what it shows, whether its controls answer.
5. Losing a ball, losing the run, and going on.

**Images:** each room, the inspector, the run choice, the README frames.
