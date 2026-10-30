# Subject: game-engine — mgame and gongfu as an engine

**What:** `mgame/` (world, simulation, input, physics, net, time, audio use) and `mgame/gongfu` (the deterministic rollback fighting framework: core, data, rules, ai, training, present2d, studio), with openmui as the UI. Read their READMEs, the structure-render-web-game design §mgame/gongfu, the fighting design documents, and the two games built on them (showcase/fighting, showcase/widget-breakout).

**Top 3:** Unreal Engine 5 · Unity 6 · Godot 4. **Reference:** Bevy (code-first ECS); GGPO and rollback frameworks (for gongfu's netcode and determinism); Skullgirls' Z-Engine (2D fighting).

**Dimensions:**
- `scope_architecture` — what the engine is for, its layering, ECS or world model, scheduling; honest scope vs a general engine.
- `simulation_determinism` — fixed-point or deterministic math, rollback, replays, checksums; vs GGPO-based engines.
- `rendering_presentation` — 2D presentation, skeletal/mesh, VFX, cameras, cinematics; vs the top 3's 2D toolsets.
- `physics_collision` — collision, physics, hit/hurt boxes.
- `input_net` — input devices, buffering, netcode, serialization robustness.
- `audio` — mixing, synthesis, voices, music systems.
- `content_pipeline_tools` — assets, studio tools, editors, baking, agent-driven tooling; vs the editors of the top 3.
- `ui_integration` — openmui as the game's UI (HUD, menus) vs UMG / UI Toolkit / Godot Control.
- `performance` — frame budgets, allocation, scaling.
- `code_quality_tests` — code, tests, hostile-input handling.

**Images:** attach fighting and breakout frames.
