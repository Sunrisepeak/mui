# openmui.rhi.sdl

The first [omui.rhi](..) backend: a thin mapping onto SDL_GPU (`compat.sdl3`). SDL_GPU runs on Vulkan (Linux, Windows, Android; lavapipe on a machine with no GPU), Metal (macOS, iOS) and Direct3D 12 (Windows).

```cpp
import openmui.rhi;
import openmui.rhi.sdl;

auto device = omui::rhi::sdl::create({.formats = omui::rhi::formats({omui::rhi::ShaderFormat::spirv, omui::rhi::ShaderFormat::msl})});
if (!device) fall_back_to_cpu(device.error().message);   // Errc::unsupported: no GPU for these formats
```

- **SDL picks the backend** that takes one of the formats the program's shaders carry (`Options::formats`, from [openmui.shader](../../shader)'s `formats_for`). `Options::backend` insists on one. Each shader uses the format its backend prefers: SPIR-V on Vulkan, DXIL then DXBC on Direct3D 12, metallib then MSL on Metal.
- **One submission is one SDL command buffer.** Every upload's bytes are staged in one mapping of a cycled transfer buffer, packed tightly and 512-byte aligned. Copy passes open and close around the render and compute passes in the list's order, and uniforms go to the command buffer. The list was already validated (rhi), so the translation checks nothing twice.
- **A fence per submission**; `wait` and `wait_idle` release them. Readback goes through a reusable download buffer and waits for its own fence.
- `Caps` names the adapter and says whether it is a software one (lavapipe, WARP, SwiftShader).
- SDL's headers stay in `src/native/device.cpp`. SDL's video subsystem is initialised while a device lives; on a machine with no display, set `SDL_VIDEO_DRIVER=offscreen`. `Options::vulkan_library` names a Vulkan loader, such as xim's for lavapipe. The offscreen driver makes no Metal views, so a headless device on macOS names a driver that does (`Options::video_driver = "cocoa"`).
- **A window**: `Options::window` (an `SDL_GetWindowID`) is claimed for the GPU when the device is made, its swapchain in sync with the display (`Options::vsync`) or immediate; `present(frame)` blits the frame into the next swapchain texture — SDL converts to the swapchain's format and scales to its size — in a command buffer after the frame's own work.

The tests (`tests/sdl_test.cpp`) run on the machine's device: its backend and formats, upload, clear, copy and readback byte for byte, a buffer at an offset, and an invalid list refused before SDL sees it. Without a device they say so and pass, unless `OMUI_REQUIRE_GPU=1` (CI with lavapipe) makes that a failure.
