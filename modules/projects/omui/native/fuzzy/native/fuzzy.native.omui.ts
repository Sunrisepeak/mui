// omui::fuzzy (the :matching partition) for scripts: command palettes, finders, a long list filtered
// as a person types (component-binding §8). The bindings are generated from
// this contract: `fuzzy.rank(…)` in TypeScript.
import { defineNative } from "openmui/define";

export default defineNative({
  fuzzy: {
    spec: "0.1",
    doc: "Fuzzy matching as a finder does it: the candidates a query matches, best first, with what to highlight.",
    types: {
      Segment: { doc: "A run of a candidate, matched or not: what a view highlights.", fields: { text: "string", matched: "bool" } },
      Match: {
        doc: "A candidate the query matched.",
        fields: {
          index: { type: "u32", doc: "its position in the list" },
          text: { type: "string", doc: "the candidate" },
          score: { type: "i32", doc: "higher is better" },
          positions: { type: "u32[]", doc: "the matched characters, as code point indices" },
          segments: { type: "Segment[]", doc: "the candidate as runs to highlight" },
        },
      },
    },
    functions: {
      rank: {
        doc: "The candidates the query matches, best first (by score, then the shorter, then the earlier); for an empty query, all of them in order.",
        returns: "Match[]",
        effect: "read",
        async: true,
        args: {
          query: { type: "string", doc: "what the person typed; words separated by spaces must all match" },
          candidates: { type: "string[]", doc: "the list to search" },
          limit: { type: "u32?", doc: "at most this many (all when left out)" },
        },
      },
      match: {
        doc: "How a query lines up with one candidate; nothing when it does not match.",
        returns: "Match?",
        effect: "read",
        args: { query: "string", candidate: "string" },
      },
    },
  },
});
