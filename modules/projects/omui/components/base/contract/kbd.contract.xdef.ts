// A key or a chord to press, drawn as the keys themselves: each key a cap
// ("Ctrl" "Shift" "P"), chords pressed in turn spaced apart ("G G"); `Mod`
// is the command key of the system it runs on (⌘ on Apple's, Ctrl
// elsewhere). Read as the keys' names ("Control+Shift+P").
import { defineContract } from "openmui/define";

export default defineContract({
  kbd: {
    role: "text",
    spec: "0.1",
    doc: "A key or a chord to press, drawn as the keys themselves.",
    props: {
      keys: { type: "string", default: "", bindable: true, doc: "The keys, `+` between them (\"Mod+K\"); a space between chords pressed in turn (\"G G\")." },
    },
    parts: { cap: {}, label: {} },
  },
});
