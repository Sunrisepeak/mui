# xrender.mesh.gpu

The mesh pass on a device, through `mlibs.rhi` alone: the same scene, the same extraction and the same shading as [xrender.mesh](..)'s CPU reference, held to it pixel by pixel (the foundation plan's §5.1–§5.3, §5.7, D-184: the CPU is the reference, the GPU the same contract's other path).

![the yard drawn on a device: a floor, a red box, a blue ball, a yellow post, a lamp and its light, a spot on the floor](media/yard-device.png)

```cpp
import xrender.mesh.gpu;

auto pass = *xrender::mesh::gpu::Pass::make(device);
pass.load(assets.meshes);                                        // each mesh's corners, once
mlibs::rhi::CommandList list;
pass.record(list, scene, extracted, view, assets.materials, colour, depth, width, height, options);
device.submit(list);                                             // colour: rgba8, sRGB, as the reference's
```

| | |
|---|---|
| **What it takes** | What the reference takes: `xrender.scene`'s scene and what it extracted for the view, the materials, `xrender.mesh`'s `Options` (ambient, background, fog). A device from whoever made it (SDL_GPU, WebGPU): only the interface is known here |
| **One shader** | `shaders/mesh.slang`, compiled at build time for the target's formats (`mlibs.shader.rules`). Suns, lamps and spots (eight a frame; more are counted as left out), matt surfaces and those with a highlight (the reference's `reflected`, term for term; and what such a surface mirrors of the environment; a polished ball and a gold post under a sky: 5 of 129,600 pixels apart ![the yard on a device with a polished ball and a gold post](media/yard-device-polished.png)), a constant light from everywhere, what the surface gives off, fog worked out along the ray, the display's curve. Matrices go a row at a time and are applied by dot products |
| **Held to the reference** | On a backend's device (`xrender.rhi.sdl`'s `mesh_test`): the yard at 480 × 270 on lavapipe — 5 of 129,600 pixels further than 6 levels from the reference, mean difference 0.008 levels; in fog — the sky behind drawn through it along each pixel's ray (`shaders/sky.slang`) — 5 of 129,600 over the whole picture. The bound in the test is a thousandth of the picture and a tenth of a level |
| **The sun's shadows, by a map** | `Shadows`: two textures the caller made (`r32_float` and its depth, 2048² by default) and how far from the eye things are shadowed. The first sun's view of everything that casts — seen by the view or not — is drawn before the frame, over a ball about the near part of the view; a surface lifted two texels along its normal is in shadow where the map saw something nearer. Held to the reference's traced shadows: of 1,002 pixels the rays shadow the map shadows 985, and 71 of 129,600 pixels are further than 6 levels apart — a texel's width along the shadows' edges. ![the yard on a device under the sun alone: the box, the ball and the post each throw a shadow](media/yard-device-shadows.png) |
| **Glow** | `Pass::make(device, reversed, true)` draws onto an `rgba16_float` target and leaves the light as it is, before the display's curve; `Glow::record(list, light, one, other, colour, width, height, bloom)` is the reference's `Glow` in seven passes — what is above the threshold spread along the rows and down the columns three times between two pictures of the caller's, then the light and what spread through the curve onto `colour` (`shaders/glow.slang`; texels read one for one). The yard with a lamp of (9, 5, 1.6) on lavapipe: lighter by the glow over 6,943 pixels (the reference: 6,996), 5 of 129,600 further than 6 levels from the reference's; with no radius, the plain pass's picture. ![the yard with the lamp's halo](media/yard-device-glow.png) |
| **A package of its own** | Not `xrender.mesh`'s feature: its build program compiles a shader, and a build program's imports are not conditional on a feature (D-99; as `mlibs.rhi.cts`) |

## Agent surface

Part of [xrender.frame](../../frame)'s (`agent = { part = "xrender.frame" }`): the picture it draws is a texture of the device, read back as pixels by the frame's own operations.

| | |
|---|---|
| Numbers | `Pass::last()`: draws, draws naming nothing loaded, triangles, lamps and lamps left out |
| Source | `record_ids`: a pass of its own on an `r32_uint` target — each pixel the place of its draw in the frame's draws (`who(pixels, width, x, y)`), from which the scene gives the instance's stable id. On lavapipe 4 of 129,600 pixels name another draw than the reference's id buffer (an edge's). The triangle under a point is `xrender.mesh`'s `pick`, by a ray |

## Not yet

- **Normals, velocity, the triangle of a pixel**: the interface has one colour target a pass; each is a pass of its own (as the ids are), or waits for several targets.
- **Shadows of lamps and spots**: the sun's are here (below); a lamp's needs a map all round it.
- **Faces by winding**: the interface has no culling state; a face turned away is discarded by its normal in the shader.
- **Clusters**: each mesh is drawn whole; leaving clusters out on the device (and levels of detail) is RN5's.
- **The resolve over time** on the device.

```sh
tools/ci/gpu.sh            # a device for this machine (lavapipe on Linux)
mcpp test -p xrender.rhi.sdl mesh_test
```
