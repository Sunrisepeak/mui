# xengine.cellular.rhi.cts

`xengine::cellular::gpu::cts::declare(device)` declares the device kernel's conformance cases (`<device>/cellular.*`, contract `xengine.cellular.gpu@1`, mtest's CTS kind). Each needs compute and readback: a device without them is unsupported, never passed. The SDL_GPU (`xengine.cellular.sdl`) and browser WebGPU (`tests/cellular_webgpu_test.cpp`, `tools/ci/browser.sh`) runners own device setup and the required/optional policy.

Forty scenarios, each stepping the same state on the CPU reference and on the device, tick after tick with the tick advancing, and comparing every uint32 value (not an image):

| Scenarios | What they hold to the CPU |
|---|---|
| `reaction.*`, `resident-batch.*`, `wide-halo.*`, `identity` | encoding 1: states, thresholds, counted neighbourhoods at ±32, empty-write barriers, saturating writes, every edge, one-cell and odd shapes, 12-tick resident batches |
| `aggregates.*` | encoding 2: a neighbourhood's sum, least and greatest in conditions and as terms, counts as terms, terms taken, values at the largest number (every saturation), every edge and a bath (a value a channel) |
| `transfer.*` | two flows in one step — conducted by a channel (the lesser of a pair's, bounded) and at a conductance of 1 — after a rule that writes the same channel |
| `exchange.*` | cells that fall, slide and spread, in 2 × 2 blocks whose cut takes its four positions; three channels carried, one not; a rule's change moved with its cell; odd, even, 2 × 2 and 1 × 5 fields; a wrapped even edge goes round |
| `matter.standard.*` | encoding 3: the standard definition of matter lowered — the table, every reaction, adjustments, flags, becoming and resets, flows conducted by a column, cells moving by the table's weight and mobility — every material scattered over a room with fires lit, open to air (a bath), nothing beyond, wrapped; 64 ticks |
| `paint` | the device painter's pixels against `pixel_of` over the CPU's field |

Every scenario also restores a host checkpoint through the same RHI and replays it, and checks that nine kinds of invalid recording (another device, aliasing, channels, shape, a stale buffer, an edge, a small buffer, a usage, a bath of the wrong size) leave the caller's command list and payload unchanged. The process hash uses the shared hash library's fixed little-endian encoding.

This is a conformance consumer, not a simulation application or a second implementation: it depends on the public cellular, matter and GPU contracts, and is never a dependency of the adapter.
