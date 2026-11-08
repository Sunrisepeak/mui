# modules/gfx

Graphics: the GPU abstraction and the shader toolchain, shared by the renderer and by GPU computation (simulation's compute kernel is [xengine's](../projects/xengine/cellular/rhi)). Knows nothing of interfaces or games. The renderer, the CPU rasteriser and the RHI backends are [xrender](../projects/xrender)'s; hanim is [its own project](../projects/hanim).

Depends on: [base](../base). Used by: [content](../content), [ui](../projects/omui).

| Package | |
|---|---|
| [rhi](rhi) | The GPU abstraction (`mlibs.rhi`): descriptions checked before a backend sees them, command lists as data, and its conformance suite; the backends (SDL_GPU, WebGPU, WebGL2) are [xrender's](../projects/xrender/rhi) |
| [shader](shader) | `mlibs.shader`: Slang shaders compiled per backend format, bindings read back, emitted as C++; its build rule inside it |

Start with [rhi](rhi/README.md).
