# Dev story — Swarm

How Swarm was made: by one agent session from one prompt, with no person in between. It is the record of the plan's T2.8 ([the plan](../../.agents/docs/2026-11-12-foundation-omui-xengine-emergent-world-design.md), M6.2: "a new agent session makes a 2D top-down shooter from scratch with one prompt: the prompt, the time, the interventions").

## 1. The experiment

- **Who**: a new session of a coding agent (`claude -p`), started in the repository with nothing from any earlier session. Allowed: reading files, writing files, `tools/dev/capped.sh` (building and testing), the lint, `ls`. Not allowed: git, anything else.
- **When**: 2026-11-14.
- **The prompt**, whole:

> You are a developer new to this repository. Read AGENTS.md first, then whatever READMEs and module interfaces you need.
>
> Task: from scratch, make a small 2D top-down shooter as a new showcase member under showcase/ (pick the directory and package names by the repository's rules), using only the public capabilities of the engine (xengine.runtime, xengine.input, xengine.world or whatever fits; mlibs for math, hash and random).
>
> The game: the player moves with two axes, aims with the pointer and shoots with a button; enemies arrive in waves and chase the player; a bullet that hits an enemy removes it and scores; an enemy that touches the player costs a life; the game ends when lives run out. Keep it small, but it must really play.
>
> Requirements:
> 1. The game is a `Playable` of xengine.runtime: deterministic (the same inputs give the same checksum), with what a player is shown as JSON (and a legend for anything that is not self-evident), and it says its own `controls` and `tasks` (see how showcase/kiln/headless does).
> 2. A program that plays it over lines, like showcase/kiln/play (`<name>-play run --session FILE`).
> 3. Tests in the member: the same inputs give the same checksum on two runs; a seek back and replay gives the same bits; an enemy is shot and the score rises; the game can be lost; each of the game's own tasks, sent as written through a session, does what it says; the agent contract suite (mlibs.agent.cts) passes on the game as the runtime's own test runs it.
> 4. A short README for the member (its first line says what it is), listed where the repository's lint expects it. Run `python3 tools/lint/lint.py` and fix what it reports about your member.
>
> Rules: build and test only your member, always through `tools/dev/capped.sh mcpp test -p <your package>` (or `mcpp build -p`). Do not run the whole workspace. Do not commit, do not touch git, do not modify any other member except to list yours in the root mcpp.toml and the showcase README. If the engine lacks something you need, do not copy or work around it silently: note it, and use the smallest honest substitute inside your member.
>
> When done, report: what you made (files), the test results as printed, every place you were stuck and what got you past it, which documents and interfaces you read to learn the API, and what the engine was missing.

| | |
|---|---|
| Time | 538 s (9 minutes), 55 turns |
| Interventions by a person | 0 |
| Cost | 5.14 USD (45,677 tokens written; 164,047 read fresh; 3,421,237 read from cache) |
| What it made | 841 lines in 8 files: the rules and the game (`headless`, 314 lines of source), the program (`play`, 78 lines), 343 lines of tests, the manifests and the README |
| Tests, as it ran them | `omui.showcase.swarm.headless`: 18 cases passed, 4 skipped (the four the runtime's own test pins as unsupported: the source of a pixel, who acted, a budget, causes); `omui.showcase.swarm.play`: 1 passed. The lint: 0 problems |
| Tests, run again afterwards by the session that started the experiment | the same: 18 passed and 4 skipped; 1 passed |

The files of this directory at the commit that adds it are as that session left them; this file was written afterwards from its report and its record.

## 2. How each part is built with the framework

| Layer | Package | What it is |
|---|---|---|
| Time and the agent | `xengine.runtime`, `mlibs.simulation`, `mlibs.agent` | the game as a `Playable` on a timeline; the subject `world` (observe, act, step, seek, branch); the game's own `controls` (six) and `tasks` (three) |
| What is in the arena | `xengine.world` | enemies and bullets as entities in one block of plain data: a copy is a branch |
| Input | `xengine.input` | a player's actions (axes, a button, the pointer), a person's or an agent's |
| Numbers | `mlibs.math`, `mlibs.hash` | vectors, the random stream (SplitMix64), the checksum |
| The program | `mlibs.agent`'s `converse`, `mcpplibs.cmdline`, `mcpplibs.os` | `swarm-play run --session FILE`: the session's file is the game |

What it read to learn this, by its own account: `AGENTS.md`, the showcase README, the lint's rules, the runtime's README, interface and test, the input and world interfaces, `mlibs.math`, `mlibs.hash`, the contract suite's interface, and all of Kiln's `headless` and `play` as the example the prompt pointed at.

## 3. Assets

None: the game has no picture and no sound. Nothing draws it yet.

## 4. Where it was stuck, and what that says of the framework

By its report; each is a finding about what is below the game.

| Where | What got it past | What it says |
|---|---|---|
| The lint ended in a traceback: a child dependency key (`mlibs.agent.cts`) beside its parent (`mlibs.agent`), unquoted, is not TOML | the rule in `AGENTS.md` §5 (quote the child key) | the lint should report the manifest and the line as a problem, not end |
| mcpp warned of a quoted dependency key in `play/mcpp.toml` | unquoted | the key was copied from Kiln's manifest, which has the same needless quotes |
| A raw string ended early (`)"` inside the legend's text) | a delimiter on the raw string | the language's; nothing of the framework's |
| `neq` is not a matcher of mtest | `!=` | mtest's matchers are found by trying |
| Four expectations of its own tests were wrong (a threshold of shots; a kill and a spawn in one step; the runtime's frame goes on after a game is lost while the game's state does not; `find "fire the gun"` ranks `aim` first because its summary names the gun) | the tests' own output | the third is a fact of the runtime worth saying in its README; the fourth is `find` being lexical |
| Two scripted edits were held back by its permissions, and two test runs ran against unchanged files | the edits made again with its own tool | the experiment's set-up, not the framework |

## 5. What the engine did not have

| Missing | Substitute here | Where it belongs |
|---|---|---|
| The length of a `Vec2` and a `Vec2` of length one (`Vec3` has both) | `std::sqrt(dot(v, v))` in `src/sim.cpp` | `mlibs.math` |
| A component of `xengine.world` holding floats has to vouch for its own layout (`no_padding`) | `Body` says it | `xengine.world` (a check that sees through members needs reflection) |
| Whether two circles meet | a squared distance against a squared sum of radii | `mlibs.geom`, when a second game asks |
| Why a game is over, in the runtime's answers | the view's `outcome` | the game's view is the right place; the runtime knows only `over` |
| A picture | none | a host and a display list, as Kiln's `picture` |

## 6. What changed below it afterwards

Each from the two tables above, in the mainline, in the checkpoint after the one that added this directory:

- `mlibs.math` has `length(Vec2)` and `normalized(Vec2)`, and `Vec2` says `no_padding`; `src/sim.cpp` uses the library's `length` (its own is gone).
- The lint reports a manifest that is not TOML as a problem (rule `toml`: the file, what the parser said, and the quoting rule), and goes on to nothing else.
- Kiln's manifest lost the needless quotes.
- The program is the runtime's: `xengine.runtime`'s `lines` feature (`play_over_lines`). `play/src/entry/main.cpp` was 78 lines, a copy of Kiln's with the names changed; it is one call now, as Kiln's is.
- `find` says what a control is (a button, an axis, the pointer) beside what it does.

Not changed: a circle test in `mlibs.geom` (one game asks; the substitute is one line), and components of `xengine.world` seeing through their members (needs reflection).

## 7. Self-review

- It plays: the test's own player (aims at the nearest enemy, backs away from it) clears waves; a player that does nothing loses in 612 steps.
- The difficulty is gentle: that player reaches wave 11 in a minute without losing a life, because enemies are always slower than the player.
- It is half a work: no picture, no sound, one arena. As a record of what one prompt makes in nine minutes on this engine it is whole.
