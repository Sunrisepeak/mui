// A spring you can throw (the site plan §7.1): a chip on a stage — drag it
// anywhere, fling it and let go: it springs home carrying the speed it was let
// go at (hanim's drive: a glide's velocity handed to a spring), never a jump.
// Grabbed again mid-flight, it is taken from where it is drawn.
import { defineContract } from "openmui/define";

export default defineContract({
  throw_chip: {
    role: "image",
    spec: "0.1",
    doc: "A chip to drag and fling: let go, it springs home with the speed it had; caught mid-flight, it stays where it is.",
    props: { label: { type: "string", default: "Throw me", doc: "What the chip says." } },
    actions: {
      fling: {
        effect: "reversible",
        args: { vx: "f64", vy: "f64" },
        doc: "Sets it moving at (vx, vy) pixels a second from where it is; it springs home.",
      },
      home: { effect: "reversible", doc: "Springs home from where it is, as fast as it moves." },
    },
    parts: { chip: {} },
  },
});
