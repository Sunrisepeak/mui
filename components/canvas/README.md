# openmui.ui.canvas

An infinite canvas (module `openmui.ui.canvas`, canvas-markdown §1, `OMUI_EXT_canvas`): a scene of nodes in world space — thousands of windows, notes, shapes — seen through a camera. It is the mechanism; selection, dragging, handles, snapping, connectors and the minimap are canvas-kit's. Contract-first: `contract/canvas.contract.omui.toml` is its interface.

```cpp
import openmui.ui.canvas;

auto& wall = omui::world::scene("wall");                      // scenes by name, as models are
wall.kind("note", {.tiers = {{0, omui::world::Tier::Draw::box},     // far: a box
                             {120, omui::world::Tier::Draw::custom}, // nearer: the kind draws it
                             {500, omui::world::Tier::Draw::promote}},// close: a live component
                   .draw = draw_note, .promote = note_view, .role = omui::Role::article});
wall.add({.kind = "note", .x = 120, .y = 80, .width = 240, .height = 160, .key = "todo"});
omui::canvas({.id = "board", .scene = "wall", .zoom = zoom});   // x, y, zoom bind two-way
```

- **The scene** (`world::Scene`) stores nodes by column — world rectangles (x, y in double precision; w, h in float), kinds, keys, fills, corner radii, and the number and text columns a kind reads — so a frame touches only what it reads and a script hands over ten thousand nodes in one call (`push`). A uniform grid indexes them: a viewport query costs what it returns (`tests/canvas_test.cpp` checks it against a scan of 20,000 nodes moving and going; `tests/bench`: panning 100,000 nodes costs what panning 1,000 does).
- **The camera** (`world::Camera`) is the world point at the centre and a zoom, in double precision; drawing is camera-relative, so a wall a billion units out is as crisp as one at the origin. It pans by dragging or a trackpad, zooms about the cursor (the wheel, a pinch of two pointers, ⌃/⌘ with a trackpad), glides after a fling (`world::Inertia`, exponential, it stops), and moves to views on springs (`world::CameraMotion`, logarithmic in zoom) — all functions of the runtime's clock, so a headless run is exact.
- **Tiers** (§3.2): a kind says how a node looks from each size on screen up — an instanced rounded box, its own drawing in screen coordinates, or a live component it is promoted to (the focused node one tier early). A live node is laid out at its world size and shown under the camera's scale, so it never reflows while the camera zooms, and it moves with the camera in the same frame (camera steps run at the start of the frame, before layout).
- **Textures** (§3.2 T1–T2, §3.3): a kind's `content` — the node drawn in its own world units, built on the UI thread — is rasterised on the shared task pool at the mip level the screen's resolution asks for (`world::Textures`), most important first (screen area, nearness to the centre), a few jobs at a time. Until that level is in, the nearest ready one is stretched (a fast zoom shows something at once and sharpens when it stops); before any is, the node is a box. Finished textures enter the image store within a per-frame budget of bytes (`upload_mb`: what a GPU renderer uploads, once per image), and are kept within a memory budget (`cache_mb`), least recently drawn out first. A node's content revision (`Scene::touch`) makes its textures stale; the stale one is drawn until the new one is in. The scene tells its canvases of every change (`Scene::observe`), so they draw again, coalesced to one repaint.
- **Semantics** (`OMUI_EXT_canvas`): the canvas states the nodes on screen (and the focused one) as its items — `<canvas>/<key>`, the kind's role, a name from its column, bounds — and keeps the rest a query away: `find` searches the whole scene by rectangle, nearness, name, kind and role. The viewport's actions: `pan_to`, `zoom_to`, `fit` (`all` or a key), `focus_node`, `camera_preset` (the scene's named views).
- **Cost** (`tests/bench`, budgets in `budgets.bench.omui.json`): a frame of panning or zooming a wall of 1000 windows, or of 100,000, under 1 ms of CPU (raster aside); measured: about 0.02 ms. After the camera stops at a new level, the textures on screen are sharp again in about 1 ms on the CI machine (`canvas/sharpen`, budget 300 ms).
