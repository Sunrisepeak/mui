# xengine.cellular.sdl

The cellular kernel on a native device (SDL_GPU): the kernel's conformance suite ([xengine.cellular.rhi.cts](../rhi/cts)) run there, and its field on screen from the device's own cells ([present](../present), drawn by the GPU renderer as an external texture, never read back).

The owner of a suite runs it against a backend, never a backend against its consumers (D-161). It is a package of its own because the kernel, its suite and its presenter run on the Web too and this backend does not.

`open(formats)` gives the device this machine has (the Vulkan library and SDL's video driver from `OMUI_VULKAN_LIBRARY` and `OMUI_GPU_VIDEO_DRIVER` when set); with `OMUI_REQUIRE_GPU=1` (CI) a missing device fails the tests, otherwise it is said and they pass vacuously.

```sh
mcpp test -p xengine.cellular.sdl      # lavapipe in Linux CI (tools/ci/gpu.sh)
```
