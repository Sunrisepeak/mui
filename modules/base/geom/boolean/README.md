# mlibs.geom.boolean

Solids combined: two closed meshes joined, one cut from the other, or what they share — a closed mesh again, with the normals, tangents and texture coordinates both carry, the same bytes on every machine (the foundation plan's §5.5: modelling by code; D-176).

```cpp
import mlibs.geom.boolean;
namespace geom = mlibs::geom;

const geom::Mesh block = geom::box({1, 1, 1});
const auto drilled = geom::without(block, geom::cylinder(0.5f, 4, 48));   // the block with a hole through it
if (!drilled) return fail(drilled.error());                              // "the second is not a solid: it is not closed, …"
const auto part = geom::united(*drilled, knob);                           // a result is a solid: combined again
```

| | |
|---|---|
| **What goes in** | Two `mlibs.geom` meshes that are solids: closed, each edge shared by two triangles, fronts outward. Corners that are one place are joined first, so a mesh whose faces each have their own corners (a box's, for its flat normals) is a solid. `closed(mesh)` says whether one is, and why not |
| **What comes out** | A solid again — never a gap, an edge three faces share, or a part inside out — so it can be combined again, measured (`geom::volume`), put in clusters, drawn. Nothing left or nothing shared: a mesh of no triangles |
| **Attributes** | Normals, tangents and texture coordinates that both meshes have are carried: a surface keeps its own, and along the line where two surfaces now meet each has its own corners there — an edge the cut made is as sharp as it is. One only one of them has is left out |
| **Refusals** | Which of the two, and what is wrong: not a mesh (`mlibs.geom`'s `check`), or not a solid |
| **The same bytes everywhere** | The combining decides exactly and its arithmetic is the same on every machine (contraction off, its own sines and cosines); what differs between standard libraries is only the order a sort leaves equal things in, so the corners and the triangles are put in an order that is the result's own — corners by their numbers, each triangle from its least corner, the triangles by theirs. A pinned result holds on GCC with libstdc++ and LLVM with libc++ (`tests/boolean_test.cpp`) |

## How it was designed (D-185)

1. *From this system's own ground.* A result is a value with one form: the canonical order makes the same solid the same bytes, so it can be a key, a pinned test, a thing two machines agree on — which the library alone does not give (its order is the standard library's). Attributes ride through the cut as the library's vertex properties, so a part made by code keeps its flat faces and its texture coordinates without a pass after.
2. *What exists, combined.* The combining is Manifold's (the one library whose booleans are guaranteed to give a solid), through its module wrap ([mcpplibs/manifold-m](https://github.com/mcpplibs/manifold-m), built on one thread with contraction off; D-56, D-83), named in one native unit. A package of its own beside `mlibs.geom`, so the geometry everything shares does not carry it (D-99).
3. *The level of the best.* Against the modellers that give solids (Manifold itself, CGAL's corefinement, OpenSCAD's and Blender's exact booleans): the robustness is Manifold's own; added here are the one byte-form of a result and the refusals that say which and why. Behind: no hulls, offsets, level sets or 2D outlines through this interface yet (the library has the first three; its 2D cross-sections need Clipper2 and are not in the wrap).

## Agent surface

A library of pure functions: no state, no clock. What an agent's question gets from it:

| | |
|---|---|
| Observation | A result is a `Mesh`: its counts, `geom::volume`, `geom::bounds`; `closed` |
| Causes | A refusal names which mesh and what is wrong with it |
| Control | `united`, `without`, `common` (`combined`) |
| Time | None: the same meshes in, the same bytes out |

## Where things are

| | |
|---|---|
| `src/boolean.cppm` | the interface |
| `src/native/manifold.cpp` | the one unit that names Manifold |
| `tests/boolean_test.cpp` | 4 cases: boxes joined, cut and shared; a bored block and its sharp rim; refusals; the same bytes (pinned) |
