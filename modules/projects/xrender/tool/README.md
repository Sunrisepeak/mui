# xrender.tool

The `xrender` command: the renderer without a GUI, for people, agents and CI.

```sh
xrender device                                                     # backend, device, formats, limits
xrender effect check fx/scanlines.effect.omui.json                 # its parameters, or every problem at file:line:column
xrender effect preview fx/scanlines.effect.omui.json --out /tmp/s.png --param strength=0.7   # before | after, by the GPU renderer
xrender cts --report /tmp/sdl.render.cts.omui.json                 # the conformance suite on this device
xrender look Duck.glb --out /tmp/duck.png --light studio.hdr --orbit 0,90,2.7 --target 0,0.86,0 --golden golden.png --follow 64
                                                                   # a glTF model as a viewer frames it; beside another renderer's picture, and the light followed
```

One JSON answer on standard output; exit 0 done, 1 failed (said why), 2 a wrong command. The device is SDL_GPU's; a headless machine uses lavapipe (`OMUI_VULKAN_LIBRARY`, `OMUI_GPU_VIDEO_DRIVER=offscreen`).

`look` draws with the CPU reference (no device): the model as model-viewer's orbit frames it (`--orbit`, `--target`, `--upright`; with none, all of it from before and a little above), lit by a Radiance picture of the surroundings, four samples a pixel, through Khronos' neutral tone curve — a PNG with nothing where the model is not, or with `--behind` the surroundings there as the eye sees them (a viewer's skybox). `--golden` stands it beside another renderer's picture of the same size and says the distance in model-viewer's render-fidelity measure (`mlibs.image`'s `apart`; that suite holds model-viewer to −22 dB against itself). `--follow RAYS` follows the light way by way a quarter as large each way and says how far the drawn picture, and the golden, are from that: which of two renderers' approximations is nearer the light. [`tools/dev/fidelity.py`](../../../../tools/dev/fidelity.py) runs the suite's scenarios through it and prints the table kept in [xrender.asset](../asset)'s README.

Its operations are a catalog ([`xrender.ops.omui.ts`](xrender.ops.omui.ts), listed in [`media/ops`](media/ops)); how an Agent uses them is [`skill.omui.md`](skill.omui.md). Its own package: a device backend, the conformance suite and image files are a tool's dependencies, not the renderer's.
