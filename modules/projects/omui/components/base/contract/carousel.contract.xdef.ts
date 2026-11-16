// One of several slides at a time, the others beside it: the arrows at its
// sides, its dots, the arrow keys, a swipe or an agent move between them (how
// the move looks is its slot's). Each slide reachable as an item of its own
// (`<id>/<index>`); taken back.
import { defineContract } from "openmui/define";

export default defineContract({
  carousel: {
    role: "region",
    spec: "0.1",
    doc: "One of several slides at a time; arrows, dots, keys, a swipe or an agent move between them.",
    children: "any",
    props: {
      index: { type: "i64", default: 0, bindable: true, doc: "The slide shown, from 0." },
      wrap: { type: "bool", default: false, doc: "After the last, the first again." },
      label: { type: "string", default: "", doc: "Its name." },
    },
    events: { changed: { args: { index: "i64" } } },
    actions: { set_index: { effect: "reversible", args: { index: "i64" } } },
    keys: { Right: "next", Left: "previous" },
    commands: { next: { doc: "The next slide." }, previous: { doc: "The slide before." } },
    parts: { track: {}, arrow: {}, dot: {}, focus_ring: {} },
  },
});
