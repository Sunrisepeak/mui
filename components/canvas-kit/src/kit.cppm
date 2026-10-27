// openmui.ui.canvas_kit — canvas behaviours (canvas-markdown §1.1–1.3).
//
//     import openmui.ui.canvas_kit;
//     omui::register_canvas_kit();
//     omui::canvas({.id = "board", .scene = "wall",
//                   .behaviors = {"select", "drag", "resize", "snap", "connect", "grid", "minimap", "delete"}});
//
//   select    a click chooses a node (⇧ adds or takes out), a click on the
//             background clears; a ⇧-drag on the background (any drag, with
//             "select:marquee") draws a marquee; Esc clears; ⌘/Ctrl-A
//             chooses what is in view; a node's `select` / `deselect`
//   drag      a node (by its title bar, when its kind has one) and the rest
//             of the selection with it; Esc puts them back; `move_to` {x, y}
//   resize    handles on the one chosen node (⇧ on a corner keeps its
//             proportions); `resize` {width, height}
//   snap      moving and resized edges and centres meet the other nodes'
//             (with guides); "snap:grid" and the grid's lines, "snap:20" every 20 units
//   connect   ⌥-drag from a node to another connects them; `connect` /
//             `disconnect` {to}
//   grid      lines under the nodes, a cell 16–32 pixels at any zoom ("grid:40": its base)
//   minimap   the whole scene in a corner ("minimap:top-left"), the view
//             outlined; press or drag in it to go there
//   delete    Delete / Backspace removes the selection; a node's `delete`
//             (irreversible: its answer is "yes")
//
// Declarative behaviours (:declarative) come from *.behavior.omui.toml.
export module openmui.ui.canvas_kit;

export import openmui.ui.canvas;
export import :declarative;

import std;

export namespace omui {

// Registers the behaviours above by name (world::register_behavior).
void register_canvas_kit();

}  // namespace omui
