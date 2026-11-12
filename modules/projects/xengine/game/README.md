# xengine/game

The game layer, above the interface (D-161, D-162): a game is the engine's — its world, its fixed steps, its rules and its scenes — and runs without omui, headless; every pixel is xrender's; windows, input devices and the interface are omui's. Of the packages here only [ui](ui), the bridge, knows omui.

| Package | |
|---|---|
| [runtime](runtime) | A game on its own: its steps on a timeline, an agent player's lane on its tier, and the game as a subject of the agent kernel (observe, act, step, seek, branch) — no interface needed: a server, a thousand playtests, CI |
| [world](world) | Entities and components in fixed columns of plain data: deterministic order, a snapshot is one copy |
| [input](input) | Keys, gamepads and the pointer bound to a game's actions, sampled once a step as plain data; an agent's actions held like keys (`PlayTier`, `AgentLane`) |
| [net](net) | Rollback netcode for two peers, independent of the transport: input delay, prediction, rollback, desync detection |
| [ui](ui) | The bridge: a game shown and played in omui — its steps paced on the runtime's clock, omui's keys and pointer given to its actions |
| [tool](tool) | The `xengine` command, headless (xengine's operations' catalog and skill) |
