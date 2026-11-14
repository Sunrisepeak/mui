# xrender — skill

How an Agent (or a person at a terminal) works with the renderer without a GUI: writes a pixel effect, checks it, looks at it drawn by the real GPU renderer, and checks the device against the CPU reference. The operations are the catalog's: [`xrender.ops.omui.ts`](xrender.ops.omui.ts), listed in [`media/ops/xrender.ops.json`](media/ops/xrender.ops.json) and [`media/ops/xrender.ops.md`](media/ops/xrender.ops.md). Each is the `xrender` command (`effect.check` is `xrender effect check FILE…`, `conformance.run` is `xrender cts`): one JSON answer, exit 0 done, 1 failed with why, 2 a wrong command. Headless machines use a software device (lavapipe: `OMUI_VULKAN_LIBRARY`, `OMUI_GPU_VIDEO_DRIVER=offscreen`, as the tests do).

## Before

1. `device.describe`: which device draws (a software one is slower, never less exact).
2. An effect is two files: `<name>.effect.omui.ts` (its name, parameters with ranges, reach; the tool reads its JSON: `omui-gen snapshot`) and its Slang fragment shader written against the effect template ([xrender.effect](../effect/README.md)). Read an existing one first.

## Doing

1. Write or change the two files.
2. `effect.check` them: every problem comes at its file, line and column, slangc's included. A shader that binds more than the template offers is refused.
3. `effect.preview` with `--param` values across each parameter's range, on the test card and on a picture that matters (`--image`). Look at the before | after strip: edges, flats, gradients, fine lines.

## Verifying

- The checks are clean, and the strip shows the change asked for — at the parameter's ends, not only its default.
- A renderer change: `conformance.run` passes on this device (every case within its tolerance of the CPU reference); failing cases leave both pictures to look at.
- Record the commands and the pictures' paths.

## A model beside another renderer's picture

1. `model.look` with the model, `--light` an HDR picture of the surroundings, and the other picture's camera (`--orbit`, `--target`, `--upright`, `--size`): look at the PNG written.
2. With `--golden`: the distance in the render-fidelity suite's measure, and how many of the golden's pixels were not drawn — more than a rim's worth says the camera is not the same, not that the shading differs — or that the golden shows the surroundings behind the model (a skybox): then `--behind`.
3. Where the distance is over the suite's bound (−22 dB), add `--follow` (64 rays a pixel is a first look, more for a verdict): the drawn picture and the golden each beside the light followed way by way. The one nearer it is the better approximation; the distance between the two alone does not say which.

## Not here

Drawing an application is its host's (its `capture`); the renderer's API is the library's. Effects are compiled by mlibs.shader.
