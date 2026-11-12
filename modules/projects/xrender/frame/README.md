# xrender.frame

The frame's entry (D-162; the plan's §5.6): a target and the layers drawn into it in order. Every producer of content hands its layers here — omui's hosts the interface's display list, a game with no interface its world's, a batch its pictures — and the composition, the capture and the way from a pixel back to what drew it are the renderer's, the same for all of them. Nothing above the renderer composes on its own.

```cpp
import xrender.frame;

xrender::CpuFrame frame;
const std::array layers{xrender::FrameLayer{.name = "world", .list = &world, .resources = {&images}},
                        xrender::FrameLayer{.name = "ui", .list = &interface, .resources = {&ui_images, &glyphs}, .object_ids = true}};
frame.render(layers, surface, {.background = black});
frame.at(120, 80);   // {layer 1, object 17}: the topmost layer that keeps ids, and its object there
```

| | |
|---|---|
| Target | What its owner gives: a surface (`CpuFrame`, drawn by the CPU reference) or a colour target on a device (`GpuFrame`, drawn by the device's renderer; the host presents it or reads it back). The renderer makes no window and knows neither an interface nor a game |
| Layer | A source of content, in order from the back: a display list with the resources its commands name, its scale and the transform it starts under; `object_ids` keeps which of its objects drew each pixel |
| Composition | The first layer over the background, each next over what is there: the bytes of the reference renderer drawing each list in turn. Only some rows when the caller knows which changed (`FrameOptions::rows`, a damage tracker's) |
| Out | The surface itself (shown by its owner); `frame.capture` as a PNG; `CpuFrame::at` and `frame.at` from a pixel back to the layer and its object |

- **A layer's objects are its producer's**: an interface's are its nodes' indices (omui turns one into a node, its ancestors and where each was built: `ui.drew`), a world's its entities'. The frame says which layer and which object; what the object is, its producer says.
- **Every host goes through it**: omui's headless host, its desktop host (SDL: the CPU's frame and the device's), the Web's (WebGPU, WebGL2, the CPU), Android's, and the development host drawing an application's list — none calls a renderer itself.
- **One compositor a target, kept from frame to frame**: each layer's renderer keeps its ids and its scratch, so a partial render (some rows) leaves the rest as it was.

## Agent surface

The frame is a `Subject` of the agent kernel ([mlibs.agent](../../../base/agent)): `FrameSubject`, shown the compositor and the surface (`show`). Its operations are a catalog, [frame.ops.omui.ts](frame.ops.omui.ts) (the table `src/catalog.cppm` and the listings `src/frame.ops.json`, `.md` are `omui-gen ops`'s); how to use them, in order, is [skill.omui.md](skill.omui.md).

| | |
|---|---|
| Subject and addresses | `frame`, interface `xrender.frame@0.1`. A pixel is `[x, y]` from the top left in the target's pixels, a region `[x, y, width, height]`; a layer is its name and its index from the back |
| State | The last frame rendered: its pixels and what each layer drew. The hash covers the size and every pixel — the same picture, the same hash |
| Observation | `frame.layers`: the size, the hash, each layer's name, how much it drew and what it could not (missing images, glyphs, effects, external textures); `frame.capture`: the pixels as drawn, whole or a region, as a PNG (at most 4 megapixels a request) |
| Source | `frame.at`: a pixel's colour and the chain from it back — the topmost layer that keeps ids, and its object there; empty where no object drew |
| Budgets and tiers | `frame.capture` by region: ask for the part in question, not the whole |
| Not here | **Control, time, preview, history**: a frame is a function of the layers it was given — what changes it is its producers' subjects (`ui`, `world`), and so is their time. **Causes** beyond the chain to an object: why the object is there is its producer's. **A device's frame as the subject** (`GpuFrame`'s target read back for `frame.capture`, its ids for `frame.at`) and **a 3D layer's id buffer** come with the mesh feature (M6.3): on a device the subject is not shown yet, and a pixel's source is the interface's (`ui.drew`, by the reference renderer) |

```sh
mcpp test -p xrender.frame
```
