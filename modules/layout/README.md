# openmui-layout

A layout engine (module `openmui.layout`): flexbox, grid, z-stacks and absolute positioning in a CSS subset with border-box sizing — wrap, grow/shrink with the §9.7 freeze loop and the automatic minimum size, stretch and baseline alignment, justify and align-content, reversal; grid placement (explicit, negative and spanning lines, row/column auto-flow) and track sizing (fixed, %, fr, auto, min/max-content, minmax); aspect ratio, min/max sizes, scroll containers.

The engine keeps its own nodes and caches every node's sizes per constraint, so a change dirties only the node and its ancestors. Frames are unrounded logical pixels (the renderer snaps edges with `geom::snap`); compiled without floating-point contraction, the same tree gives the same frames on every platform — the tests pin a hash of 300 nodes' frames that GCC, LLVM and the Windows target reproduce.

```cpp
layout::Engine e;
auto root = e.create({.direction = layout::Direction::column, .row_gap = 8});
auto title = e.create();
e.set_measure(title, measure_text, &paragraph);   // content size of a leaf
e.append(root, title);
e.compute(root, {1280, 720});
Rect f = e.frame(title);
```
