# meng.runtime

A game running on its own (D-162). A game is the engine's — its state, its fixed steps, its rules — and runs without omui: a server, a thousand playtests, CI. omui shows and plays the same game through [meng.ui](../ui), the bridge; nothing here knows it (lint `closure`).

```cpp
import meng.runtime;

struct Play {                                    // what a game gives (Playable)
    using State = …; using Input = …;            // mlibs.simulation's Model: plain data, copied whole
    static void step(State&, const Input&);
    static constexpr std::int32_t steps_per_second = 120;
    static std::uint64_t checksum(const State&);            // the same play, the same number
    static std::string   observe(const State&, bool full);  // what a player is shown, as JSON
    static void          play(Input&, const meng::Actions&);   // an agent player's actions into a step's input
    static bool          over(const State&);
};

meng::runtime::Runtime<Play> game(initial);   // human tier, stepped by the agent that plays it
mlibs::agent::Endpoint endpoint({.name = "breakout", .interface_name = "meng.world@0.1", .session = [&] {
    return std::make_unique<mlibs::agent::SubjectSession>(std::vector<mlibs::agent::Subject*>{&game}); }});
endpoint.start();
while (!game.over()) endpoint.pump();            // or: game.step(input); endpoint.pump(); with Stepping::owner
```

## Agent surface

The game is a `Subject` of the agent kernel ([mlibs.agent](../../../../base/agent)); every operation is decided there first (openas). Its operations are a catalog, [world.ops.xdef.ts](world.ops.xdef.ts) (the table `src/catalog.cppm` and the listings `src/world.ops.json`, `.md` are `ops-gen`'s); how to use them, in order, is [skill.omui.md](skill.omui.md).

| | |
|---|---|
| Subject and addresses | `world`, interface `meng.world@0.1`. Steps are numbered from 0; `frame` is how many were played |
| State | The game's `State`: plain data on `mlibs.simulation`'s timeline, copied whole; `world.state` gives the frame, the hash (`checksum`), whether it is over, and the frames it can be sought within |
| Observation | `world.observe`: what a player on the runtime's tier is shown — the game's own projection (`observe(state, full)`), with the frame and the hash. The `human` tier is shown what a person is; a tier with `full_view` also what is hidden |
| Control | `world.act`: the same actions a person has (`buttons` held, `axis`, `pointer`), decided on a frame `seen`; the state shows them from `effective` — no sooner than the tier's reaction after `seen`, no more often than the tier allows — and they hold, like keys, until the next |
| Time | `world.step` (where the agent steps the game; an owner's clock refuses it), `world.seek` back within the history kept (what is played from there is a new branch); with `watch` (a place in the view, a JSON pointer: `/cat/y`) and `every`, the answer's `watched` is that place read as the steps went by — how a jump rises and lands, in one request |
| Preview | `world.branch`: what would be `steps` ahead if given actions were given now — the future's view and hash; the game itself untouched (a copy of the state is played, on a copy of the player's lane: the actions take effect after the tier's reaction and what is held until then is held, so the preview is the future `world.act` and `world.step` play; an action the tier would refuse as too often is refused here too). `then` gives what follows — actions each `after` so many steps of the branch — so a press and its release, or a run and then a jump, are tried as they would be done; `watch` and `every` read a place of that future's view as its steps go by, as `world.step` reads the game's |
| Causes | Not the game's own: `capabilities.causes` is false. Why a cell of its world is what it is, is a kernel's subject put beside the game in the same session (a world of matter: `meng.cellular`'s `matter` on the world's lattice), which follows the game's time through `Runtime::tell` — told after each step and after each seek back; a branch run ahead tells no one |
| History | Not here: `capabilities.history` is false (commits and branches are the kernel's, when a design adapter needs them) |
| What the game says of itself | Beside `Playable`, as static data: `controls` (a name and a summary each: a button, an axis pushed one way, or the pointer) and `tasks` (moves: controls held for so many steps). `world.state` lists the controls, each with the members of the `world.act` that holds it; `find` finds one by what is wanted; `describe` lists the game's tasks before the runtime's, as requests to send as they are. What does not hold together does not compile (`told_well`). A game that says nothing is played by numbers: `buttons: [0]` |
| A program over lines | The `lines` feature (module `meng.runtime.lines`): `play_over_lines<Game>(argc, argv, {.program = {.name, .about}, .begin = …})` is a game's whole program — `<name> run [--session FILE] [--tier human\|assist] [--actor ACTOR] [--seed N]`, a JSON request a line on standard input, the records on standard output; the session's file is the game (`mlibs.agent`'s `converse`). Kiln's and Swarm's programs are this call. A feature because a command line and standard input are the native systems', and the runtime also builds for the Web |
| Budgets and tiers | `Options::tier` (reaction, actions a second, full view), `most_steps` a request, `keep` steps of history |

The same requests at the same frames give the same bits: an agent's actions are recorded in the timeline as a person's are. The agent contract's suite ([mlibs.agent.cts](../../../../base/agent/cts)) runs on it in `tests/runtime_test.cpp`: what it has passes; causes, the source of what it puts out and a budget are reported unsupported.

```sh
mcpp test -p meng.runtime      # a small chase of its own: tiers, branch, seek, the same bits, an endpoint
```

## Rollback netcode (feature `net`, experimental)

Rollback netcode is the feature `net` (`src/net/`, module `meng.net`; experimental, while the rest of the runtime is preview). Rollback netcode for two peers, independent of the transport (D-85). Each peer runs the whole game and sends only its inputs; the other's input for a frame not yet received is predicted (its last known), and when the real one differs the session restores the state saved at that frame and plays forward again.

```cpp
meng::net::Session session(game, local_player, {.delay = 2, .max_rollback = 8});
transport.send(meng::net::encode(session.local(input)));        // this frame's input, for frame + delay
for (auto& bytes : transport.received())
    if (auto m = meng::net::decode(bytes)) session.receive(*m);
session.advance();                                               // advanced, or waiting for the peer
```

- **A game** is anything that plays one frame from both players' inputs, saves and restores its state by a copy, and checksums a saved state (`meng::net::Game`); gongfu's match is one.
- **Input delay** (`delay`) plays a local input that many frames after it is given: latency within it needs no rollback.
- **Rollback** reaches back at most `max_rollback` frames; a peer that far past the other's last input waits instead.
- **Messages** carry every local input the peer has not acknowledged — a lost one is sent again with the next — the acknowledgement of the peer's, the sender's frame (`ahead()`: what time sync evens out) and, every `checksum_every` confirmed frames, a checksum: `desync()` names the first frame the peers disagree on. `encode` / `decode` make them little-endian bytes for any transport (UDP, WebRTC).

`tests/net_test.cpp` (`mcpp test -p meng.runtime net_test`) plays gongfu's match on two peers through simulated links — perfect; latency inside the delay; 4, 6 ± 3 and 8 ± 4 frames with 0, 5 and 15 % loss; a peer 20 frames behind — and holds both to the bytes of an offline run of the same inputs, with no rollback where the delay covers the latency and rollbacks where it does not; a game altered on one peer is named at the frame they parted.

### Experimental (release 0.1, D-199)

- **Target**: rollback netcode any deterministic game uses as is, over any transport.
- **Now**: the session (prediction, restore and replay) with its tests; no transport.
- **Gap**: no transport and no match between two machines; gongfu its one intended consumer.
- **Graduation to preview**: a transport, and two processes playing a whole match to the same checksum.
- **Agent surface**: owed — a match's rollbacks as branches, a desync located to its step and channel (M6.2, §3.4).
