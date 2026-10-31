// openmui.ui.timeline — the timeline every editor shares, as a component.
//
//     timeline_editor({.id = "edit", .document = text, .zoom = 120})
//
// Tracks of clips in exact time (openmui.timeline): a ruler in frames and
// seconds, a header a track, the clips coloured by their track's kind with
// their fades and keyed properties marked, the markers and the playhead. A
// person clicks to select and to move the playhead, drags a clip to move
// it, drags its edge to trim it, presses S to split it at the playhead and
// Delete to remove it, Ctrl+Z and Ctrl+Shift+Z to undo and redo; an agent
// does the same through the actions. Every edit is openmui.timeline's change
// with its exact inverse, and after each the `edited` event carries the
// document as it now is.
export module openmui.ui.timeline;

export import :timeline_editor_contract;
export import :view;
