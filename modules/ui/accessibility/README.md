# openmui.accessibility

The semantic tree served to the platform's assistive technology (D-21): a
screen reader, a magnifier or voice control reads the same tree agents and
tests read, and asks for its actions.

| Platform | Service | Through |
|---|---|---|
| Windows x86_64 | UI Automation | AccessKit's subclassing adapter on the window's HWND |
| macOS arm64 | NSAccessibility | AccessKit's subclassing adapter on the NSWindow's content view |
| Linux x86_64 | AT-SPI (D-Bus) | AccessKit's unix adapter |
| elsewhere | none | the mapping alone; `available` is false |

AccessKit's C bindings come from mcpp-index (`compat.accesskit`, imported as
the module `mcpplibs.mwrap.accesskit`), only in `src/native/accesskit/`.

- **The mapping** (`describe`) — a node becomes AccessKit's: its role by the
  spec's own column (`roles.toml`'s `accesskit`); its name, or for static
  text its words as its value; a slider's number and range; whether a
  checkbox, switch or radio is on (or mixed); a heading's level; a link's
  address; a scroll view's offsets; disabled, read-only, required, invalid,
  busy, hidden, modal, selected, expanded; status and timer as live regions.
  Actions keep AccessKit's names where it has them (click is the node's
  activate, else toggle, else select); the others with no arguments
  (dismiss, deselect…) are custom actions a screen reader lists by name.
- **The mirror** (`Mirror`) — the tree as the platform was last told it:
  `refresh` reads what changed (the nodes whose children changed too) and
  where the focus is; the window is the root, named as the window. Node keys
  are FNV-1a of the ids, the same from one update to the next.
- **Requests** — come from the platform's threads, wait in the mirror, and are
  performed between frames on the runtime's thread (`serve`), as the person's
  own input: what a click would answer, an assistive device's click answers.
  A request for an action the node does not have is refused, not guessed.
- **The adapter** (`Adapter::attach`) — answers the system's first question
  from the mirror and is told, after each frame, what changed.
  `openmui.host.sdl3` attaches it before the window is first shown
  (`DesktopOptions::accessibility`, `AppSpec::accessibility`,
  `--no-accessibility`).

Tested: the mapping and the mirror on every platform; AccessKit's own reading
of the update where it has a library; on Linux x86_64 CI, `tools/ci/atspi.sh`
— an AT-SPI client over D-Bus, as Orca speaks it, reads example 03's tree
(names, roles, states, the slider's value) and presses its button and sets
its slider. Trace category and log category `accessibility`.

Depends on `openmui.core`, `openmui.view`, `openmui.semantic`, `openmui.hash`
and `openmui.trace`.
