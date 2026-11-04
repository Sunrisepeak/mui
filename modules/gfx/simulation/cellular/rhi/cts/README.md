# openmui.simulation.cellular.rhi.cts

`omui::simulation::cellular::gpu::cts::run(device)` returns a typed report or the first actual failure. It requires compute/readback and never treats unavailable execution as passed. The existing SDL_GPU and browser WebGPU test runners own device setup and required/optional policy.

Sixteen scenarios cover material/state/age reaction, all boundary modes, every-tick comparison, resident 12-tick batches, explicit checkpoint upload/replay, one-cell/odd dimensions, ±32 offsets, wrap multiplicity, empty-write barriers, saturating writes and identity plans. Every comparison checks complete uint32 state, not an approximate final image. The process hash uses the shared hash library's fixed little-endian encoding. Failed-record checks preserve a pre-existing command and payload.

This is a conformance consumer, not a simulation application, parser, renderer or separate compute implementation. Public API users need neither CTS nor a particular backend.
