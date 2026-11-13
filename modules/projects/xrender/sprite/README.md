# xrender.sprite

Pictures on bones in two dimensions: the renderer's `sprite` feature. Its first part is 2D skinning — a picture's region laid on a bone as a grid of cells, each corner going over to the next bones along across their joints. It makes corners, texture coordinates and triangles; `xrender.vector`'s `draw_mesh` draws them.

```cpp
import xrender.sprite;
namespace sprite = xrender::sprite;

sprite::Mesh mesh;                                                   // kept: grown once
const std::array<sprite::Posed, 1> bends{{{forearm_drawn, forearm_now}}};
sprite::skinned({.source = sleeve, .anchor = shoulder, .tip = wrist},  // the art: a region and its line on the bone
                {.columns = 6, .rows = 2, .blend = 16, .length = 80}, // the grid; fitted to the bone's length
                {upper_drawn, upper_now}, bends, mesh);
list.draw_mesh(picture, mesh.at, mesh.uv, mesh.index, xrender::Sampling::linear);
```

| | |
|---|---|
| **A region on a bone** | `Region`: the picture's rectangle (pixels), an `anchor` that sits on the bone's joint and a `tip` along it. Fitted (`Grid::scale` 0), anchor to tip is the bone's `length`; else `scale` units a pixel. `mirror` reflects the art across its line; `facing` −1 turns the whole figure |
| **A grid** | (`columns` + 1) × (`rows` + 1) corners, a row at a time; two triangles a cell |
| **Bends** | Each corner is placed in the rest pose, then taken over by each bend bone in turn: not at all before the bend's joint, wholly past it, across `blend` pixels of the art between. How far toward a bend is measured along the part's own bone, so a rest pose that is itself bent does not split the part across its width |
| **A cel** | `cornered(source, placed, mesh)`: a region as one cell whose four corners the caller placed |
| **Deterministic** | A function of what it is given: `mlibs.math`'s deterministic sines, no fast-math, no state. The corners of a bent sleeve are pinned by their bits (`tests/sprite_test.cpp`); a mesh grown once is not grown again |
| **Its consumer** | `gongfu.present2d`: its fighters' drawn parts and cels are skinned here (it had the same arithmetic as its own until 2026-11-14; its 29 cases and their pinned lists and pixels did not change with the move) |

## Agent surface

A library: a function of a region, its grid and the bones' places. It has no state to observe and nothing to control; what is drawn from it is traced by the frame's subject ([xrender.frame](../frame)) as any display list's content is.

## Not yet

- **Batching and atlases**: many sprites of one picture as one draw, and pictures packed on a sheet, are the rest of the feature.
- **Weights of a corner's own**: a corner's share of each bone comes from where it lies along the part's bone; weights painted a corner at a time are not read.
- **On a device**: the corners are made on the CPU each frame and drawn as a textured mesh.

```sh
mcpp test -p xrender.sprite
```
