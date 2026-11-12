# modules/projects

Mature projects: branded engines, frameworks and modules that are complex enough to stand as a project or repository of their own (convergence plan §9). A project may span layers — the layer of each package comes from the lint's path-to-layer table, not from this directory — and holds its own subsystems that have not graduated yet.

| Project | One sentence | |
|---|---|---|
| [omui](omui) | A UI framework in which one semantic tree drives pixels, strong motion, accessibility, agents and tests (its motion is hanim's) | `openmui.*` (ui, components, lang, sdk; `omui.*` after M1's rename) |
| [hanim](hanim) | A composable, agent-native animation engine: one set of capabilities for UI animation, video and game animation | `hanim.core` (base), `hanim` (gfx) |
| [xengine](xengine) | An agent-native game engine system in which people and agents develop and play as equals (internal code name) | `xengine.dynamics` (base), `xengine.cellular*` (base, gfx), `xengine.{audio,video,timeline}*` (media), `xengine.{world,input,physics,net,ui}` (game) |
| [gongfu](gongfu) | A deterministic, rollback-ready fighting-game framework whose characters are data | `gongfu` (the standard library only), `gongfu.{data,ai,training}`, `gongfu.{present2d,studio,rules}` |
| [xrender](xrender) | A CPU/GPU same-source, Web-first renderer whose pixels agents can review (internal code name) | `xrender.graph`, `xrender.vector`, `xrender.effect`, `xrender.raster`, `xrender.rhi.*`, `xrender.cts` (gfx) |

M1's last step renames omui's packages (`openmui.*` → `omui.*`) and removes the old names' forwarders.

**Admission** — a project here must:

1. be describable in one sentence, written as the first line of its README and used as the first sentence anywhere it is introduced;
2. have a few signature features that lead the industry or are designed for what comes next, each with its evidence: a benchmark against the best alternatives, or a real task only this design makes possible. Until the evidence exists a feature is a *candidate* and is not claimed outside the repository;
3. meet the graduation criteria of [modules](../README.md) by milestone M5 (at least two independent consumers, a frozen v1 API, ops and skill files, a CTS, documentation, benchmarks) — or go back to xengine or mlibs.

**What each project has** (plan §9.4): a README whose first line is the definition, then its signature features and their evidence, responsibilities, public entry points by level (L0–L4), its dependency closure and the state of the graduation criteria; `ops.omui.toml` and `skill.omui.md` (M4); a CTS, benchmark budgets and independent consumer tests. Projects reach each other only through declared package dependencies, never by relative source paths; inside, responsibilities are `src/<role>/` directories selected by features, and a sub-package exists only where isolation requires it (D-99).

## Agent surface

Agent-native is the architecture, not an adapter (D-173, D-174): every engine gives an agent, from its data model on, state, observation, control, time, causes, and identity and authority — through the agent kernel ([mlibs.agent](../base/agent)), the same way whatever the subject is. **A new engine, kernel package or design adapter writes this first, then its code.**

Every package here says in its manifest how an agent reaches what it holds (lint `agent-surface`):

```toml
[package.metadata.openmui]
agent = "subject"                        # it serves a subject of the agent kernel
agent = "tool"                           # a command line an agent runs, with its catalog and skill
agent = { part = "xengine.cellular" }    # what it holds is reached through that package's subject
agent = { library = "a function of the query and the candidates" }   # nothing an agent could reach, and why
```

A **subject**'s package has a catalog of its own (`<product>.ops.omui.ts`; `omui-gen ops` makes the table and the listings), a `skill.omui.md` (the procedure only), a test that runs the contract suite on it ([mlibs.agent.cts](../base/agent/cts): the eight native properties — passed, failed or unsupported, the last only from what the subject says it cannot do), and this section in its README:

```markdown
## Agent surface

| | |
|---|---|
| Subject and addresses | its name, the interface its operations speak (`name@version`), how what it holds is addressed (ids, regions, frames) |
| State | what its state is (plain data, copied whole), and what its hash covers |
| Observation | the projections made for an agent — brief first, by region or kind, within a budget — never the state dumped |
| Control | the operations that change it, each with its effect and how it is taken back |
| Time | steps, seeks, replays; who steps it |
| Preview | what would be, asked before acting, the state untouched |
| Causes | why its state is what it is, and who made each change |
| Source | from what it puts out (a pixel, a sound, a value) to what made it |
| Budgets and tiers | the limits a request may ask within; the tiers of what a player is shown and how fast it may act |
| Not here | each capability it says it has not, and why or what brings it |
```

What a subject cannot do it says (`capabilities()`), and its README says under "Not here": a capability is never pretended. [xengine.runtime](xengine/game/runtime) is the first written this way.
