# terminal — skill

How an Agent works with an application shown in a terminal's character cells (omui.host.terminal). The operations, their parameters and effects are in [terminal.ops.xdef.ts](terminal.ops.xdef.ts) (the subject, asked to describe itself at its endpoint, answers the same); this is the order to use them in.

## Before

1. Ask `subjects`: beside the interface (`ui`) is the subject `terminal` — the picture in cells. It observes and traces; it changes nothing (control is the interface's).
2. `terminal.frame` gives the size in cells, the grid (a cell in the interface's pixels) and the text a row a line: read it as a person reads the screen.

## Doing

1. Operate the interface as in a window: `ui.*` by id (actions, focus, values), or input as a person's devices give it where the application allows agent input. Nothing about the interface is terminal-specific.
2. To act where something is shown, take the cell to its node: `terminal.at` `[column, row]` gives the node's path and the point a pointer there would hit.
3. Colours and style are in `terminal.frame` with `cells`: ask only when they are in question (about 60 bytes a cell).

## Verifying

- After a change, `terminal.frame` again: the text is what a person now sees; the subject's state hash changes only if a cell did.
- The interface's facts (`ui.timeline`, `ui.explain`) say why it shows what it does; the picture only shows it.

## Not here

Pixels and vectors (`ui.capture` is refused in a terminal: read the text). Images and effects are not drawn in cells.
