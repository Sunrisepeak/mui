// xrender's operations (convergence plan §10.3): what an Agent does with the
// renderer without a GUI, through the `xrender` command (JSON out; exit 0
// done, 1 failed with why, 2 a wrong command). omui-gen ops makes the listing
// (media/ops/xrender.ops.json for CLI and MCP bindings) and the reference
// (media/ops/xrender.ops.md); effects are openas's. Drawing itself is the
// library's (a display list into the CPU or GPU renderer); a running
// application's pixels are its host's `capture`.
import { defineOps } from "omui/define";

export default defineOps({
  product: "xrender",
  doc: "Check and preview pixel effects through the real GPU renderer, run the render contract's conformance suite on this machine's device, describe the device, look at a glTF model beside another renderer's picture of it.",
  generated: "media/ops",
  operations: [
    {
      name: "effect.check",
      effect: "read",
      summary: "Read each effect file, compile its shader for the device formats and register it: its parameters, or every problem at file, line and column",
      params: [{ name: "files", type: "json", doc: "the <name>.effect.omui.json files (what a .effect.omui.ts evaluates to)" }],
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
      name: "model.look",
      effect: "reversible",
      summary: "Draw a glTF model with the CPU reference as a viewer of products frames it, lit by an HDR picture of the surroundings, as PNG; say how far it is from another renderer's picture of the same (the render-fidelity suite's measure) and from the light followed way by way",
      params: [
        { name: "model", type: "path", doc: "a .gltf or .glb" },
        { name: "out", type: "path", doc: "the PNG written: nothing where the model is not" },
        { name: "light", type: "path", doc: "a Radiance .hdr of the surroundings", optional: true },
        { name: "orbit", type: "string", doc: "THETA,PHI,RADIUS: degrees about the way up from +z, degrees down from it, how far", optional: true },
        { name: "target", type: "string", doc: "X,Y,Z the eye looks at", optional: true },
        { name: "size", type: "string", doc: "WxH, 8 to 4096 each", optional: true },
        { name: "upright", type: "string", doc: "the angle of view up and down, degrees", optional: true },
        { name: "samples", type: "string", doc: "N by N samples a pixel, 1 to 4", optional: true },
        { name: "behind", type: "bool", doc: "where the model is not, the surroundings as the eye sees them (a viewer's skybox); with light", optional: true },
        { name: "golden", type: "path", doc: "another renderer's PNG of the same size to stand beside", optional: true },
        { name: "follow", type: "string", doc: "rays a pixel for the light followed way by way, a quarter as large each way", optional: true },
      ],
      result: "json",
      cost: "heavy",
      undo: "delete the written pictures",
    },
    {
      name: "device.describe",
      effect: "read",
      summary: "The GPU device: backend, name, whether it is software, shader formats, limits",
      result: "json",
    },
  ],
});
