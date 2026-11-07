// xengine.simulation.cellular.rhi.cts — the cellular kernel's conformance on a
// device (mlibs.cts): every scenario's GPU values equal the CPU rules' at every
// tick, across reactions, resident batches, wide halos and edge modes, and an
// unprepared plan is refused. Each case needs compute and readback: a device
// without them is unsupported, never passed — and a backend whose tests
// require compute say so (`{.required = {"compute"}}`), which fails it.
export module xengine.simulation.cellular.rhi.cts.api;
import std;
import mlibs.rhi;
export import mlibs.cts;

export namespace xengine::simulation::cellular::gpu::cts {
// What the scenarios did, all cases together.
struct Totals {
    std::uint32_t scenarios = 0, steps = 0, comparisons = 0, validation_checks = 0;
    std::uint64_t compared_values = 0, process_hash = 0;
};
// Every case on `device` (ids `cellular.*`, contract xengine.simulation.cellular.gpu@1).
[[nodiscard]] mlibs::cts::Report run(mlibs::rhi::Device& device, mlibs::cts::Environment environment = {},
                                     const mlibs::cts::Policy& policy = {}, Totals* totals = nullptr);
}
