# omui.host.sdl3.gpu

The GPU path of a desktop window, for [`omui.host.sdl3`](..): SDL_GPU's device (`mlibs.rhi` over Vulkan, Metal or Direct3D 12, through `mrender.rhi.sdl`) mrender's renderer on it, and the library of a program's effect files (`mrender.effect`, which only the GPU renderer draws), installed at the host's slot `omui.host.sdl3.gpu` ([`mlibs.compose`](../../../../../base/compose); a preview, `lower` in the manifest).

```cpp
import omui.host.sdl3.gpu;
omui::host::sdl3::gpu::install();   // once, before a host is made
```

A package of its own: the host names only the slot (`omui::gpu_slot`, `GpuService`), and a program's root names this package. `omui.sdk` installs it by its feature `gpu`, which the worlds `app` and `full` have; an application built `lean` draws every frame with the CPU renderer — as a machine without a GPU does — and carries neither SDL_GPU's backend nor the renderer's shaders. A host asked to draw with the GPU (`Renderer::gpu`) where none is installed is refused, the refusal naming the slot and how to have it.

`begin` makes the device on a thread of its own and, once it is made, the renderer's pipelines on the same thread (`device.renderer` in a trace), then tells the host (`GpuDeviceOptions::made`): a host drawing its first frames with the CPU takes both over between frames (UI9).

Tested: `tests/gpu_test.cpp` — installed, the slot has it; `tests/host_test.cpp` — the host drawing on the device (the first frame by the CPU, by the window the machine chooses and by the GPU insisted on; the first frames the CPU's while the device is made, the device taking over without drawing the same picture again and drawing it within the pinned bound of the CPU's — 50 levels on text, the CPU's glyph masks at a quarter pixel against the device's outlines where they are, 8 elsewhere; two hosts with one's device still being made, the swapchain and an effect edited while it runs, a picture drawn elsewhere). These live here and not with the host: a slot's owner cannot depend on its provider, so the owner tests the absent path and the provider the installed one.
