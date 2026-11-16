# xtools

The toolchain whole: one command and one MCP server over every project's toolkit, each command behaving as it does on its own (internal code name, D-208).

It is a thin composition (convergence plan §4.5, D-203, D-204). Each project keeps its toolkit:
- the library and its operations;
- the catalog (`<product>.ops.omui.ts`);
- a thin program;
- its skill.

This project only wires those toolkits into one session of the tool kernel (mlibs.agent, T0). Everything it can do can be found in some project's toolkit. It writes no domain logic.

| Toolkit | Its own command | Its subject | Its project |
|---|---|---|---|
| [omui.cli](../omui/tools/cli) | `omui` | `omui` (the project's commands), the protocol's catalogs (`omui agent`, `omui page`) | omui |
| [hanim.studio](../hanim/studio) | `hanim` | `document` | hanim |
| [hanim.timeline.tool](../hanim/timeline/tool) | `hanim-timeline` | `timeline` | hanim |
| [xrender.tool](../xrender/tool) | `xrender` | `xrender` | xrender |
| [gongfu.studio](../gongfu/studio) | `gongfu-studio` | `gongfu` | gongfu |

```sh
xtools hanim document.check rise.json        # the same as: hanim document.check rise.json
xtools omui build --release                  # the same as: omui build --release
xtools fighter.check showcase/fighting/fighters   # an operation by its whole name, whichever toolkit has it
xtools subjects                              # every toolkit's subject
xtools find "check the hurtboxes"            # what answers an intent, across the toolkits
xtools mcp                                   # every toolkit's operations to an MCP client
```

A toolkit's command under `xtools <toolkit> …` is its own command: the same library function, the same request, the same records.

An operation's whole name reaches it in the one session that holds every subject. What holds a terminal (`omui dev`, `run`, `connect`, `mcp`) runs from a person's command line, as under `omui`; `xtools mcp` does not list it.

## Agent surface

Nothing of its own: each toolkit's subject and catalog are its project's package (lint `agent-surface`: a composition). `xtools subjects` lists them, `describe` and `find` reach across them, and every request is decided and answered by the subject that holds the operation.

## Experimental (release 0.1, D-199)

- **Target** (plan §4.5, RC3.10): the toolchain's whole is one entry and one workbench: each project's toolkit reached the same way alone or composed, and the cross-project tools (the inspector, Live Design's composition of the UI and animation adapters) here as compositions.
- **Now**: the unified command and MCP server over five toolkits.
- **Gap**: the workbench (an omui application composing the toolkits' panels), the inspector moved in from `tools/`, Live Design's composition of the UI and hanim adapters.
- **Limits**: nothing of its own beyond the wiring: a toolkit not linked into it is not reachable through it.
- **Graduation to preview**: the workbench and both compositions here, with a headless case that designs a UI and an animation in one workspace (RC3.10's bar).
