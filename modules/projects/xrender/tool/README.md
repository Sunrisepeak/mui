# xrender.tool

The `xrender` command: the renderer without a GUI, for people, agents and CI.

```sh
xrender device                                                     # backend, device, formats, limits
xrender effect check fx/scanlines.effect.omui.json                 # its parameters, or every problem at file:line:column
xrender effect preview fx/scanlines.effect.omui.json --out /tmp/s.png --param strength=0.7   # before | after, by the GPU renderer
xrender cts --report /tmp/sdl.render.cts.omui.json                 # the conformance suite on this device
```

One JSON answer on standard output; exit 0 done, 1 failed (said why), 2 a wrong command. The device is SDL_GPU's; a headless machine uses lavapipe (`OMUI_VULKAN_LIBRARY`, `OMUI_GPU_VIDEO_DRIVER=offscreen`).

Its operations are a catalog ([`xrender.ops.omui.ts`](xrender.ops.omui.ts), listed in [`media/ops`](media/ops)); how an Agent uses them is [`skill.omui.md`](skill.omui.md). Its own package: a device backend, the conformance suite and image files are a tool's dependencies, not the renderer's.
