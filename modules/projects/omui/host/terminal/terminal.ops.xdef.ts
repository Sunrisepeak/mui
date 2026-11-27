// A terminal's picture as a subject of the agent kernel (D-173; terminal design §4): what an Agent reads of an
// application shown in a terminal's character cells — the frame as text, each cell's colours and style, and from
// a cell back to the node that drew it. The interface itself (`ui.*`) is served beside it as a window's is.
// ops-gen makes the table (src/catalog.cppm) and the listings (src/terminal.ops.json, terminal.ops.md).
import { defineOps } from "mlibs/define";

export default defineOps({
  product: "terminal",
  module: "omui.host.terminal",
  namespace: "omui::terminal",
  generated: "src",
  doc: "Read an application's picture in a terminal: its cells as text, their colours and style, and what drew one.",
  operations: [
    {
      name: "terminal.frame",
      effect: "read",
      summary: "The frame as the terminal shows it: its size in cells, the grid in the interface's pixels, its text a row a line; with `cells`, each cell's grapheme, colours (#rrggbb), style and the node that drew it",
      params: [{ name: "cells", type: "bool", doc: "each cell too, row by row (the text alone when not given)", optional: true }],
      result: "json",
    },
    {
      name: "terminal.at",
      effect: "read",
      summary: "What drew a cell: its grapheme, colours and style, and the node that drew it last — its id and path in the interface — with the point in the interface's pixels a pointer there would hit",
      params: [{ name: "at", type: "json", doc: "[column, row], from 0 at the top left" }],
      result: "json",
    },
  ],
  tasks: [
    {
      name: "read-the-screen",
      summary: "Read what the terminal shows, a row a line",
      steps: [
        { op: "terminal.frame" },
      ],
    },
    {
      name: "ask-what-drew-a-cell",
      summary: "Go from a cell back to the node that drew it",
      steps: [
        { op: "terminal.at", at: [2, 1] },
      ],
    },
  ],
});
