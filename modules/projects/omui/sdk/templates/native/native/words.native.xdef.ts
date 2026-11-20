// A native module: C++ functions scripts call (component-binding §8). The
// bindings are generated from this file; src/words.cpp implements them.
import { defineNative } from "openmui/define";

export default defineNative({
  words: {
    spec: "0.1",
    doc: "Counts in a text: words, characters, sentences.",
    types: { Counts: { doc: "What a text holds.", fields: { words: "u32", characters: "u32", sentences: "u32" } } },
    functions: { count: { doc: "The words, characters (code points) and sentences of a text.", returns: "Counts", args: { text: "string" } } },
  },
});
