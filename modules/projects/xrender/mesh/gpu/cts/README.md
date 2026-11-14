# xrender.mesh.gpu.cts

The mesh pass's contract on a device (`xrender.mesh.gpu@1`): the scenes any backend of `mlibs.rhi` must draw as the CPU reference (`xrender.mesh`) draws them, each within a tolerance its case states. A backend's device is opened by whoever runs the suite; the suite knows `mlibs.rhi`'s interface and nothing of a backend.

```cpp
options.formats = xrender::mesh::gpu::cts::shader_formats();          // what the pass's shaders were compiled to
auto device = backend::create(options);
xrender::mesh::gpu::cts::declare(**device, {.subject = (*device)->caps().device}, {.scratch = directory});
```

| Case | What is held to the reference |
|---|---|
| `mesh.yard` | every draw drawn; the picture within 6 levels at all but a thousandth of its pixels |
| `mesh.fog` | surfaces through fog, and the sky behind them clearing upward |
| `mesh.ids` | who drew each pixel: the reference's draw at all but an edge's pixels; a pixel of the ball is the ball's id |
| `mesh.shadows.sun` | the sun's shadows by a map: the pixels the reference's rays darken |
| `mesh.polished` | highlights and what a metal mirrors |
| `mesh.glow` | light kept before the display's curve, spread, shown; through a tone |
| `mesh.shadows.lamps` | a lamp's and a spot's shadows by six views each |
| `mesh.harbour` | the harbour at night: five lights, four with shadows, a wet quay, fog, glow |
| `mesh.temporal` | sixteen frames gathered, then a thing moved a long way in one: the reference's resolve, kept by the render graph's histories |
| `mesh.open` | how open a place is: darker where the reference's rays find it shut |
| `mesh.textures` | a floor of tiles: sharp near, the mean of its tiles far away |
| `mesh.surface` | the surface's other pictures: roughness and metal, the normal (by the mesh's tangents and, on a mesh with none, worked out at the pixel), emission and shade |

Where it runs, and what it measured there (480 × 270; pixels further than 6 levels from the reference, of 129,600):

| Device | Run by | yard | harbour | gathered | tiles |
|---|---|---|---|---|---|
| lavapipe through SDL_GPU (Vulkan) | `xrender.rhi.sdl`'s `mesh_test` | 5 | 351 | 11 | 591 |
| the page's WebGPU (headless Chromium, SwiftShader) | `tests/mesh_webgpu_test.cpp`, `tools/ci/browser.sh` | 24 | 367 | 128 | 871 |
| the page's WebGL2 (ANGLE on SwiftShader) | `tests/mesh_webgl_test.cpp`, `tools/ci/browser.sh` | 24 | 367 | 128 | 871 |

All eleven cases pass on each. WebGL2 runs the same shaders (ESSL) and the same float targets as WebGPU: no case is lowered for it.

## Agent surface

A library: a conformance suite, for tests only. Its results are mtest's conformance cases (`<device>/mesh.*`), or a tally where one is asked for (`mlibs.cts`).

## Not yet

- **What a device lacks** is not yet said case by case (`needs`): a device without float colour targets fails the cases that use them and is not reported as unsupported.
- **A device's own picture kept** as a pinned image: the cases compare with the reference drawn in the same run, not with a stored picture.

```sh
tools/ci/gpu.sh mcpp test -p xrender.rhi.sdl mesh_test     # this machine's device
tools/ci/browser.sh                                        # the page's WebGPU and WebGL2
```
