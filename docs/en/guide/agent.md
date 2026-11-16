# Agents

Every omui application can be understood and operated by an agent as it is: the agent reads the semantic tree and performs the nodes' actions — the ones a person uses. No recognising of screenshots, and no second interface for you to write.

## The command line

```sh
omui agent ls                                   # which applications are running
omui agent snapshot <app> --interactive         # what can be operated: role, name, value, states, actions
omui agent find <app> '{"role":"button","name":"Save*"}'
omui agent act <app> <node> <action> [--args JSON]
omui agent wait <app> --settled                 # until the interface is at rest
omui agent capture <app> --out shot.png         # a picture when one is needed; --format svg for exact vectors
omui agent undo <app>
```

Output is one JSON object a line; the exit code is 0 done, 1 failed, 2 a person must answer, 3 not ready.

## Connecting Claude Code and other agents

```sh
omui mcp            # an MCP server: read the tree, find, act, wait, capture
```

Registered as an agent's MCP server, it lets the agent drive your running application. The application changes no code for it.

## Who may do what

- Actions are of three kinds: **read**, **reversible**, and **irreversible or external** (delete, send, pay). The third asks a person by default: the command returns 2, and runs when called again with `--answer`.
- An agent cannot claim to be the person, nor approve its own request.
- Permission is a person's, written in `omui.config.ts`; `omui new` writes this one (everything in development; in a release, a person is asked before what cannot be undone):

<!-- code: modules/projects/omui/sdk/templates/app-ts/omui.config.ts.in -->
```ts
// How far agents may go and what the recorder keeps, in development (`omui
// dev`) and in a release (`omui run`). These are the defaults; a switch given
// at launch (--agent-mode) wins over them.
//   agent:  development — every control a developer's own tools have; whether
//           to ask you first is the agent's judgement and your instructions
//           to it. dual-user — it asks you before what cannot be undone.
//           automatic — what the build profile says.
//   record: off, errors, actions, interactions, full (what input did: `ui.explain`).
//   designer: whether Live Design is linked; a release leaves it out at compile time.
//   generation: where interface generated while it runs may go (a view's slots) and how
//           open it is — e.g. { surfaces: { extras: "cards an agent adds" },
//           open: { release: "data" } }; without surfaces, none.
import { defineConfig } from "omui/define";

export default defineConfig({
  agent: { development: "development", release: "dual-user" },
  record: { development: "full", release: "errors" },
  designer: { development: true, release: false },
});
```

In development (`omui dev`) an agent has every control, and whether it asks you first is its judgement and your instructions to it. In a release its range is what the developer allows and the user has set, both.

## Asking what happened

The framework records each input and what came of it; an agent asks it directly, without your telling:

```sh
omui agent timeline <app>                             # what input did: what it hit, what stopped it, the actions it led to
omui agent explain <app> --cause 12                   # one press, from press to release
omui agent drew <app> '[120,48]'                      # which node drew this pixel, and where in the source it was built
```

Reading a person's record needs their consent in a release.

## An agent designing the interface

While `omui dev` runs, an agent can change the running interface: it sends TypeScript or structured edits, they show within a frame, and the source is the one truth. You can take over at any moment; every step is in a history that can be committed, branched and gone back in. A whole session is [example 13](../../../examples/13-live-design).

## When something goes wrong

- **An action is refused**: every refusal carries its reason and a way on — the action a node does take, the range a value must keep, or the permission the session lacks. In development mode (`omui dev`) a trusted session has every declared capability; outside it, what an agent may do is what the developer allows and the user's configuration keeps (`omui.config.ts`, `--agent-mode`).
- **What happened, and why**: `ui.timeline` lists the interactions recorded and `ui.explain` the chain from an input to its effect; ask them rather than a person's account.
- **Nothing to connect to**: the application must be running; `omui agent ls` lists the live endpoints.
