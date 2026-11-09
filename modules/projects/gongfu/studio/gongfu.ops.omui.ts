// gongfu's operations (convergence plan §10.3): what an Agent does with
// fighters through gongfu.studio's command line (`gongfu-studio <verb> …`,
// JSON out, exit 0 done / 1 failed / 2 wrong command). omui-gen ops makes the
// listing (media/ops/gongfu.ops.json for CLI and MCP bindings) and the
// reference (media/ops/gongfu.ops.md) from it; effects are openas's. The
// fighters themselves are data (`<name>.fighter.gongfu.toml`, its look and
// moves) written with any editor; a match is played through a host's stage
// (the fighting showcase's `press`, `turn` and `inputs` actions).
import { defineOps } from "openmui/define";

export default defineOps({
  product: "gongfu",
  doc: "Make and check fighters: hurtboxes baked from the animation, contact sheets, stage pictures, art cut from sheets, the roster's balance.",
  generated: "media/ops",
  operations: [
    {
      name: "fighter.check",
      effect: "read",
      summary: "Whether each fighter's baked hurtboxes are what its look and moves make now (stale ones name the command that fixes them)",
      params: [{ name: "dir", type: "path", doc: "a directory of fighters" }],
      result: "json",
      cost: "bounded",
    },
    {
      name: "fighter.bake",
      effect: "reversible",
      summary: "Bake each fighter's hurtboxes from its look's animation into <name>.boxes.gongfu.toml",
      params: [{ name: "dir", type: "path", doc: "a directory of fighters" }],
      result: "json",
      cost: "bounded",
      undo: "the files are derived: restore the previous ones from version control (or bake again from the sources)",
    },
    {
      name: "fighter.embed",
      effect: "reversible",
      summary: "Write the fighters as a module partition for a program; refused while a bake is stale",
      params: [
        { name: "dir", type: "path" },
        { name: "module", type: "string", doc: "the program's module" },
        { name: "namespace", type: "string" },
        { name: "out", type: "path", doc: "the .cppm written" },
      ],
      result: "json",
      cost: "bounded",
      undo: "delete or restore the written file",
    },
    {
      name: "fighter.sheet",
      effect: "reversible",
      summary: "A contact sheet of a fighter's clips with their boxes, frame by frame, as PNG",
      params: [
        { name: "dir", type: "path" },
        { name: "name", type: "string", doc: "the fighter" },
        { name: "out", type: "path", doc: "the PNG written" },
        { name: "clips", type: "string", doc: "clips by name, a,b", optional: true },
        { name: "frames", type: "string", doc: "frames, 0,12", optional: true },
        { name: "cell", type: "number", doc: "a frame's size in pixels", optional: true },
        { name: "columns", type: "integer", optional: true },
        { name: "boxes", type: "bool", doc: "draw the boxes (default true)", optional: true },
        { name: "palette", type: "integer", doc: "the fighter's colour variant", optional: true },
      ],
      result: "json",
      cost: "bounded",
      undo: "delete the written picture",
    },
    {
      name: "stage.picture",
      effect: "reversible",
      summary: "A stage as a frame of the game: its layers, two fighters for scale, the camera framing them as the game does",
      params: [
        { name: "dir", type: "path", doc: "the stages' directory" },
        { name: "name", type: "string", doc: "<name>.stage.gongfu.toml" },
        { name: "out", type: "path" },
        { name: "fighters", type: "string", doc: "a fighters' directory and two names: <dir> a,b", optional: true },
        { name: "at", type: "number", optional: true },
        { name: "apart", type: "number", doc: "units between the fighters", optional: true },
        { name: "frame", type: "integer", optional: true },
      ],
      result: "json",
      cost: "bounded",
      undo: "delete the written picture",
    },
    {
      name: "art.cut",
      effect: "reversible",
      summary: "Cut a sheet of drawn parts on a flat background into pieces: each keyed out and written as a PNG, listed with its region",
      params: [
        { name: "sheet", type: "path", doc: "the sheet PNG" },
        { name: "out", type: "path", doc: "the directory written" },
        { name: "key", type: "string", doc: "the background, r,g,b", optional: true },
        { name: "gap", type: "integer", optional: true },
        { name: "min", type: "integer", doc: "the smallest piece's area", optional: true },
        { name: "alpha", type: "integer", optional: true },
        { name: "scale", type: "number", optional: true },
      ],
      result: "json",
      cost: "bounded",
      undo: "delete the written pieces",
    },
    {
      name: "art.picture",
      effect: "reversible",
      summary: "A picture made ready for a look: keyed out, trimmed to what it shows, scaled to a width, written as PNG or WebP",
      params: [
        { name: "file", type: "path" },
        { name: "out", type: "path" },
        { name: "key", type: "string", optional: true },
        { name: "trim", type: "bool", optional: true },
        { name: "width", type: "integer", optional: true },
        { name: "quality", type: "number", optional: true },
      ],
      result: "json",
      cost: "bounded",
      undo: "delete the written picture",
    },
    {
      name: "roster.versus",
      effect: "read",
      summary: "Bots play every pair of the directory's fighters, sides swapped: wins, damage, supers and round length per pair; each fighter's win rate and where its damage comes from",
      params: [
        { name: "dir", type: "path" },
        { name: "rounds", type: "integer", doc: "per pair (200)", optional: true },
        { name: "seed", type: "integer", optional: true },
        { name: "system", type: "path", doc: "a system file other than the default", optional: true },
      ],
      result: "json",
      cost: "heavy",
    },
  ],
});
