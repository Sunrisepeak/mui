# xengine

An agent-native game engine system in which people and agents develop and play as equals (internal code name).

It is two halves with omui between them (D-161), each in a directory of its own, so that the projects have no cycle and can part:

- [kernel](kernel) — the engine kernels **below** the interface: simulation (dynamics, cellular; matter next) and media (audio, video). hanim and omui may use them; they depend on `mlibs` and [xrender](../xrender), never on the game layer or omui. Domain subsystems that have not graduated into projects of their own live here (convergence plan §4.3).
- [game](game) — the game layer **above** the interface: a game is the engine's (its world, steps, rules and scenes) and runs without omui, headless; only [ui](game/ui), the bridge, knows omui (D-162).

**Signature features — candidates** (plan §9.5; none is claimed outside the repository before its evidence exists):

| Feature | Kind | Evidence needed |
|---|---|---|
| Agents develop as people do: edit, build, play and review without a GUI | for what comes next | one whole development task done headless |
| Agents play as people do: the same actions, declared fairness tiers, authorised through openas | for what comes next | the first agent player's acceptance (plan §8.4, the widget breakout); the openas extension in the standard |
| Deterministic simulation, replay and branching by default | leading | replays byte-identical across platforms; against mainstream engines' replay and rollback |

| Half | Subsystem | Layer | |
|---|---|---|---|
| kernel | [dynamics](kernel/dynamics) | base | Every solved simulation (D-133): collision and queries (swept circles against boxes, half-planes, one-way ledges), XPBD soft bodies, physical materials; on [mlibs.simulation](../../base/simulation)'s timeline; rigid bodies next |
| kernel | [cellular](kernel/cellular) | base (its device parts gfx) | Integer field rules, with an optional compute kernel on `mlibs.rhi` ([rhi](kernel/cellular/rhi)), a field on screen as one image ([present](kernel/cellular/present)), its suite on a native device ([sdl](kernel/cellular/sdl)) |
| kernel | [audio](kernel/audio) | media | Sounds and music as data (`*.sound.omui.ts`, `*.music.omui.ts`), voices mixed into buses a block at a time with nothing allocated; offline renders the same bits everywhere; the `omui-music` tool |
| kernel | [video](kernel/video) | media | Frames and a sound track encoded on exact frame and sample counts: sources, encoders as plugins, `*.video.omui.ts`; the same bytes everywhere |
| game | [world](game/world) | game | Entities and components in fixed columns of plain data: deterministic order, a snapshot is one copy |
| game | [input](game/input) | game | Keys, gamepads and the pointer bound to a game's actions, sampled once a frame as plain data |
| game | [net](game/net) | game | Rollback netcode for two peers, independent of the transport: input delay, prediction, rollback, desync detection |
| game | [ui](game/ui) | game | The bridge: a game inside omui — its steps paced on the runtime's clock, omui's keys and pointer given to its actions |
| game | [tool](game/tool) | game | The `xengine` command: timelines checked, read at a moment, edited and edits taken back, headless (xengine's operations' catalog and skill) |

The other engines of the system are projects of their own: [hanim](../hanim) (animation, and the timeline every editor shares: [hanim.timeline](../hanim/timeline)), [xrender](../xrender) (rendering) and [gongfu](../gongfu) (fighting games, built on the game layer's ideas but with a core that needs only the standard library). Convergence moves the timeline's animation tracks and audio gain automation onto hanim's channels (plan §6.5).
