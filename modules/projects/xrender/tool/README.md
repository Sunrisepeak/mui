# xrender.tool

The `xrender` command: the renderer without a GUI, for people, agents and CI.

```sh
xrender device.describe                                            # backend, device, formats, limits
xrender effect.check --files fx/scanlines.effect.omui.json         # its parameters, or every problem at file:line:column
xrender effect.preview --file fx/scanlines.effect.omui.json --out /tmp/s.png --param strength=0.7   # before | after, by the GPU renderer
xrender conformance.run --report /tmp/sdl.render.cts.omui.json     # the conformance suite on this device
xrender model.look --model Duck.glb --out /tmp/duck.png --light studio.hdr --orbit 0,90,2.7 --target 0,0.86,0 --golden golden.png --follow 64
                                                                   # a glTF model as a viewer frames it; beside another renderer's picture, and the light followed
```

The command is the subject `xrender` behind the tool kernel's entries (mlibs.agent:host): records on standard output, a line each, the data first and the result last; exit 0 done, 1 failed (why, and the ways out), 2 arguments that make no request. `xrender mcp` offers the same operations to an MCP client. The device is SDL_GPU's; a headless machine uses lavapipe (`OMUI_VULKAN_LIBRARY`, `OMUI_GPU_VIDEO_DRIVER=offscreen`).

`model.look` draws with the CPU reference (no device): the model as model-viewer's orbit frames it (`--orbit`, `--target`, `--upright`; with none, all of it from before and a little above), lit by a Radiance picture of the surroundings, four samples a pixel, through Khronos' neutral tone curve — a PNG with nothing where the model is not, or with `--behind` the surroundings there as the eye sees them (a viewer's skybox). `--golden` stands it beside another renderer's picture of the same size and says the distance in model-viewer's render-fidelity measure (`mlibs.image`'s `apart`; that suite holds model-viewer to −22 dB against itself). `--follow RAYS` follows the light way by way a quarter as large each way and says how far the drawn picture, and the golden, are from that: which of two renderers' approximations is nearer the light. [`xrender fidelity`](../../../../`xrender fidelity`) runs the suite's scenarios through it and prints the table kept in [xrender.asset](../asset)'s README. The answer's `model.left_out` is the importer's notes one by one (`where` a JSON pointer into the document, `what`, `instead`).

Its operations are a catalog ([`xrender.ops.omui.ts`](xrender.ops.omui.ts), listed in [`src/catalog`](src/catalog)); how an Agent uses them is [`skill.omui.md`](skill.omui.md). Its own package: a device backend, the conformance suite and image files are a tool's dependencies, not the renderer's.

`fidelity.run [--names NAME …] [--cache DIR] [--out DIR] [--follow RAYS]` runs `model.look` on model-viewer's render-fidelity scenarios (Khronos' sample models with their cameras and HDR surroundings), each in a process of its own, and answers with a Markdown table of each against its golden in the suite's measure (−22 dB its bound). What it reads is fetched once into the cache by `curl` (the suite's list, goldens and surroundings from google/model-viewer, the models from KhronosGroup/glTF-Sample-Assets — run by nothing, put in no repository); a `.gltf`'s files are fetched only where they lie below its own directory. Exit 1 where a scenario asked for by name could not be drawn. It replaced `tools/dev/fidelity.py` (the same table, AGENTS.md §5: a module's tools in C++).
