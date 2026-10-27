# openmui.host.headless

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

Every level gives the same semantic tree (ids, roles, names, values,
actions); pixels are the same on every platform.

```cpp
import openmui.host.headless;

omui::Headless h({.size = {640, 480}});
h.mount("main", app());
h.click("main/save");
h.settle();
h.save_png("after-save.png");
```
