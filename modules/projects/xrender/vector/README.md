# xrender.vector

2D vector rendering (module `xrender.vector`, D-57), a rendering feature of [xrender](..) (D-81) — its GPU passes are recorded through the render graph: the display list of contract ⑤ (paths, paints, gradients, strokes, clips, transforms, layers with blend modes and effect parameters, glyph runs, images, external textures, node markers), resources by reference (an image store; glyphs from the text engine through `GlyphSource`), and backends. The CPU backend renders through openmui.raster, byte-for-byte the same on every platform, records pixel provenance, and reports what a CPU cannot draw (effects, external textures — spec R-3). Knows nothing of nodes or views.

Path views, command verbs and fill rules are the shared `mlibs.geom` types.
CPU/GPU paths reach rasterization without allocating or copying a second command
array; validation uses the same geometry contract as Hanim's path capture.
Renderer-specific recording, arena ownership and drawing remain here.

**Content-sized image scopes.** The optional final `push_layer` argument
`LayerBoundsMode::content` unions actual source commands with the declared
rectangle/shapes. `LayerBoundsMap` measures transforms, clips, actual glyph masks
and nested output reach once in device coordinates; CPU, GPU and damage share
this implementation. Clipping outside a scope applies when composing its result,
while clips inside it constrain captured content. Reuse the map between calls,
check `measure` before reading entries, and use `at(push_command)` for checked
lookup. Unbalanced/nonfinite input does not publish a valid lookup. Existing
declared-mode hashes and recording remain compatible; new mode survives copies
and morph snapshots. Trace spans use `render.effects`.

The explicit `CpuOptions::effect_scale` is a finite positive output scale for
filter parameters, separate from geometric transforms. Offscreen recursion
preserves it; node scale must not multiply the output DPI again. When omitted,
the initial transform's scale retains the existing public behavior. Content
measurement is not a GPU throughput claim or a replacement for raster timing.

`EffectInfo::preserves_hit_geometry` lets an optional interactive consumer require
a provider to preserve source control identity/location. It defaults to unknown
(`false`); blur, tone and pixelate declare it. This is independent of renderer
availability and does not restrict standalone rendering. Spatial deformation
needs a separate input-mapping contract. Registry changes must occur at quiescent
boundaries. Blur radius zero and tone amount zero now declare exact filter
identity, retaining the ordinary layer's opacity/blending semantics.

**Prepared geometry and materials.** `DisplayList::record_path` writes geometry
directly into the caller's arena; its checked recording can feed a fill and
stroke without copying the path. A cleared, moved or foreign list invalidates
the recording token. `PreparedPaintMix` owns and deduplicates flat leaf paints,
samples into caller storage and records an owned brush in a display list.
The default limit is 16 distinct leaves; overflow is an explicit preparation
error. CPU and GPU mix premultiplied material fields before applying the
current geometry's coverage and clip once. Existing gradient, image and
normal-lighting code is shared. These graphics contracts have no UI dependency;
see the path-recording and paint-mix CTS for lifetime, retarget and readback
checks. Image-store resources retain their existing caller-owned lifetime.

Display-list hashes encode numeric fields in little-endian order through
`openmui.hash`. Command indices, path lengths and material-mix counts use
fixed 64-bit values, so native 64-bit and Wasm32 consumers share the same
content hash. Existing native pins are retained.

**Incremental frames** (`:damage`, D-62). A `DamageTracker` compares a display list with the previous one, segment by segment (the node markers; a segment's hash covers its own commands and its children's order, its bounds are in device space) and returns the bands of rows that can differ. The CPU backend draws only those rows over the previous frame (`CpuOptions::rows`), skipping commands that cannot reach them, and the result is the full render byte-for-byte — `tests/damage_test.cpp` holds it to that for every kind of change (colours, moves with shadows, rotations under rounded clips, clips, layer opacity, z-order, nodes appearing and going, strokes), and the headless host's tests for real frames. On the 1000-node dashboard of M1 ⑤ at 1080p a one-label frame goes from 28 ms to under 2 ms (`tests/bench`). With a `Parallel` (`CpuOptions::parallel`, the host's task pool) the bands are cut into tasks drawn at the same time — each by its own pooled canvas, glyph masks resolved first on the calling thread — and the bytes are still the serial render's: every band is exact — also under contention, several renderers on several threads each drawing in bands at once, effect layers and pictures included (`tests/effects_test.cpp`). `CpuOptions::measure` fills `RenderReport::kinds`: count and time per kind of command (`command_kind_name`), which is where a frame's raster time goes (the `ui/raster/full-1080p` benchmark reports it).


