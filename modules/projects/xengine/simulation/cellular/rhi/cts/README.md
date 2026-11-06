# xengine.simulation.cellular.rhi.cts

`xengine::simulation::cellular::gpu::cts::run(device)` returns a typed report or the first actual failure. It requires compute/readback and never treats unavailable execution as passed. The existing SDL_GPU and browser WebGPU test runners own device setup and required/optional policy.

Sixteen scenarios cover material/state/age reaction, all boundary modes, every-tick comparison, resident 12-tick batches, explicit checkpoint upload/replay, one-cell/odd dimensions, ±32 offsets, wrap multiplicity, empty-write barriers, saturating writes and identity plans. Every comparison checks complete uint32 state, not an approximate final image. The process hash uses the shared hash library's fixed little-endian encoding. Failed-record checks preserve a pre-existing command and payload.

This is a conformance consumer, not a simulation application, parser, renderer or separate compute implementation. Public API users need neither CTS nor a particular backend.

This manifest is retained because two actual backend test suites share the
same scenarios and complete-state comparisons. It depends on the public rule
and GPU contracts, not a UI/testing runtime or clock. It is an explicit test
consumer, never a mandatory dependency of the adapter. Old module names
forward within this package; use the new `xengine.*` dependency key.
