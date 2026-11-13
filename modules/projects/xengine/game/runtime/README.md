# xengine.runtime

A game running on its own (D-162). A game is the engine's — its state, its fixed steps, its rules — and runs without omui: a server, a thousand playtests, CI. omui shows and plays the same game through [xengine.ui](../ui), the bridge; nothing here knows it (lint `closure`).

```cpp
import xengine.runtime;

struct Play {                                    // what a game gives (Playable)
    using State = …; using Input = …;            // mlibs.simulation's Model: plain data, copied whole
    static void step(State&, const Input&);
    static constexpr std::int32_t steps_per_second = 120;
    static std::uint64_t checksum(const State&);            // the same play, the same number
    static std::string   observe(const State&, bool full);  // what a player is shown, as JSON
    static void          play(Input&, const xengine::Actions&);   // an agent player's actions into a step's input
    static bool          over(const State&);
};

xengine::runtime::Runtime<Play> game(initial);   // human tier, stepped by the agent that plays it
mlibs::agent::Endpoint endpoint({.name = "breakout", .interface_name = "xengine.world@0.1", .session = [&] {
    return std::make_unique<mlibs::agent::SubjectSession>(std::vector<mlibs::agent::Subject*>{&game}); }});
endpoint.start();
while (!game.over()) endpoint.pump();            // or: game.step(input); endpoint.pump(); with Stepping::owner
```

## Agent surface

The game is a `Subject` of the agent kernel ([mlibs.agent](../../../../base/agent)); every operation is decided there first (openas). Its operations are a catalog, [world.ops.omui.ts](world.ops.omui.ts) (the table `src/catalog.cppm` and the listings `src/world.ops.json`, `.md` are `omui-gen ops`'s); how to use them, in order, is [skill.omui.md](skill.omui.md).

| | |
|---|---|
| Subject and addresses | `world`, interface `xengine.world@0.1`. Steps are numbered from 0; `frame` is how many were played |
| State | The game's `State`: plain data on `mlibs.simulation`'s timeline, copied whole; `world.state` gives the frame, the hash (`checksum`), whether it is over, and the frames it can be sought within |
| Observation | `world.observe`: what a player on the runtime's tier is shown — the game's own projection (`observe(state, full)`), with the frame and the hash. The `human` tier is shown what a person is; a tier with `full_view` also what is hidden |
| Control | `world.act`: the same actions a person has (`buttons` held, `axis`, `pointer`), decided on a frame `seen`; the state shows them from `effective` — no sooner than the tier's reaction after `seen`, no more often than the tier allows — and they hold, like keys, until the next |
| Time | `world.step` (where the agent steps the game; an owner's clock refuses it), `world.seek` back within the history kept (what is played from there is a new branch) |
| Preview | `world.branch`: what would be `steps` ahead holding given actions — the future's view and hash; the game itself untouched (a copy of the state is played) |
| Causes | Not the game's own: `capabilities.causes` is false. Why a cell of its world is what it is, is a kernel's subject put beside the game in the same session (a world of matter: `xengine.cellular`'s `matter` on the world's lattice), which follows the game's time through `Runtime::tell` — told after each step and after each seek back; a branch run ahead tells no one |
| History | Not here: `capabilities.history` is false (commits and branches are the kernel's, when a design adapter needs them) |
| Budgets and tiers | `Options::tier` (reaction, actions a second, full view), `most_steps` a request, `keep` steps of history |

The same requests at the same frames give the same bits: an agent's actions are recorded in the timeline as a person's are. The agent contract's suite ([mlibs.agent.cts](../../../../base/agent/cts)) runs on it in `tests/runtime_test.cpp`: what it has passes; causes, the source of what it puts out and a budget are reported unsupported.

```sh
mcpp test -p xengine.runtime      # a small chase of its own: tiers, branch, seek, the same bits, an endpoint
```
