# xtools

The toolchain whole: one command and one MCP server over every project's toolkit, each command behaving as it does on its own (internal code name, D-208).

| Capability | Label | Packages | Evidence |
|---|---|---|---|
| One command and one MCP server over every toolkit, each command as it is on its own | experimental | `xtools` | [xtools_test](tests/xtools_test.cpp), [workspace_test](tests/workspace_test.cpp) |
| The Inspector: a running application's tree, frames and clock; the motion workbench; the Reaction lab | stable | `xtools.inspector` | [inspector_test](inspector/tests/inspector_test.cpp), [motion_test](inspector/tests/motion_test.cpp) |

It is a thin composition (convergence plan §4.5, D-203, D-204). Each project keeps its toolkit:
- the library and its operations;
- the catalog (`<product>.ops.xdef.ts`);
- a thin program;
- its skill.

This project only wires those toolkits into one session of the tool kernel (mlibs.agent, T0). Everything it can do can be found in some project's toolkit. It writes no domain logic.

| Toolkit | Its own command | Its subject | Its project |
|---|---|---|---|
| [omui.cli](../omui/tools/cli) | `omui` | `omui` (the project's commands), the protocol's catalogs (`omui agent`, `omui page`) | omui |
| [hanim.studio](../hanim/studio) | `hanim` | `document` | hanim |
| [hanim.timeline.tool](../hanim/timeline/tool) | `hanim-timeline` | `timeline` | hanim |
| [mrender.tool](../mrender/tool) | `mrender` | `mrender` | mrender |
| [gongfu.studio](../gongfu/studio) | `gongfu-studio` | `gongfu` | gongfu |
| [meng.video.tool](../meng/kernel/video/tool) | `video` | `video` (here with omui's sources, composed by xtools' `films()`: a view's film, data modules anywhere; `omui design film` runs it) | meng |

The compositions that are more than one project's toolkit live here too (D-204):

| Package | What it composes |
|---|---|
| [xtools.audition](audition) | A timeline's gain automation heard on a real audio bus and seen in a headless omui view (hanim.timeline, meng.audio, omui) |
| [xtools.inspector](inspector) | The Inspector (`xtools-inspector`): a running application's semantic tree, a node's details, frame times, its pipeline and clock (omui); the motion workbench over an animation document (hanim.studio); the Reaction lab over cellular projects (meng.cellular) |

```sh
xtools hanim document.check rise.json        # the same as: hanim document.check rise.json
xtools omui build --release                  # the same as: omui build --release
xtools fighter.check showcase/fighting/fighters   # an operation by its whole name, whichever toolkit has it
xtools subjects                              # every toolkit's subject
xtools video render hello.view.xdef.json --out shots/   # the video toolkit with omui's view source
xtools find "check the hurtboxes"            # what answers an intent, across the toolkits
xtools mcp                                   # every toolkit's operations to an MCP client
```

A toolkit's command under `xtools <toolkit> …` is its own command: the same library function, the same request, the same records.

An operation's whole name reaches it in the one session that holds every subject. What holds a terminal (`omui dev`, `run`, `connect`, `mcp`) runs from a person's command line, as under `omui`; `xtools mcp` does not list it.

## Agent surface

Nothing of its own: each toolkit's subject and catalog are its project's package (lint `agent-surface`: a composition). `xtools subjects` lists them, `describe` and `find` reach across them, and every request is decided and answered by the subject that holds the operation.

## Experimental (release 0.1, D-199)

- **Target** (plan §4.5, RC3.10): the toolchain's whole is one entry and one workbench: each project's toolkit reached the same way alone or composed, and the cross-project tools (the inspector, Live Design's composition of the UI and animation adapters) here as compositions.
- **Now**: the unified command and MCP server over five toolkits; the Inspector composed here; Live Design's composition: the interface's and hanim's design adapters in one workspace under one history, headless (`tests/workspace_test.cpp`, RC3.10's bar), each adapter and the composition passing the adapter's contract (mlibs.agent.sources@1).
- **Gap**: the workbench (an omui application composing the toolkits' panels and the workspace's history); the workspace reached through `xtools` as operations; the interface's live script sources (the player's) as an adapter beside the view documents'.
- **Limits**: nothing of its own beyond the wiring: a toolkit not linked into it is not reachable through it.
- **Graduation to preview**: the workbench and both compositions here, with a headless case that designs a UI and an animation in one workspace (RC3.10's bar).

## Known limitations

Each names the level it is solved at (D-179: foundation, part, specialised, application).

- **No workbench yet** (application): the toolkits are composed as one command and one MCP server; the omui application composing their panels and the workspace's history is owed.
- **Only what is linked** (part): a toolkit not linked into `xtools` is not reachable through it.

## Closure exceptions

- **7**: a composition has no scene of its own to budget: every operation it reaches is its toolkit's, measured where that toolkit's project budgets it (tests/bench), and a bench scene here would link every toolkit into the bench's release build.
