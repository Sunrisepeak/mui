# Agents

Every omui application can be understood and operated by an agent as it is: the agent reads the semantic tree and performs the nodes' actions — the ones a person uses. No recognising of screenshots, and no second interface for you to write.

## The command line

```sh
omui agent ls                                   # which applications are running
omui agent snapshot <app> --interactive         # what can be operated: role, name, value, states, actions
omui agent find <app> --role button --name "Save*"
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
- Permission is a person's, written in `omui.config.ts`:

```ts
import { defineConfig } from "omui/define";

export default defineConfig({
  agent:  { development: "development", release: "dual-user" },  // everything in development; in a release, a person is asked before what cannot be undone
  record: { development: "full", release: "errors" },            // what is recorded
});
```

In development (`omui dev`) an agent has every control, and whether it asks you first is its judgement and your instructions to it. In a release its range is what the developer allows and the user has set, both.

## Asking what happened

The framework records each input and what came of it; an agent asks it directly, without your telling:

```sh
omui agent call <app> '{"op":"ui.timeline"}'            # what input did: what it hit, what stopped it, the actions it led to
omui agent call <app> '{"op":"ui.explain","cause":12}'  # one press, from press to release
omui agent call <app> '{"op":"ui.drew","at":[120,48]}'  # which node drew this pixel, and where in the source it was built
```

Reading a person's record needs their consent in a release.

## An agent designing the interface

While `omui dev` runs, an agent can change the running interface: it sends TypeScript or structured edits, they show within a frame, and the source is the one truth. You can take over at any moment; every step is in a history that can be committed, branched and gone back in. A whole session is [example 13](../../../examples/13-live-design).
