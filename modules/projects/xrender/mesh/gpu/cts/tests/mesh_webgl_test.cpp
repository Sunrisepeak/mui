// The mesh pass's suite on the page's WebGL2: the owner of a suite runs it against a backend. In a browser
// (tools/ci/browser.sh, `--require-webgl`) a missing device fails; elsewhere there is no page, which is said,
// and the case passes vacuously.
import std;
import mlibs.rhi;
import mlibs.cts;
import xrender.rhi.webgl;
import xrender.mesh.gpu.cts;
import mcpplibs.os;
import mcpplibs.mtest;

namespace webgl = xrender::rhi::webgl;

using namespace mcpplibs::mtest;

namespace {
bool require_flag = false;  // --require-webgl
bool required() { return require_flag || mcpplibs::os::env::get("OMUI_REQUIRE_WEBGL") == "1"; }
}  // namespace

int main(int argc, char** argv) {
    std::vector<char*> args(argv, argv + argc);
    require_flag = std::erase_if(args, [](const char* argument) { return std::string_view(argument) == "--require-webgl"; }) > 0;

    "the mesh pass's suite on the page's WebGL2: every case"_test = [] {
        auto device = webgl::create();
        if (!device) {
            if (required()) expect(false) << std::format("WebGL2 is required here, and there is no device: {}", device.error().message);
            else std::println("no WebGL2 device here ({}); OMUI_REQUIRE_WEBGL=1 makes that a failure", device.error().message);
            return;
        }
            const auto& caps = (*device)->caps();
        std::println("device: {} on {}{}", caps.device, name(caps.backend), caps.software ? " (software)" : "");
        // The suite's cases, this program's own: <device>/mesh.yard, …
        xrender::mesh::gpu::cts::declare(**device, {.subject = caps.device});
    };

    return mcpplibs::mtest::run(static_cast<int>(args.size()), args.data());
}
