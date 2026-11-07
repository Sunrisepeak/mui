// hanim.rhi.cts — the shape batch's conformance on a device (mlibs.cts):
// every control point of a thousand morphs, sampled at once at several
// progresses, equals the CPU sampler's within float error (1e-4 on
// coordinates up to 40), straight and on a curved point path; noise, mixed
// paths, empty batches and bad records are refused, appending nothing. Each
// case needs compute and readback: without them it is unsupported, never
// passed. The backends' own tests run it on their devices.
export module hanim.rhi.cts;
import std;
import mlibs.rhi;
export import mlibs.cts;

export namespace hanim::gpu::cts {
struct Totals {
    std::uint32_t scenarios = 0, samples = 0, validation_checks = 0;
    std::uint64_t compared_points = 0;
    double largest_difference = 0;
};
// Every case on `device` (ids `shape.*`, contract hanim.shape.gpu@1).
[[nodiscard]] mlibs::cts::Report run(mlibs::rhi::Device& device,
                                     mlibs::cts::Environment environment = {},
                                     const mlibs::cts::Policy& policy = {},
                                     Totals* totals = nullptr);
}  // namespace hanim::gpu::cts
