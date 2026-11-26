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
  `GamepadReader`; the device's events released before it goes), started
  once the application takes their input (a component type that says
  `gamepads`, a `gamepad_*` handler, `Runtime::want_gamepads`): until then
  no devices are watched, and where the system gives SDL no notice of them
  (no `/dev/input`) the idle loop is not woken every three seconds to look;
- the system clipboard and cursors (an I-beam over text fields);
- frames drawn by the GPU renderer into the window's swapchain (omui.rhi on
  SDL_GPU) when the machine has a GPU, else by the CPU renderer and streamed
  into a texture — presented only when the display list changed; while the
  device is still being made the CPU draws the first frames, and the device
  takes over when it is ready (UI9);
- a loop that sleeps until an event, the next timer or an agent's request
  (`Desktop::wake` from other threads), and paces animation by vsync;
- the agent endpoint (`openmui.tree@0.1`), with capture of the shown frame.
- the platform's assistive technology served the tree (`omui.accessibility`:
  UI Automation, NSAccessibility, AT-SPI), its adapter attached before the
  window is first shown; what a screen reader asks is performed between
  frames as the person's input (`DesktopOptions::accessibility`);
- the person's appearance and motion as the system says them, read when the
  window opens and followed while it runs (`DesktopOptions::follow_system`, on
  by default): a dark appearance is `Runtime::dark_preferred` — a theme that
  follows it (omui.theme's schemes, an application's `dark_theme`) turns with
  the system —, less motion adds to `reduce_motion`; where the system says
  nothing, light and full motion;
- the window opened where it is asked to be (`DesktopOptions::position`, the
  system's screen coordinates), else where the system places it.

```cpp
import omui.host.sdl3;

int main() {
    omui::Desktop d({.title = "Notes", .app = "org.example.notes"});
    d.mount("main", notes());
    return d.run();
}
```

Frames are drawn by the GPU renderer (mrender.vector's `GpuRenderer` on an
omui.rhi device that claimed the window) into a texture the swapchain shows,
or by the CPU renderer into a surface streamed to an SDL renderer's texture —
the same pixels, which mrender.vector's tests hold together.
The GPU path is a provider's, installed at this package's slot `omui.host.sdl3.gpu`
(`omui::gpu_slot`, `GpuService`; [`mlibs.compose`](../../../../base/compose), a preview,
`lower` in the manifest): [`gpu`](gpu) (`omui.host.sdl3.gpu`) has SDL_GPU's device,
mrender's renderer and the effect library, and `omui.sdk` installs it. A program that installs none —
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
and `gpu` insist.

**The first frame is not the device's to wait for** (UI9, [design](../../../../../.agents/docs/2026-11-27-gpu-window-first-frame-design.md)),
**and a window that does not need a device makes none** (UI10, [design](../../../../../.agents/docs/2026-11-27-window-device-on-demand-design.md)).
An `automatic` window draws its first frames with the CPU into the window's
own framebuffer. The device — and the renderer's pipelines, on the same
thread — is begun only once the window's frames need it
(`DesktopOptions::device_when`, `needed` by default): its first frame, the
whole window, took the CPU more than the display's interval to draw and show
(8.3 ms at 120 Hz), or three of its last ten frames in motion more than half of
it. The window's own framebuffer is sent only the bands of rows that changed,
so a small change costs the CPU little however large the window. A window the
CPU draws well enough — a dialog, a form, hello — loads no driver and has none
of a device's memory or its threads' wakeups; a large one whose full redraw is
dearer than that begins its device right after its first frame, as UI9 did for
every window (`first_frame` asks for that). Either way it is made off the first
frame's path, and its thread wakes the loop when it is done. At rest (the loop asleep, no input come;
a second after it is ready where something keeps moving) the device claims the
window — about 75 ms on an RTX 4080 under X11, SDL syncing the window and the
driver making the swapchain —, the framebuffer is let go, and the CPU's frame on
screen is sent to the device's frame texture as it is (in bands of about
256 KB, so the device's staging buffer is not left written through). The two renderers are
one contract (D-184), so nothing is drawn again at the takeover; from the next
change of the display list on the GPU draws. Where a window keeps no framebuffer of its own
(macOS, Wayland: a CPU frame there needs an SDL renderer the device could not
share the window with), with effects (only the GPU draws them), or with the GPU
insisted on, the window waits for the device as before. `Desktop::gpu_pending()`
says a takeover is to come; `Desktop::gpu()` says who draws now. A device the
window cannot use (a software one, a transparent window SDL_GPU refuses) is let
go and the CPU draws on. `DesktopOptions::effects` names a directory of
`*.effect.xdef.ts` or `.json` (mrender.effect): loaded with the window, loaded again
between frames when a file in it — or a file its shader imports — changes, so
a shader saved while the program runs shows on the next frame; a file that
no longer loads is reported where it is wrong and the effect stays as it was.

