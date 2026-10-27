# openmui.ui.canvas_kit

What a canvas does beyond panning and zooming (module `openmui.ui.canvas_kit`, canvas-markdown §1.1–1.3): behaviours a canvas names in its `behaviors`, and declarative ones, statecharts in `*.behavior.omui.toml` run by C++.

```cpp
import openmui.ui.canvas_kit;

omui::register_canvas_kit();
omui::canvas({.id = "board",
              .behaviors = std::vector<std::string>{"select", "drag", "resize", "snap:grid", "connect", "grid", "minimap", "delete"},
              .scene = "wall",
              .on_moved = [](double dx, double dy, std::vector<std::string> keys) { save(keys); }});
```

| Behaviour | A person | An agent (the node's actions) | Tells |
|---|---|---|---|
| `select` | a click chooses a node, ⇧ adds or takes out, a click on the background clears; a ⇧-drag on the background draws a marquee (`select:marquee`: any drag); Esc clears, ⌘/Ctrl-A chooses what is in view | `select` (adds), `deselect` | `selection_changed` |
| `drag` | drags a node — by its title bar when its kind has one — and the rest of the selection with it; a click on a chosen one chooses it alone; Esc puts everything back | `move_to` {x, y} | `moved` |
| `resize` | eight handles on the one chosen node; ⇧ on a corner keeps its proportions; no side passes the other (8 px on screen at least); Esc puts it back | `resize` {width, height} | `resized` |
| `snap` | a dragged or resized node's edges and centre meet those of the nodes in view within 6 px, with guides; `snap:grid` also meets the grid's lines, `snap:20` every 20 units | — | — |
| `connect` | ⌥-drag from a node to another connects them | `connect` {to}, `disconnect` {to} | `connected`, `disconnected` |
| `grid` | lines under the nodes, a cell 16–32 px at any zoom, every second one stronger, the finer ones fading in (`grid:40`: the base spacing) | — | — |
| `minimap` | the whole scene in a corner (`minimap:top-left` …), the view outlined; press or drag in it to go there | — | — |
| `delete` | Delete or Backspace removes the selection | `delete` (irreversible: its answer is `yes`) | `deleted` |

Presses are offered from the highest priority: minimap 100, resize 60, connect 50, drag 40, select 30; a declarative behaviour states its own.

## Declarative behaviours

A statechart in TOML (view-data-files §5), loaded with `world::load_behaviors(text, file)` or `world::load_behavior_file(path)` and then named like any other:

```toml
# group-drag.behavior.omui.toml
[behavior.group-drag]
on       = "canvas"
initial  = "idle"
priority = 45

[behavior.group-drag.states.idle]
on.pointer_down = { when = "hit != null && hit.kind == 'label'", to = "dragging", do = ["capture()", "select(hit.node)"] }

[behavior.group-drag.states.dragging]
on.pointer_move = { do = ["move(hit.node, delta.world)"] }
on.pointer_up   = { to = "idle", do = ["commit('group.moved', {'key': hit.node, 'dx': travel.world.x})"] }

[behavior.group-drag.actions.nudge]       # what it adds to a node's actions
when = "node.kind == 'label'"
args = { dx = "f64", dy = "f64" }
do   = ["move(node.key, {'x': args.dx, 'y': args.dy})"]
```

- **Events**: `pointer_down`, `pointer_move`, `pointer_up`, `pointer_cancel`, `tap`, `key`. Each maps to a transition or a list of them; the first whose `when` holds is taken. A transition may go `to` a state and `do` operations; states may have `entry` and `exit` operations (exit, the transition's own, entry — SCXML's order). A press is the behaviour's only if a `pointer_down` transition does `capture()`.
- **Names** the expressions read: `pointer` and `press` ({x, y, world: {x, y}, button, clicks}), `delta` (since the last event) and `travel` (since the press), `hit` (what the press hit: {node, kind, title}) and `over` (what the pointer is over now), `mods` ({shift, ctrl, alt, meta, mod}), `key` (`"Escape"`, `"A"`), and always `selection`, `zoom`, `vars` (what `set` remembered; `vars.args` is what follows the name in `behaviors`) and `state`. An action's expressions read `node` ({key, kind, x, y, width, height}) and `args`.
- **Operations** (`world::behavior_operations()`): `capture`, `move`, `move_to`, `resize`, `raise`, `select`, `add_to_selection`, `toggle_selection`, `clear_selection`, `connect`, `disconnect`, `remove`, `commit`, `set`, `fit`. A target is a node's key or a list of them.
- **Checked when it loads**: every key, state, event, operation, its number of arguments, every function and name in the expressions, the types of an action's arguments, and S-1 (an irreversible action states its answer). A file with a problem registers nothing; each problem has its line and column and, where one is near, the name meant (`did you mean `pointer_down`?`).
- Nothing runs a script: the expressions are evaluated by C++ (`openmui.expr`), so the per-frame path stays native; `commit` is where scripts hear of it.

## Conformance and cost

`tests/canvas_kit_test.cpp` runs every registered behaviour — canvas-kit's, with and without arguments, and a declarative one — through the behaviour CTS: 120 seeded random gestures, clicks, keys and wheel turns, a cancel mid-drag; the same input twice must leave the same scene; every node's item passes spec §3.2's checks; every action offered is called with arguments of its declared types (and its answer); when the canvas lets the behaviour go, its images go too.

`tests/bench` `canvas-kit/drag-100k`: ten chosen windows of 100,000 dragged every frame with select, drag, snap, grid and minimap on — about 0.26 ms a frame at p95 (budget 1 ms). The minimap is drawn as one image made through difference arrays (the nodes plus the pixels, whatever their sizes), in passes of 12,000 nodes a frame, at most ten times a second while the scene changes; the scene keeps its bounds across moves inside them.
