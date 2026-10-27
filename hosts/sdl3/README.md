# openmui.host.sdl3

openmui on the desktop, through SDL3 (`compat.sdl3` from mcpp-index):

- a window at the display's scale (logical pixels everywhere above the host);
- mouse (click counts, capture), touch, wheel (notches and trackpad pixels),
  keyboard (layout keycodes, so shortcuts follow the layout; Command is the
  primary modifier on macOS), text and input methods (the preedit drawn by
  openmui, the candidates at the caret);
- the system clipboard and cursors (an I-beam over text fields);
- the CPU renderer's frames streamed into a texture, presented only when the
  display list changed (the GPU renderer replaces it at M4);
- a loop that sleeps until an event, the next timer or an agent's request
  (`Desktop::wake` from other threads), and paces animation by vsync;
- the agent endpoint (`openmui.tree@0.1`), with capture of the shown frame.

```cpp
import openmui.host.sdl3;

int main() {
    omui::Desktop d({.title = "Notes", .app = "org.example.notes"});
    d.mount("main", notes());
    return d.run();
}
```

Frames are drawn by the GPU renderer (openmui.render's `GpuRenderer` on an
omui.rhi device that claimed the window) into a texture the swapchain shows,
or by the CPU renderer into a surface streamed to an SDL renderer's texture —
the same pixels, which openmui.render's tests hold together.
`DesktopOptions::renderer` says who: `automatic` (the default) uses the GPU
when the machine has one and the CPU renderer when the only device is a
software rasteriser (lavapipe, WARP), which the CPU renderer outruns; `cpu`
and `gpu` insist. `DesktopOptions::effects` names a directory of
`*.effect.omui.toml` (openmui.effect): loaded with the window, loaded again
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
