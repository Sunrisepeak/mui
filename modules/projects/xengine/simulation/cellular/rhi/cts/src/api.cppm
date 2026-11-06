export module xengine.simulation.cellular.rhi.cts.api;
import std;
import mlibs.rhi;

export namespace xengine::simulation::cellular::gpu::cts {
struct Report {
    std::uint32_t scenarios = 0, steps = 0, comparisons = 0, validation_checks = 0;
    std::uint64_t compared_values = 0, process_hash = 0;
};
// Needs an actual compute device with readback. Unsupported is an error, not
// a passed/empty test. Backend tests decide whether that device is required.
[[nodiscard]] mlibs::rhi::Result<Report> run(mlibs::rhi::Device&);
}
