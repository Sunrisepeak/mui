// Something is happening, how long unknown: an arc turning on the runtime's
// clock — still, a full ring at rest, when motion is reduced. Busy to every
// reader; its name says what is happening.
import { defineContract } from "openmui/define";

export default defineContract({
  spinner: {
    role: "progress",
    spec: "0.1",
    doc: "Something is happening, how long unknown: an arc turning.",
    props: {
      size: { type: "f64", default: 20, doc: "Its diameter." },
      label: { type: "string", default: "Loading", bindable: true, doc: "What is happening (its name)." },
    },
    parts: { track: {}, arc: {} },
  },
});
