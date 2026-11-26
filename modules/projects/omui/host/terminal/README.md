# omui.host.terminal

openmui in a terminal's character cells (module `omui.host.terminal`; [terminal design](../../../../../.agents/docs/2026-11-19-terminal-host-design.md), D-249). The same tree, layout and display list as a window's, read into cells by [mrender.cells](../../../mrender/cells) instead of pixels: the components are the window's, unchanged. Text is measured in cells by [mlibs.text](../../../../content/text)'s cell measure (a grapheme its cells, a line a row) over the grid the host gives — a cell is `grid.column × grid.row` of the interface's logical pixels — so layout, hit testing and focus work as they do in a window. Its own font collection measures in cells whatever else the process installed, so a window's program can show a terminal too (`omui::run` with `--terminal`).

- **Frames are values** (`step`, `frame`): `cols × rows` cells, compared as text — a terminal's picture is a text baseline a test compares, with no terminal at all.
- **Input** (`terminal::Input`): the terminal's bytes as the events every host delivers — keys (ECMA-48 and xterm's sequences with their modifiers), text (UTF-8; a key and its text, as a window's host gives them), the mouse (SGR mode 1006: buttons, drags, motion, the wheel, at the centre of the cell named), pasted text (bracketed paste) and focus. A sequence cut between reads is kept; an Escape alone is the key once nothing follows it; Ctrl+C ends the program.
- **On this program's terminal** (`run`): keys read as typed (mcpplibs.os's raw mode), the alternate screen, the mouse, pasting and focus reported, the terminal's size followed; each frame only the cells that changed, inside synchronized output; 24-bit colour where `COLORTERM` says so, else the 256. Input or output that is not a terminal (a pipe) is read and written the same way, without the modes.

**Why a package of its own**: a terminal's program carries this host and no window, and a window's no terminal; it is a platform of its own (`separate`).

**Label**: preview — new (CF1). Verified headless (frames as values, the decoder, keys and the mouse, the agent's operations) on Linux x64; on a real terminal as `run` shows it, by hand. Not verified: terminal emulators' differences (synchronized output, the mouse, wide characters) beyond the ones this machine has; Windows' console (its own input, not ECMA-48 bytes) is not supported yet.

## Agent surface

- **Subject and addresses**: the terminal's picture, `terminal` (`omui.host.terminal@0.1`), beside the interface the endpoint serves as a window's (`ui.*`: the tree, actions, input). Cells are addressed `[column, row]` from the top left.
- **Observation**: `terminal.frame` — the size in cells, the grid in the interface's pixels, the text a row a line; with `cells`, each cell's grapheme, colours (`#rrggbb`), style and the node that drew it.
- **Operations and effects**: both read (openas `read`); control is the interface's (`ui.*` actions, or input as a person's devices give it, when the application allows agent input).
- **Causes**: `terminal.at` goes from a cell to the node that drew it last (its path in the interface) and the point a pointer in it would hit; why the node shows what it does is the interface's (`ui.explain`).
- **Preview and time**: none of its own: a picture is a function of the interface at its frame.
- **Budgets and tiers**: `terminal.frame` with cells answers about 60 bytes a cell (a 200 × 60 terminal: 0.7 MB); without, the text alone.
- **What it cannot do**: no pixels (`ui.capture` is refused, with the way to the text), no vectors; images and effects are not drawn in cells (the terminal graphics protocols are a later level, design §3 ②).
