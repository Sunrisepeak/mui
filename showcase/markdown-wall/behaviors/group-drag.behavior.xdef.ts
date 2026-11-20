// A workspace dragged by its label, with every window lying on it; Esc or a
// cancelled gesture puts them back. Told as `moved` (all of them: one step
// of history, undone together) and `workspace.moved`.

import { defineBehavior } from "omui/define";

export default defineBehavior({
  "group-drag": {
    on: "canvas",
    initial: "idle",
    priority: 45,
    doc: "Drags a workspace by its label, with the windows on it.",

    states: {
      idle: {
        on: {
          pointer_down: { when: "hit != null && hit.kind == 'workspace' && hit.title && !mods.alt", to: "dragging", do: ["capture()", "raise(hit.node)", "raise(hit.inside)"] },
        },
      },

      dragging: {
        on: {
          pointer_move: { do: ["move(hit.node, delta.world)", "move(hit.inside, delta.world)"] },
          pointer_up: { to: "idle", do: ["commit('moved', {'keys': hit.group, 'dx': travel.world.x, 'dy': travel.world.y})", "commit('workspace.moved', {'workspace': hit.node, 'dx': travel.world.x, 'dy': travel.world.y})"] },
          pointer_cancel: { to: "idle", do: ["move(hit.node, {'x': -travel.world.x, 'y': -travel.world.y})", "move(hit.inside, {'x': -travel.world.x, 'y': -travel.world.y})"] },
          key: { when: "key == 'Escape'", to: "cancelled", do: ["move(hit.node, {'x': -travel.world.x, 'y': -travel.world.y})", "move(hit.inside, {'x': -travel.world.x, 'y': -travel.world.y})"] },
        },
      },

      // Put back: what is left of the gesture changes nothing.
      cancelled: {
        on: {
          pointer_up: { to: "idle" },
          pointer_cancel: { to: "idle" },
        },
      },
    },
  },
});
