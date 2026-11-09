// xrender's operations (convergence plan §10.3): what an Agent does with the
// renderer without a GUI, through the `xrender` command (JSON out; exit 0
// done, 1 failed with why, 2 a wrong command). omui-gen ops makes the listing
// (media/ops/xrender.ops.json for CLI and MCP bindings) and the reference
// (media/ops/xrender.ops.md); effects are openas's. Drawing itself is the
// library's (a display list into the CPU or GPU renderer); a running
// application's pixels are its host's `capture`.
import { defineOps } from "openmui/define";

export default defineOps({
  product: "xrender",
  doc: "Check and preview pixel effects through the real GPU renderer, run the render contract's conformance suite on this machine's device, describe the device.",
  generated: "media/ops",
  operations: [
    {
      name: "effect.check",
      effect: "read",
      summary: "Read each effect file, compile its shader for the device formats and register it: its parameters, or every problem at file, line and column",
      params: [{ name: "files", type: "json", doc: "the <name>.effect.omui.toml files" }],
      result: "json",
      cost: "bounded",
    },
    {
      name: "effect.preview",
      effect: "reversible",
      summary: "Draw an effect with the GPU renderer over a picture (a test card when none), before | after side by side, as PNG",
      params: [
        { name: "file", type: "path", doc: "the effect file" },
        { name: "out", type: "path", doc: "the PNG written" },
        { name: "image", type: "path", doc: "a PNG to apply it to", optional: true },
        { name: "size", type: "string", doc: "WxH of the test card", optional: true },
        { name: "param", type: "json", doc: "NAME=VALUE overrides, clamped to each parameter's range", optional: true },
      ],
      result: "json",
      cost: "bounded",
      undo: "delete the written picture",
    },
    {
      name: "conformance.run",
      effect: "read",
      summary: "The render contract's conformance suite on this machine's device: each case against the CPU reference within its tolerance; failing pictures kept",
      params: [{ name: "report", type: "path", doc: "write the full *.cts.omui.json report here", optional: true }],
      result: "json",
      cost: "heavy",
    },
    {
      name: "device.describe",
      effect: "read",
      summary: "The GPU device: backend, name, whether it is software, shader formats, limits",
      result: "json",
    },
  ],
});