**Lit pictures** (`Material`). A textured mesh — a painted picture cut into parts and skinned on bones — or any image-painted fill can be shaded by the light of the scene it is in: `draw_mesh(image, positions, uvs, indices, sampling, opacity, seam, &material)` (the old signature is the same call without a material, and draws byte for byte as before). A material is a tangent-space normal map aligned with the picture (`openmui.image`'s `normal_map`: a rounded bevel of the silhouette so parts read as volumes, plus painted folds from the luminance; R = x right, G = y down, B = out of the picture) and a stylised light:

```cpp
render::Material skin{.normal = normals, .light = {-1, -1}, .light_z = 0.8f,      // toward the light, screen space, y down
                      .shadow = Color::hex(0x5b4a8a), .shadow_strength = 0.55f,  // multiplied in where unlit
                      .bands = 3, .softness = 0.06f,                             // cel bands, their edges' width in lambert
                      .rim = Color{1, 0.95f, 0.85f, 1}, .rim_strength = 0.9f, .rim_width = 0.45f};
dl.draw_mesh(arm, at, uv, tri, render::Sampling::linear, 1, 0.5f, &skin);
```

The normal map is sampled with the colour's uvs and the same filter; its xy is turned by the orthogonal part of whatever maps the picture to the device — each triangle's own affine and the list's transforms, rotation and mirror (a mirrored part flips the normal's x; a scale or shear is not applied to normals) — so a turned or flipped part is lit by the light, not by its picture. The light is a direction in device space, the same for every part of a figure: lambert n·l is cut into `bands` levels (0 or 1: smooth), each step a linear ramp `softness` wide; the colour is multiplied toward `shadow` by (1 − level) · `shadow_strength`; where n·z is below `rim_width` (an edge's grazing normals) and the normal faces the light in the screen's plane, `rim` (by its alpha and `rim_strength`, the edge as soft as a band's) is added. Premultiplied alpha is kept: the alpha never changes and no channel passes it. A material is copied into the list's arena once per mesh (its triangles share it) and hashed with the paint — a lit list hashes apart from the unlit one; a missing normal map draws unlit and is counted in `missing_images`. `Paint::Kind::image_mask` (and `draw_mesh_mask`) draws a picture's alpha in one colour — a black ink stroke as a rim of light, a silhouette — lit too with a material. The arithmetic (`raster::Shader::lit`, IEEE single precision without contraction) is the CPU reference's, pinned for GCC and LLVM; the GPU draws lit images with a second fragment entry of `ui.slang` (`ui_lit_fs`: the normal map in sampler 4, the material's constants as the CPU computes them — `raster::material_setup` — in a uniform buffer, each instance's rotation in `pgeo`), switching pipelines only where a run of lit draws begins or ends, and gives the CPU's bytes on lavapipe (`../cts/tests/vector_gpu_test.cpp`, the `lit meshes` scene: 0 levels). A lit pixel costs about the price of a second bilinear sample, a square root and two divisions; unlit fills take the same path as before.

**GPU backend** (`:gpu`, D-33). `GpuRenderer::create(device)` builds its pipelines on an `omui.rhi` device (any backend that takes one of `gpu_shader_formats()`); `render(list, target, resources, options)` draws the list into an `rgba8_unorm` colour target and returns the same `RenderReport`. Slang shaders (`shaders/`, `effects/`), compiled at build time by `openmui.shader.rules` into every format the target's backends take, draw every primitive as an instanced quad, cut into batches only where a texture changes.

The GPU works in the CPU's bytes: coverage as raster makes it (rectangles in 1/256 pixel, the rasteriser's masks for paths, strokes, glyphs, box shadows and clips, placed in an atlas), colours premultiplied to 8 bits, gradients read from raster's own 256-entry tables at the same index, images sampled with the same integer weights, every product a rounded `mul255`; source-over is left to the blending unit, which on an exact one gives the CPU's bytes — `../cts/tests/vector_gpu_test.cpp` measures 0 levels of difference on lavapipe for rectangles, gradients, strokes, shadows, clips, layers, images, glyphs and every effect, and holds each scene to a stated tolerance. Rounded rectangles are the exception, and the GPU is the exact one there: it covers a pixel by the true circle's area, where the CPU's chords (1/32 pixel) are within 8 levels. A rounded rectangle's stroke is drawn on both as the area between two rounded rectangles (`stroke_ring`: the outer edge, each radius grown by half the width, and the inner, each shrunk by as much): the CPU fills the two even-odd, the GPU subtracts their exact areas (`KIND_RRING`) — neither strokes a curve, whose inner offset over-covers. A layer under a blend mode is composited by `shaders/blend.slang` over a copy of the target beneath it, with the CPU's arithmetic. On Metal the MSL asks for IEEE arithmetic without contraction (`openmui.shader`), as SPIR-V has it.

**From frame to frame** the GPU keeps what it made. A coverage mask (a glyph, a path, a stroke, a shadow) is kept in the atlas under a 64-bit key of everything that made it — the geometry, the transform, the clip it was rasterised within; a glyph by its font, size and subpixel offset — so a frame rasterises and uploads only the masks the last frames did not have: an interface that did not move sends its instances and nothing else (`RenderReport::masks_made`, `masks_reused`). The atlas grows to 2048 rows freely; past that it is first emptied of what earlier frames left and the frame encoded again (`atlas_emptied`), and grows on only for what one frame draws. Rounded and path clips' masks are kept by key too; an ImageStore image is uploaded once, in one command, its pixels borrowed rather than copied (the store holds them through `render()`), and a texture the store let go is reused for the next image of its size. In the first benchmark report (`docs/en/benchmarks/`) S2's interfaces take 0.2–0.6 ms a frame and S1's zoom sweep a p99 of 5.7 ms on an RTX 4080.

**Effects on the GPU** (D-68). An effect layer is drawn offscreen in a surface of its own — the layer plus the effect's reach, as the CPU prepares it — the effect runs on it, and the result is composited where the layer is. Each built-in has a GPU path held to its CPU reference: per-pixel effects are fragment shaders written against the effect template (`effects/lib/openmui_effect.slang`: the source, an auxiliary row texture, the parameters and values the CPU derives — trigonometry, sizes — so the arithmetic is the reference's; the 64-bit hash in pairs of 32-bit words), blur is box passes in integers, pixelate a compute pass with one invocation a block, particles one instance a cell. `EffectInfo::gpu` says how; an effect without a GPU path is drawn without it and counted in `effects_skipped`, as the CPU counts one without a CPU path. A program's own effects — a `*.effect.omui.toml` and a Slang shader against the same template, no C++ — are [xrender.effect](../effect)'s, loaded and reloaded while the program runs; the template's text is carried in the program (`:effect_template`, generated by `omui-gen embed`). **External textures** (`OMUI_EXT_external_texture`): `GpuOptions::external` maps an `external_texture()` id to a texture the program made on the same device — a video frame it decoded, an engine's view, premultiplied — drawn over its rectangle as `draw_image` would draw the same pixels (bilinear, within a level of the CPU's image of them); an id it does not know is skipped and counted, as the CPU renderer counts every one.

**Backdrop effects and liquid glass** (`OMUI_EXT_backdrop_effect`, D-64). An effect that declares `EffectInfo::backdrop` reads what lies under its layer: the renderer captures it where the layer begins — the CPU renderer copies the target when its draw reaches the layer, the GPU renderer copies the parent target into the effect's aux texture — frosted by `backdrop_blur` with the same integer box blur on both (`box_blur`). A layer carries shapes (`EffectShape`, rounded rectangles in its space; a glass group's members), which the effect reads as signed distances (`shape_distance`, `smooth_min`; the template's `shapes_distance`). A frame with a backdrop effect is drawn by the CPU in one band, and a band that reaches such an effect takes all of its rows; the damage tracker marks a backdrop effect damaged whenever damage reaches it. The built-in `liquid_glass` is one: the backdrop bent at the bevel from the distance's gradient, split by colour, frosted, tinted darker over bright backdrops and lighter over dark ones, lit at the rim facing the light and along a fine inner edge, with the layer's content over it — its CPU reference and GPU path give the same bytes on lavapipe (`../cts/tests/vector_gpu_test.cpp`), and its CPU pixels are pinned for every platform (`tests/effects_test.cpp`).

**The style packs' effects** (S2): `neon` (a glow outside the layer's shapes, a bright tube at their edge, a sweeping band of light), `crt` (barrel curvature, scanlines — one factor a row, the aux texture on the GPU —, an aperture grille, a vignette; black beyond the glass), `paper` (a warm tone, a seeded grain, darker edges), `wobble` (displaced by a smooth value noise and inked where the alpha changes: a hand-drawn line; a new seed, a new drawing) and `metaball` (the layer's shapes merged by `smooth_min`, the bridges filled). Each has a CPU reference and a GPU path in the same operations: on lavapipe they give the same bytes within a level (metaball within the rounded rectangles' allowance, its content being two), and their CPU pixels are pinned.

