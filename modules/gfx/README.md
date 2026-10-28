# modules/gfx

Graphics: the GPU abstraction, shaders, the CPU rasteriser and the renderer. Knows nothing of interfaces or games.

Depends on: [base](../base). Used by: [content](../content), [ui](../ui).

| Package | |
|---|---|
| [rhi](rhi) | The GPU abstraction (`omui.rhi`): descriptions checked before a backend sees them, command lists as data; backends and their conformance suite inside it |
| [shader](shader) | Slang shaders compiled per backend format, bindings read back, emitted as C++; its build rule inside it |
| [raster](raster) | The deterministic CPU rasteriser: paths, strokes, gradients, images, clips, layers, blend modes, shadows |
| [render](render) | Display lists, the renderer interface, the CPU and GPU renderers; a program's own pixel effects inside it |

Start with [render](render/README.md).
