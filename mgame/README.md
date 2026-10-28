# mgame

The game engine (internal name, D-82, D-85). It uses openmui as its interface — menus, HUDs and the training mode are openmui views — and openmui never depends on it.

Depends on: [modules](../modules) (the independent components and openmui). Its fighting framework's core depends on the standard library alone.

| Package | |
|---|---|
| [world](world) | Entities and components in fixed columns of plain data: deterministic order, a snapshot is one copy |
| [sim](sim) | A fixed-step clock, and a timeline of snapshots and inputs that reaches any recent frame exactly (rewind, rollback) |
| [input](input) | Keys, gamepads and the pointer bound to a game's actions, sampled once a frame as plain data |
| [physics](physics) | Swept circles against boxes: exact times of impact, the same bits everywhere |
| [ui](ui) | A game inside openmui: its steps paced on the runtime's clock, openmui's keys and pointer given to its actions |
| [net](net) | Rollback netcode for two peers, independent of the transport: input delay, prediction, rollback, waiting, desync detection |
| [gongfu](gongfu) | The fighting framework's core: a deterministic fixed-point 60 Hz simulation with rollback, fighting-game input, hits, blocks, throws and combos, characters as data |
