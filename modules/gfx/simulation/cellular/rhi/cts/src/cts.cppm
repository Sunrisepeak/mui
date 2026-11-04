export module openmui.simulation.cellular.rhi.cts;
import std;
import openmui.rhi;

export namespace omui::simulation::cellular::gpu::cts {
struct Report {
    std::uint32_t scenarios = 0, steps = 0, comparisons = 0, validation_checks = 0;
    std::uint64_t compared_values = 0, process_hash = 0;
};
// Needs an actual compute device with readback. Unsupported is an error, not
// a passed/empty test. Backend tests decide whether that device is required.
[[nodiscard]] omui::rhi::Result<Report> run(omui::rhi::Device&);
}
