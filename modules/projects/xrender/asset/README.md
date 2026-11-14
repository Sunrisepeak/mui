# xrender.asset

Things made elsewhere, brought in: a glTF 2.0 document as what the scene and the mesh pass are given — meshes in clusters, materials, textures, instances and lights — with a list of what the document said that was not carried (the foundation plan's §5.5, M6.3; D-171).

```cpp
import xrender.asset;
namespace asset = xrender::asset;

const auto model = asset::gltf_file("lantern.glb");          // or asset::gltf(bytes): a .gltf's text or a .glb's bytes
if (!model) return fail(model.error());                      // "not a glTF document: its JSON does not parse"
asset::place(*model, world, {.mesh = first_mesh, .material = first_material});
for (const asset::Note& note : model->notes)                 // /materials/0/normalTexture: a normal map → the mesh's own normals
    log(note.where, note.what, note.instead);
```

| | |
|---|---|
| **What is made** | A mesh in clusters a primitive (`mlibs.geom`'s `ClusteredMesh`: positions, normals, tangents, the first texture coordinates, indices; triangles, strips and fans), the materials (base colour, roughness, metal, emission with `KHR_materials_emissive_strength`; glTF's own defaults where the document is silent), the pictures a base colour names as `xrender.mesh` textures (PNG and WebP, `EXT_texture_webp`; in a buffer, in the document as base64, or in a file beside it), an instance for each primitive of each node of the scene at the node's place in the world, and `KHR_lights_punctual`'s lights. Numbers packed as `KHR_mesh_quantization` allows are read |
| **Stable ids** | An instance's id is the file's place: `base + (node + 1) × 65,536 + primitive + 1`; a light's `base + node + 1`. The same document gives the same ids in every run, and `Options::base` keeps two documents apart in one scene. `Imported::placed` says each instance's node, mesh, primitive and name |
| **Notes** | What the document says that is not in what was made is never dropped in silence: a `Note` each, with where it is in the document (a JSON pointer: `/materials/2/normalTexture`), what it is, and what was done instead. Today: normal, occlusion, emissive and metal-roughness pictures; alpha modes; two-sided surfaces; the `KHR_materials_*` extensions beyond emission's strength; texture transforms and further sets of coordinates; colours a corner; skins, morph targets, animations, cameras; points and lines; pictures that are not PNG or WebP (JPEG, KTX2); Draco and meshopt packing; any other extension. The list is the measure of what stands between this and the Khronos sample set drawn whole |
| **Refusals** | What cannot be read is refused with its reason and nothing is made: JSON that does not parse, a number that names what is not there, accessors that reach past their buffers, an index that names no position, a file beside the document that cannot be read (or no way given to read one), a document that requires Draco or meshopt packing. Nothing of a file is trusted: the parser's validation first, then `mlibs.geom`'s `check` on every mesh; an accessor with no bytes behind it (a length nobody vouches for) is not read |
| **Files beside the document** | `gltf_file` reads them from the document's directory and nowhere else (a name that climbs out, or an absolute one, is refused); `gltf` reads them through `Options::read`, by the name the document has |
| **Ordered for drawing** | `optimised(mesh)` (on by default for what is brought in; of use for a mesh made by code too): the triangles in the order a vertex cache serves best, cut into clusters of up to 124 that lie together and face alike, the corners in the order the clusters use them. The clusters' bounds and cones are `mlibs.geom`'s own (`clustered` by runs), so a cluster is the same thing whoever cut it |

## Measured

Khronos' sample models, read whole from their `.glb` (a debug build, 2026-11-14; the time is mostly pictures decoded and their smaller sizes made):

| Model | Triangles | Clusters | Instances | Notes |
|---|---|---|---|---|
| Box | 12 | 1 | 1 | none |
| BoxTextured | 12 | 1 | 1 | none |
| Duck | 4,212 | 35 | 1 | a camera |
| Fox | 576 | 7 | 1 | a skin, its bones and weights, 3 animations; no normals (worked out) |
| Avocado | 682 | 6 | 1 | a normal map; roughness and metal from a picture |
| Lantern | 5,394 | 46 | 3 | those two; emission from a picture |
| DamagedHelmet | 15,452 | 130 | 1 | those three; occlusion from a picture; its base colour is a JPEG |
| AntiqueCamera | 20,066 | 167 | 2 | a normal map and roughness and metal from a picture, each twice |

A sheet of 4,608 triangles in a scattered order: cut as they come, a cluster spans the sheet (the widest 5.66 across its bounds); ordered, the widest is 1.39 in as many clusters (38). The plain meshlet builder fills every cluster and gathers what is left at the end into one 4.04 across — one culling can never leave out; the builder used closes a cluster early where it would grow wide.

## How it was designed (D-185)

1. *From this system's own ground.* An import is a value with its own account: the notes are data an agent reads (where, what, instead), not a log line — so "why does the helmet look flat" has an answer without a person looking. Ids are the file's places, so a pixel traced to an instance ([xrender.frame](../frame)) is traced on to a node of the document. A cluster's bounds and cone are worked out by the one owner of clusters (`mlibs.geom`), whatever tool ordered the triangles.
2. *What exists, combined.* Parsing is cgltf's and the ordering meshoptimizer's, each through its module wrap ([mcpplibs/cgltf-m](https://github.com/mcpplibs/cgltf-m), [mcpplibs/meshoptimizer-m](https://github.com/mcpplibs/meshoptimizer-m); D-56, D-83), each named in one native unit. Pictures are `mlibs.image`'s.
3. *The level of the best.* Against the reference loaders (Khronos' sample viewer, Filament's gltfio, Godot's importer): behind — they carry every texture of the surface model, skins, animations, morph targets, JPEG and KTX2, which here are notes. Ahead in one thing those do not give: what was not carried, as data with its place. What comes next is in the notes' order of weight: the surface's other pictures (with the mesh pass's material), JPEG and KTX2, then skins and animations to hanim.

## Agent surface

Part of the frame's subject ([xrender.frame](../frame)); what this package gives an agent's question, as plain functions:

| | |
|---|---|
| State and addresses | An instance by its id, which is its node and primitive in the document (`placed`); a material and a mesh by their place |
| Observation | `Imported` is all data: counts, `triangles()`, `bounds()`, every note |
| Causes | A `Note`'s `where` is a JSON pointer into the document; a refusal's text says what is wrong and, where there is a way on, the way (`Options::read`, another `Options::base`) |
| Control | `gltf`, `gltf_file`, `place` (refused whole, the scene as it was), `optimised` |
| Time | None: the same bytes in give the same value out |
| Not here | Who may read which file (the caller's `read`); drawing (the mesh pass); what moves (hanim, when skins and animations are carried) |

## Where things are

| | |
|---|---|
| `src/asset.cppm` | the interface |
| `src/asset.cpp` | placing into a scene, reading from a file |
| `src/native/gltf.cpp` | the one unit that names cgltf |
| `src/native/optimise.cpp` | the one unit that names meshoptimizer |
| `tests/asset_test.cpp` | 7 cases: a whole document; the same bytes twice, `.glb` and files beside; from a file; the notes; strips, fans and lamps; refusals; ordering |
