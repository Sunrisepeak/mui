# xrender.particle.gpu

Particles drawn on a device: [xrender.particle](..)'s reference through `mlibs.rhi` alone, one Slang shader (`shaders/particle.slang`), held to the reference's pixels.

| | |
|---|---|
| **What it takes** | The reference's batches and view; the frame's light (`rgba16_float`, what `xrender.mesh.gpu`'s pass of light drew — kept) and how far each surface is (`record_surface` of the same view) |
| **Order** | The reference's: every particle of every batch the farthest first, laid out on the CPU as squares facing the eye into one buffer of corners a frame, drawn in runs of one batch — `add` by adding, `over` premultiplied |
| **A pixel** | Where its middle is inside the square: the reference's round spot at its place there — or the batch's picture, read at the size the reference reads (each particle's texels a pixel, the same mix between two sizes, straight in the footprint) — faded by how near it is along the eye's ray to the surface behind |
| **Pictures** | Put on the device the first time a batch shows them, on the frame's list, by `xrender.mesh.gpu`'s `picture_on` (the one way a picture goes there: each size a level, light times cover and cover) and read through `sampler_of`; kept by address, `forget` lets one go |
| **Held to the reference** | [cts](cts) (`xrender.particle.gpu.cts`, contract `xrender.particle.gpu@1`): `particle.room` — what 145 sparks and puffs change in a room against what they change in the reference; `particle.pictured` — rings added and smoke laid over by their pictures, near and far; backends declare it (`xrender.rhi.sdl`'s `particle_test`) |
| **Not yet** | The Web backends' runs of the suite |

| Package | |
|---|---|
| [cts](cts) | The suite a backend's device is held to |

## Agent surface

The reference's (`pick`, `Drawn`): a device draws what the reference says, and adds `Stats` — particles, those outside, runs, those of a picture and the pictures put on the device this frame.
