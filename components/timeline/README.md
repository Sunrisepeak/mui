# openmui.ui.timeline

The timeline every editor shares, as a component (D-86): `timeline_editor` shows and edits an [`openmui.timeline`](../../modules/media/timeline) document.

```cpp
import openmui.ui.timeline;

Element edit = timeline_editor({.id = "edit", .document = text, .frame = playhead, .zoom = 120});
edit.on("edited", [](const Value& v) { save(v.get("document")->as_string()); });
```

- **What it shows.** A ruler in seconds, with frame ticks when they are far enough apart. Each track has a header with its kind's colour. Its clips show their names, their fades as shaded corners and their keyed properties as small diamonds. Markers are flags, and the playhead is a red line.
- **A person edits it.**
  - A click on the ruler moves the playhead; a click on a clip selects it.
  - A drag moves the clip by whole frames, onto another track of the same kind.
  - A drag of the clip's edge trims it, with its source staying under its time.
  - S splits it at the playhead, Delete removes it (Shift+Delete ripples), Ctrl+Z and Ctrl+Shift+Z undo and redo.
  - Left and Right step a frame (a second with Shift), Home and End go to the start and the end, the wheel scrolls and Ctrl+wheel zooms about the pointer.
- **An agent edits it** through the same actions: `seek`, `select`, `move`, `trim`, `split`, `remove`, `undo`, `redo`. Its semantic value is the timeline as data: each track's clips in frames, the playhead, the selection, the lengths of the undo and redo histories, the last edit, and the document's problems.
- **Every edit undoes exactly.** It is `openmui.timeline`'s change, and applying it answers its inverse. After each edit the `edited` event carries the document as it now is, for its owner to keep. The playhead and the zoom write back. A document that does not read shows its first problem, and its value lists them all with their lines and columns.

Tests (`mcpp test -p openmui.ui.timeline`):
- the document as an agent sees it;
- an agent's edits each undone and redone to the document's very text;
- a person's clicks, drags, edge trims and keys;
- a document that does not read;
- the picture pinned.