**The system's appearance.** On Windows and macOS SDL reads it (`SDL_GetSystemTheme`, and its
`SDL_EVENT_SYSTEM_THEME_CHANGED`). On Linux the build of SDL here has no D-Bus (`compat.sdl3` leaves SDL's D-Bus
stack out), so it says nothing; the host reads the desktop portal's settings through
[`mcpplibs.mpal`](../../../../base/mpal) (`mpal:preferences`, a preview): `org.freedesktop.appearance`'s
`color-scheme` for the appearance, its `reduced-motion` for motion where the portal has it, else GNOME's
`enable-animations` (its "Reduce animation"), which the portal passes on. The reading is a conversation of its
own on a thread of its own, begun when the host is made, so it runs beside the fonts, the runtime and the window
(26–30 ms from the host's making to the window open on this machine's desktop, the read never waited for); the
window's opening waits for it at most 100 ms (a portal being started by the bus) and takes it later if it comes
later. The thread then waits for the portal's `SettingChanged`, asked for by namespace only — no wakeups while
nothing changes — and a change wakes the loop, which hands it to the runtime between frames. Tests that look at
pixels turn it off (`follow_system = false`); `preferences_test` runs a copy of the test program against a portal
of its own (no display, nothing of the machine's session read), and on a person's desktop, when asked
(`OMUI_REAL_WINDOW_AT=x,y`), opens a real window there and holds what it has to what the session's portal says.

**The pointer elsewhere does not wake the window** (red 158). SDL's X11 driver asks the X server at its start for
XInput2's raw motion, raw buttons and raw touches on the root window (`X11_InitXinput2`, 3.4.18 and SDL's main):
the server then sends the program every movement of the pointer anywhere on the screen, and its loop wakes for
each, with nothing to do. SDL needs them only for its relative mode and for the cache of `SDL_GetGlobalMouseState`
(which this host does not call); a window's own input comes through its own selection. Once SDL's video is up the
host clears the root's selection for the master devices (XInput2's `XISelectEvents`, through the libXi SDL loaded
itself) and asks for raw motion again only while the pointer is locked (`Runtime`'s pointer lock, SDL's relative
mode). The fix that belongs in SDL — those events selected only while relative mode or a global mouse query needs
them — is prepared for upstream; until it is released, `compat.sdl3` could carry it as a patch of its own build, and
the host's clearing would then be left out. `root_motion_test` holds it on an X server of the test's own
(`OMUI_TEST_X_DISPLAY`, an Xvfb whose pointer it moves).

SDL's headers appear only in `src/native/sdl.cpp`. Tests run on SDL's
offscreen video driver with software rendering: what the window shows is read
back and compared with the CPU frame; input is synthesized into SDL's queue
and goes through the same translation as a person's. On the machine's GPU
(lavapipe in CI, Metal on macOS; required with `OMUI_REQUIRE_GPU=1`) the
swapchain shows the GPU frame, and an effect's shader edited while the window
runs shows on the next frame — 167 ms from the saved file to the frame on the
Linux CI machine, under the 500 ms M4 asks for (the test measures it).
