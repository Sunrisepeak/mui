# omui.host.sdl3

openmui on the desktop, through SDL3 (`compat.sdl3` from mcpp-index):

- a window at the display's scale (logical pixels everywhere above the host);
- mouse (click counts, capture), touch, wheel (notches and trackpad pixels),
  keyboard (layout keycodes, so shortcuts follow the layout; Command is the
  primary modifier on macOS), text and input methods (the preedit drawn by
  openmui, the candidates at the caret);
- gamepads, plugged in and out while it runs, through SDL's mapping
  database to the spec's standard set (§6.1: south/east/west/north,
  shoulders, triggers as axes and buttons, d-pad, sticks; the dead zones by
  `GamepadReader`; the device's events released before it goes);
- the system clipboard and cursors (an I-beam over text fields);
- frames drawn by the GPU renderer into the window's swapchain (omui.rhi on
  SDL_GPU) when the machine has a GPU, else by the CPU renderer and streamed
  into a texture — presented only when the display list changed;
- a loop that sleeps until an event, the next timer or an agent's request
  (`Desktop::wake` from other threads), and paces animation by vsync;
- the agent endpoint (`openmui.tree@0.1`), with capture of the shown frame.
- the platform's assistive technology served the tree (`omui.accessibility`:
  UI Automation, NSAccessibility, AT-SPI), its adapter attached before the
  window is first shown; what a screen reader asks is performed between
  frames as the person's input (`DesktopOptions::accessibility`).

```cpp
import omui.host.sdl3;

int main() {
    omui::Desktop d({.title = "Notes", .app = "org.example.notes"});
    d.mount("main", notes());
    return d.run();
}
```

Frames are drawn by the GPU renderer (xrender.vector's `GpuRenderer` on an
omui.rhi device that claimed the window) into a texture the swapchain shows,
or by the CPU renderer into a surface streamed to an SDL renderer's texture —
the same pixels, which xrender.vector's tests hold together.
The GPU path is a provider's, installed at this package's slot `omui.host.sdl3.gpu`
(`omui::gpu_slot`, `GpuService`; [`mlibs.compose`](../../../../base/compose), a preview,
`lower` in the manifest): [`gpu`](gpu) (`omui.host.sdl3.gpu`) has SDL_GPU's device,
xrender's renderer and the effect library, and `omui.sdk` installs it. A program that installs none —
an application built `lean` — draws every frame with the CPU renderer, as a machine
without a GPU does, and `renderer = gpu` is refused naming the slot and how to have
it. This package's tests draw with the CPU and hold that refusal; the device's are
the provider's (a slot's owner cannot depend on its provider).

Two reductions an application may ask for by this package's features (off by default; `omui.sdk` forwards them):
`no-gamepad-hidapi` (SDL's HIDAPI game-controller drivers) and `software-present` — SDL's accelerated 2D back ends
left out, so CPU frames are presented by SDL's software renderer through the window's surface. A program the GPU
path draws presents through SDL_GPU and loses nothing; one drawing with the CPU presents without the display's
acceleration (not yet timed on a display: only the first frame, offscreen, which is shown as before).

`DesktopOptions::renderer` says who: `automatic` (the default) uses the GPU
when the machine has one and the CPU renderer when the only device is a
software rasteriser (lavapipe, WARP), which the CPU renderer outruns; `cpu`
and `gpu` insist. `DesktopOptions::effects` names a directory of
`*.effect.xdef.ts` or `.json` (xrender.effect): loaded with the window, loaded again
between frames when a file in it — or a file its shader imports — changes, so
a shader saved while the program runs shows on the next frame; a file that
no longer loads is reported where it is wrong and the effect stays as it was.

SDL's headers appear only in `src/native/sdl.cpp`. Tests run on SDL's
offscreen video driver with software rendering: what the window shows is read
back and compared with the CPU frame; input is synthesized into SDL's queue
and goes through the same translation as a person's. On the machine's GPU
(lavapipe in CI, Metal on macOS; required with `OMUI_REQUIRE_GPU=1`) the
swapchain shows the GPU frame, and an effect's shader edited while the window
runs shows on the next frame — 167 ms from the saved file to the frame on the
Linux CI machine, under the 500 ms M4 asks for (the test measures it).
