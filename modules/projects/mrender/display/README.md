# mrender.display

The display list of contract ⑤ (module `mrender.display`, spec §7): what a frame draws — paths, paints and gradients, strokes, clips, transforms, layers with blend modes and effect parameters, glyph runs, images, external textures, node and part markers — as plain data whose variable parts live in the list's arena, so a frame's list of the same shape as the last allocates nothing, and two lists compare and hash.

**Why a package of its own**: a renderer reads a list; what writes one, or reads it without pixels, needs only the types. [mrender.vector](../vector) (the CPU and GPU renderers) re-exports it, so `import mrender.vector;` reads as before; [mrender.cells](../cells) (a terminal's character cells) depends on this alone, with no rasteriser, vector renderer or shaper (terminal design §9, step 3).

**Label**: stable — the list as it was in mrender.vector, moved with its hashes and wire format unchanged.

## Agent surface

The list is data a renderer turns into a frame; an agent reaches it through the frame's owner ([mrender.frame](../frame)'s capture and provenance, the hosts' `ui.*` operations), not here.
