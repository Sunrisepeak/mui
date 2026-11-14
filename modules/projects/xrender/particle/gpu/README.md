# xrender.particle.gpu

Particles drawn on a device: [xrender.particle](..)'s reference through `mlibs.rhi` alone, one Slang shader (`shaders/particle.slang`), held to the reference's pixels.

| | |
|---|---|
| **What it takes** | The reference's batches and view; the frame's light (`rgba16_float`, what `xrender.mesh.gpu`'s pass of light drew — kept) and how far each surface is (`record_surface` of the same view) |
| **Order** | The reference's: every particle of every batch the farthest first, laid out on the CPU as squares facing the eye into one buffer of corners a frame, drawn in runs of one batch — `add` by adding, `over` premultiplied |
| **A pixel** | Where its middle is inside the square: the reference's round spot at its place there, faded by how near it is along the eye's ray to the surface behind |
| **Held to the reference** | [cts](cts) (`xrender.particle.gpu.cts`, contract `xrender.particle.gpu@1`): what 145 sparks and puffs change in a room against what they change in the reference; backends declare it (`xrender.rhi.sdl`'s `particle_test`) |
| **Not yet** | A batch with a picture (`Batch::sprite`): counted in `Stats::pictured` and left out; the Web backends' runs of the suite |

| Package | |
|---|---|
| [cts](cts) | The suite a backend's device is held to |

## Agent surface

The reference's (`pick`, `Drawn`): a device draws what the reference says, and adds `Stats` — particles, those outside, runs, those left out for a picture.
