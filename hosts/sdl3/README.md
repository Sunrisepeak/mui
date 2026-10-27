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

SDL's headers appear only in `src/native/sdl.cpp`. Tests run on SDL's
offscreen video driver with software rendering: what the window shows is read
back and compared with the CPU frame; input is synthesized into SDL's queue
and goes through the same translation as a person's.
