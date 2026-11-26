# omui.host.headless

openmui without a display: a runtime on a clock stepped frame by frame,
input injected the way a person gives it (by point, or by id), pixels from
the CPU reference renderer, PNG output, pixel provenance through the
object-id buffer, and — at H3 — the agent endpoint served between frames.

| Level | Runs | For |
|---|---|---|
| H0 `semantics` | build and reconcile | unit tests, agent rehearsal |
| H1 `layout` | plus bounds and hit testing | interaction logic, accessibility checks |
| H2 `pixels` | plus CPU raster, PNG | golden images |
| H3 `session` | plus the agent endpoint | end-to-end tests, agents |

Input is anything a host gives (`step(events)`, or their JSON form in a
recorded session) — a gamepad's too: `GamepadEvent{.kind =
GamepadKind::down, .device = 1, .button = GamepadButton::south}`.

Every level gives the same semantic tree (ids, roles, names, values,
actions); pixels are the same on every platform.

```cpp
import omui.host.headless;

omui::Headless h({.size = {640, 480}});
h.mount("main", app());
h.click("main/save");
h.settle();
h.save_png("after-save.png");
```

Its text measure is the host's to bring (terminal design §9): where the program installed no shaper at [mlibs.text](../../../../content/text)'s slot `mlibs.text.shaper`, it installs HarfBuzz ([mlibs.text.harfbuzz](../../../../content/text/harfbuzz)) before it makes its fonts — so it is a composition root in its manifest (it has no worlds). A program that installed the cell measure gets a headless host that measures in cells. So with what its renderer draws, where none is installed: the pixel effects at mrender's slot `mrender.vector.effects` (`mrender::install_effects()`) universal motion's morphs at omui.view's `omui.view.morphs` (`omui::install_morphs()`) and the PNG reader at mlibs.image's `mlibs.image.decoder` (`mlibs::image::install()`), before it makes its runtime.

The endpoint of a session (H3) is the application's agent surface's: this host reads the slot `omui.agent.endpoint` ([`mlibs.compose`](../../../../base/compose), a preview, `lower` in the manifest) where a session opens, and makes the endpoint and what it captures through its table. A program whose root installed none (`omui::agent::install()`; `omui.sdk` does, by its feature `agent`) has no endpoint at H3, as where local endpoints are not supported. `png()` and `svg()` are whoever calls them's.
