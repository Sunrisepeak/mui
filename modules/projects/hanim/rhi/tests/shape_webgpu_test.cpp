// hanim.rhi's conformance suite on the page's WebGPU: the owner of a suite runs
// it against a backend (D-161), never a backend against its consumers. In a
// browser (tools/ci/browser.sh, `--require-webgpu`) a missing device or a link
// that cannot wait for it fails; elsewhere there is no page, which is said,
// and the case passes vacuously.
import std;
import mlibs.rhi;
import mlibs.cts;
import xrender.rhi.webgpu;
import hanim.rhi;
import hanim.rhi.cts;
import mcpplibs.os;
import mlibs.testing;
import mcpplibs.mtest;

namespace webgpu = xrender::rhi::webgpu;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;

namespace {
bool require_flag = false;  // --require-webgpu
bool required() { return require_flag || mcpplibs::os::env::get("OMUI_REQUIRE_WEBGPU") == "1"; }
}  // namespace

int main(int argc, char** argv) {
    std::vector<char*> args(argv, argv + argc);
    require_flag = std::erase_if(args, [](const char* argument) { return std::string_view(argument) == "--require-webgpu"; }) > 0;

    "shape batches: every control point equal to the CPU sampler's within float error"_test = [] {
        auto device = webgpu::create();
        if (!device) {
            if (required()) expect(false) << std::format("WebGPU is required here, and there is no device: {}", device.error().message);
            else std::println("no WebGPU device here ({}); OMUI_REQUIRE_WEBGPU=1 makes that a failure", device.error().message);
            return;
        }
        if (!webgpu::can_wait()) {
            expect(!required()) << "the shape batch suite requires actual WebGPU readback";
            std::println("the shape batch suite was not run: this link cannot wait for WebGPU");
            return;
        }
        const auto& caps = (*device)->caps();
        std::println("device: {} on {}{}", caps.device, name(caps.backend), caps.software ? " (software)" : "");
        hanim::gpu::cts::Totals totals;
        const auto report = hanim::gpu::cts::run(**device, {.subject = caps.device, .platform = "webgpu"}, {.required = {"compute"}}, &totals);
        std::print("{}", report.text());
        expect(report.ok() && report.count(mlibs::cts::Status::passed) == report.outcomes.size()) << report.text();
        expect(eq(totals.scenarios, 4u));
        std::println("shape batches: {} samples, {} points, largest difference {}", totals.samples, totals.compared_points, totals.largest_difference);
        std::ofstream(scratch_dir("rhi") / "webgpu-shape.cts.omui.json") << report.json();  // the evidence, for a reviewer
    };

    return mcpplibs::mtest::run(static_cast<int>(args.size()), args.data());
}
