# mgame

The game engine (internal name, D-82, D-85). It uses openmui as its interface — menus, HUDs and the training mode are openmui views — and openmui never depends on it.

Depends on: [modules](../modules) (the independent components and openmui). Its fighting framework's core depends on the standard library alone.

| Package | |
|---|---|
| [net](net) | Rollback netcode for two peers, independent of the transport: input delay, prediction, rollback, waiting, desync detection |
| [gongfu](gongfu) | The fighting framework's core: a deterministic fixed-point 60 Hz simulation with rollback, fighting-game input, hits, blocks, throws and combos, characters as data |