**`tone`**: the layer in one colour — each pixel's brightness (Rec. 709 luma) kept, its hue the tint's, mixed by `amount` (a fighting game's world gone grey while time stands still, a memory, a page not in use, whatever is drawn in it: shapes and pictures alike). Its CPU reference and GPU path give the same bytes on lavapipe, and its CPU pixels are pinned.


**Pixel grid and strength.** `pixelate` retains its original `size` parameter
and adds `amount` (default 1, range 0–1), `device_pixels` (default 0; values
at least 0.5 select device units), and `origin_x`/`origin_y` (default 0).
Size and origin offsets use the selected unit; the origin is relative to
the layer's corner, with the grid aligned to output axes. Exactly zero amount
is an identity even at fractional/high DPI; size 1 in logical units is not.
CPU and GPU average premultiplied bytes, then mix source and average with
the same 1/255 strength. The original full-strength defaults retain the
pinned pixels for fully captured layers.

`EffectInfo::identity` lets a plugin declare an exact no-op without backend
name checks. The renderer keeps the ordinary layer's opacity/blend and child
effects. `EffectInfo::input_radius` separately declares the device-pixel
neighbourhood needed when cropping input to a viewport. Pixelate retains a
full cell around visible pixels, so independently cropped regions match the
full render. This does not enlarge the layer's capture or output footprint;
`reach`/`output_reach` keep those responsibilities. Both backends consume
the same metadata. New callbacks are optional for existing plugins.

The public `hanim.effect` pixelate recipe and UI named effect channels consume
these parameters; neither owns another pixelation algorithm. CPU tests cover
strength/alpha, unit conversion, phase, exact identity, nested effects and
viewport cropping; desktop CTS exercises actual offscreen GPU readback.

**Test platforms.** The renderer and its portable CPU suites declare Linux,
macOS, Windows and Emscripten. Actual SDL GPU cases live in the desktop-only
[render CTS](../cts): graph passes, vector CPU/GPU comparisons, material mixes
and effect output bounds. Native workspace CI still runs all of them with the
existing required-device policy. Web CI runs every serial CPU case and image
pin here; only thread-only cases are selected by `omui::testing::threads`.
No rendering algorithm, shader or pinned image hash changes in this split.

## Corners with continuous curvature

`CornerRadii::smoothing` (0 circular … 1; iOS's corners are about 0.6) gives rounded rectangles continuous curvature where edge meets corner: each corner eases in on a cubic from `(1 + s)·r` along the edge, follows a circular arc shortened to `90°·(1 − s)`, and eases out (Figma's corner smoothing). The outline is one function, `mlibs::geom::rounded_rect`, written into any path builder: the vector `Path`, the CPU raster's, the GPU's masks and the SVG writer all draw the same curve. The CPU fills it as a path; the GPU, which draws circular corners analytically, covers a smoothed one through the path's coverage mask — CTS scene `smoothed corners`, within one level of the CPU. Circular corners (smoothing 0) are the very path and hash they always were. omui's themes and style packs set it (`corner_smoothing`), `Theme::radii(r)` gives a component its corners.

## The picture as SVG

`xrender.vector.svg` writes a display list as SVG: the same picture as vectors, at any size, the same bytes on every platform.

```cpp
import xrender.vector.svg;
auto picture = xrender::svg::write(list, resources, {.view = {0, 0, 800, 600}, .encode_png = png});
// picture->text: the SVG; picture->report: shapes, glyphs, images, islands
```

- **SVG's own elements** for rectangles and rounded rectangles, paths (fill rules), strokes (caps, joins, miter limits, dashes; trims as a dash whose pattern is one contour long, measured on the stroker's own flattening, wrapping as the renderers wrap (not `pathLength`, which resvg and Skia's SVG module ignore)), clips (rectangles, rounded, paths), transforms, layers' opacity and blend modes, solid, linear and radial paints, outer box shadows (blur and spread as CSS: σ = blur / 2, shown outside the box only), images (each defined once, nearest sampling kept), and glyphs as their outlines from the `GlyphSource` (each glyph defined once, placed where the renderers place it, its own style's transform, opacity and tint) — so text needs no font where it is read.
- **Islands of pixels** where SVG cannot say the same thing: a layer's pixel effect (the `opacity` effect excepted: it is the group's opacity; drawn alone; one that reads its backdrop, with what lies under it, cut to its layer), conic, image and mixed paints, the erase blend, inset shadows, strokes both trimmed and dashed, and colour glyphs. The CPU reference draws them at `island_scale` and they sit in place as PNG, marked `data-island="<why>"` (an effect's also `data-effect="<name>"`) and listed in the report. A GPU texture (`external_texture`) keeps only its place (`data-external`).
- **Provenance:** node markers are groups marked `data-node` (the host names them: omui gives each node's path) and paint parts `data-part`/`data-part-id`, so an agent reading the picture knows what drew each piece.
- **No codec here:** PNG for images and islands comes from the caller (`Options::encode_png`, e.g. `mlibs::image::encode_png`); without one they are left out and counted.

Fidelity, measured (tests/bench/competitors): Skia drawing xrender's SVG of each benchmark scene gives the same picture as Skia drawing the original canvas calls (the same differences from xrender's pixels to the last digit), and resvg drawing it is within 0.11–0.84 per channel of xrender's own pixels. The component gallery's SVG (45 KB) drawn by resvg differs from its pinned pixels by 0.18 per channel on average, with no fonts given to resvg. omui offers it as `capture` with `format` `svg` (`omui agent capture --format svg`).

## Independent boundary and compatibility

This substantial rendering feature is independently consumed by Hanim, text,
UI hosts and native tools. It owns DisplayList, paints, resources, damage and
CPU/GPU rendering, and records GPU passes through the shared graph. Its separate
manifest keeps this feature and shader generation out of graph-only users; its
internal passes, paints and individual effects stay source modules in this one
package. It does not require the optional dynamic effect loader or desktop CTS.

Use `import xrender.vector;` and `xrender`. Old
`openmui.render.vector`/`omui::render` imports are same-package forwards with the
same type and effect-registry identities. The `openmui_effect.slang` template
name, effect IDs and `*.effect.omui.toml` format stay unchanged. The embedded
template and shader partitions now belong to `xrender.vector.api`.

The built-in `opacity` image filter exposes `amount` in 0..1 (default 1).
CPU and GPU use the existing premultiplied-byte scaling helpers; no independent
colour algorithm or UI-specific rendering path is introduced. It is row-local,
has zero input/output reach and preserves source hit geometry. Amount 1 is an
exact filter identity, retaining the enclosing layer's own opacity, blend and
clip; amount 0 is transparent output, not an identity. Existing built-in refs
retain their order because this effect is appended.

This is group compositing: overlapping opaque children at half group opacity
remain half opaque in their overlap. Fading each paint separately produces a
different image. Input visibility and semantic lifetime belong to the host.
The effect uses the ordinary image pass/texture path; no new pass fusion or
zero-cost layer claim is made. CPU process samples and the real-device CTS
cover endpoints, intermediate amounts and fractional/high DPI.
