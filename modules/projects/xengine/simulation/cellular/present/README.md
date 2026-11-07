# xengine.simulation.cellular.present

A cellular field on screen as one image (D-132): its cells coloured by a palette into one image of an xrender `ImageStore`, a pixel a cell, drawn by one command however large the field — and only the tiles that changed recoloured, re-uploaded and redrawn.

```cpp
import xengine.simulation.cellular.present;
namespace ca = xengine::simulation::cellular;

auto view = ca::FieldPresenter::make(store, shape, channels, palette);   // the image is in the store
program.step(shape, cells, next, {.changed_tiles = mask});              // the step says which tiles changed
view->present(next, mask);                                              // or view->present(next): found by comparison
view->draw(list, {x, y, width, height});                                // one DrawImage, nearest sampling
```

- **Palette.** The first swatch whose tests a cell passes (each a channel and a range) gives its colour; none: the fallback. `recolor` takes a new palette: everything is recoloured at the next present.
- **Only what changed.** A present recolours the tiles the step's mask marks (`Execution::changed_tiles`, or-ed over several steps) — or, given only the cells, those that differ from the cells presented last — and updates the image in place (`ImageStore::update`) naming exactly those rectangles. The GPU renderer uploads only them; the CPU renderer's `DamageTracker` redraws only their rows, and the incremental frame is the full one byte for byte.
- **Immutable while drawn.** Two surfaces take turns: the one the store holds is never written. One still held elsewhere (a snapshot on another thread) is left as it is and a new one made.
- **Lifetimes.** The image stays in the store until `release()`; a presenter that outlives its store never touches it.

**On the device.** A field stepped on the GPU (`xengine.simulation.cellular.rhi`'s `Kernel`) is painted there too: `Painter::prepare(device, view->pixels(), view->fallback_pixel(), channels)` takes this presenter's palette as pixels, `record(list, cells, texture)` writes each cell's pixel into an rgba8 texture, and the GPU renderer draws it as one external texture (`list.external_texture(id, dest, 1, Sampling::nearest)`, `GpuOptions::external`) — the field never read back. Both paths keep one rule (`pixel_of` in the cellular core); the backend's test (`xrender.rhi.sdl`'s `field_present_test`) draws a field both ways every ten ticks and finds the same pixels.

The renderer knows no cells and the rules know no drawing: this package is the small adapter between them.

Tests (`mcpp test -p xengine.simulation.cellular.present`): every pixel its cell's colour after 60 steps, only the changed tiles recoloured and named; drawn with damage tracking, the incremental frame equals the full one at every step; a held snapshot unchanged, a new palette, refusals. Benchmarks: `simulation/cellular-1024-present` (12 changed tiles of 1024: 0.044 ms p95), `-present-all` (every cell: 2.7 ms), `-frame` (present, damage and the incremental CPU raster: 0.52 ms) — locally, release.
