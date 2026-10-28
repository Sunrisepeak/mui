# openmui.mcp

An MCP server (the Model Context Protocol, 2025-06-18) for every openmui application running on the machine (plan §11, S3). An agent — Claude Code, or any MCP client — reads an application's semantic tree, finds nodes, acts, invokes verbs, waits and captures pixels, over the application's own agent endpoint (`openmui.tree@0.1`, [openmui.agent](../../modules/ui/agent)).

```sh
claude mcp add openmui -- omui mcp          # or: omui-mcp
omui mcp --app org.openmui.notes            # one application when a call names none
```

| Tool | What it does |
|---|---|
| `ui_apps` | the applications running: endpoint names and app ids |
| `ui_snapshot` | the semantic tree as an outline — one node a line: `id role "name" = value [states] {actions} @x,y wxh`, children under their parent |
| `ui_find` | nodes by role, name glob, state, or a whole query (spatial: `within`) |
| `ui_act` | an action a node offers (activate, toggle, set_value …) |
| `ui_invoke` | an application verb (`notes.new` …) |
| `ui_wait` | until a query matches (or, `absent`, does not) and/or the interface settles |
| `ui_capture` | the window's or a node's pixels, as a PNG image |

A call names its application with `app` (an endpoint name or an app id); without one it is the server's `--app`, or the only one running.

**The person decides what cannot be undone.** An action the application marks `irreversible` or `external` answers *needs-answer* (S-1). The server then asks the person: with a client that offers MCP elicitation, a confirmation the client shows ("agent:claude-code wants to: delete-all on 'main' (irreversible)"); with their yes it acts again, the actor recorded as `agent:claude-code;via=person:user`, and with their no nothing is done. A client without elicitation is told the action needs the user's confirmation, and to pass `answer` only if the user gave it. The tool's annotations say which tools only read.

**And what is the person's own stays theirs.** An action marked `person` (S-10: the answer to a confirmation in a cockpit, a permission) shows as `(person)` in the outline, and the application refuses it to any request over the bus, whatever actor that request names — an agent cannot approve its own request by pressing the button that approves it.

The actor is the client's own name from `initialize` (`agent:claude-code`), or `--actor`. Every call can be journalled (`ServerOptions::journal`) — what a cockpit shows.

`Server::handle` takes one JSON-RPC line and writes its answers to a `Transport`; `serve` loops over one (`StdioTransport`: this process's standard streams, as bytes). The tests (`tests/mcp_test.cpp`) run a headless application with a real endpoint and drive the server line by line: the protocol and its errors, every tool, a refused irreversible action, and an elicited one allowed and declined. CI drives `omui mcp` over real pipes against example 07 running as its own process (`tools/ci/agent-e2e.sh`).
