// A program's own small storage for scripts: settings, progress, a draft —
// kept where the host keeps such things (the page's localStorage on the Web,
// memory when the host keeps none). The bindings are generated from this
// contract: `store.get(…)` in TypeScript, Python and Lua.
import { defineNative } from "openmui/define";

export default defineNative({
  store: {
    spec: "0.1",
    doc: "The program's own small key-value storage: text values by key, kept by the host (the page's localStorage on the Web; memory where the host keeps none).",
    functions: {
      get: {
        doc: "The value kept under a key; nothing when there is none.",
        returns: "string?",
        effect: "read",
        args: { key: { type: "string", doc: "1 to 256 bytes" } },
      },
      set: {
        doc: "Keeps a value under a key, in place of what was there. Refused when the key is not one or the host's storage is full.",
        effect: "reversible",
        args: {
          key: { type: "string", doc: "1 to 256 bytes" },
          value: { type: "string", doc: "any text; the host's storage is small (a page's: about 5 MB for all of an origin's programs)" },
        },
      },
      remove: { doc: "Forgets a key; whether it was there.", returns: "bool", effect: "reversible", args: { key: "string" } },
      keys: {
        doc: "The keys kept, in byte order; only those that begin with the prefix when one is given.",
        returns: "string[]",
        effect: "read",
        args: { prefix: { type: "string?", doc: "what the keys begin with (all of them when left out)" } },
      },
    },
  },
});
