// The mesh pass's suite on the page's WebGPU: the owner of a suite runs it against a backend. In a browser
// (tools/ci/browser.sh, `--require-webgpu`) a missing device fails; elsewhere there is no page, which is said,
// and the case passes vacuously.
import std;
import mlibs.rhi;
import mlibs.cts;
import xrender.rhi.webgpu;
import xrender.mesh.gpu.cts;
import mcpplibs.os;
import mcpplibs.mtest;

namespace webgpu = xrender::rhi::webgpu;

using namespace mcpplibs::mtest;

namespace {
bool require_flag = false;  // --require-webgpu
bool required() { return require_flag || mcpplibs::os::env::get("OMUI_REQUIRE_WEBGPU") == "1"; }
}  // namespace

int main(int argc, char** argv) {
    std::vector<char*> args(argv, argv + argc);
    require_flag = std::erase_if(args, [](const char* argument) { return std::string_view(argument) == "--require-webgpu"; }) > 0;

    "the mesh pass's suite on the page's WebGPU: every case"_test = [] {
        auto device = webgpu::create();
        if (!device) {
            if (required()) expect(false) << std::format("WebGPU is required here, and there is no device: {}", device.error().message);
            else std::println("no WebGPU device here ({}); OMUI_REQUIRE_WEBGPU=1 makes that a failure", device.error().message);
            return;
        }
        if (!webgpu::can_wait()) {
            expect(!required()) << "the suite reads the device back: this link must be able to wait for WebGPU (-sASYNCIFY)";
            std::println("the mesh suite was not run: this link cannot wait for WebGPU");
            return;
        }
        const auto& caps = (*device)->caps();
        std::println("device: {} on {}{}", caps.device, name(caps.backend), caps.software ? " (software)" : "");
        // The suite's cases, this program's own: <device>/mesh.yard, …
        xrender::mesh::gpu::cts::declare(**device, {.subject = caps.device});
    };

    return mcpplibs::mtest::run(static_cast<int>(args.size()), args.data());
}
