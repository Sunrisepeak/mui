// Quay's frame on the page's WebGPU (the release plan's RC2.1; D-187's weaker tier: a browser, p99 within
// 16.7 ms): the frame `tests/bench`'s game/quay-frame-1080p-gpu times on a desktop device — the step, the guide's
// choice, the picture prepared, and on the device the bodies bent, the moon's map, the lamps' views, the light, the
// glow and the curve, submitted and waited for — timed here on whatever device the page has. In a browser
// (omui-devtools check browser, `--require-webgpu`) a missing device fails; elsewhere there is no page, which is said, and
// the case passes vacuously. A software device (SwiftShader, CI's) is timed and said, never held to the budget:
// its numbers are not performance (AGENTS §2.10). On a real GPU (omui-devtools browser --headed under omui-devtools gpu --real, where a display is) the
// p99 is held to it.
import std;
import mlibs.rhi;
import xrender.rhi.webgpu;
import omui.showcase.quay.picture;
import mcpplibs.os;
import mcpplibs.mtest;

namespace webgpu = xrender::rhi::webgpu;
namespace rhi = mlibs::rhi;

using namespace mcpplibs::mtest;

namespace {
bool require_flag = false;  // --require-webgpu
bool required() { return require_flag || mcpplibs::os::env::get("OMUI_REQUIRE_WEBGPU") == "1"; }
// The frames timed (`--frames N`: fewer, where the device is software and only the program is checked), at 1080p
// as the desktop bench.
std::uint32_t frames = 300;
constexpr std::uint32_t width = 1920, height = 1080;
}  // namespace

int main(int argc, char** argv) {
    std::vector<char*> args(argv, argv + argc);
    require_flag = std::erase_if(args, [](const char* argument) { return std::string_view(argument) == "--require-webgpu"; }) > 0;
    for (std::size_t at = 1; at + 1 < args.size(); ++at)
        if (std::string_view(args[at]) == "--frames") {
            std::from_chars(args[at + 1], args[at + 1] + std::strlen(args[at + 1]), frames);
            frames = std::max(frames, 1u);
            args.erase(args.begin() + static_cast<std::ptrdiff_t>(at), args.begin() + static_cast<std::ptrdiff_t>(at) + 2);
            break;
        }

    "Quay's frame on the page's WebGPU: timed as the desktop bench times it, held to 16.7 ms at the p99 on a real GPU"_test = [] {
        auto device = webgpu::create();
        if (!device) {
            if (required()) expect(false) << std::format("WebGPU is required here, and there is no device: {}", device.error().message);
            else std::println("no WebGPU device here ({}); OMUI_REQUIRE_WEBGPU=1 makes that a failure", device.error().message);
            return;
        }
        if (!webgpu::can_wait()) {
            expect(!required()) << "a frame is waited for: this link must be able to wait for WebGPU (-sASYNCIFY)";
            std::println("Quay's frame was not timed: this link cannot wait for WebGPU");
            return;
        }
        rhi::Device& on = **device;
        const rhi::Caps& caps = on.caps();
        auto played = quay::picture::Played::make(on, width, height);
        expect(fatal(played.has_value())) << (played ? "" : played.error());
        std::vector<double> took;
        took.reserve(frames);
        const std::uint32_t warming = std::min(frames, 10u);
        for (std::uint32_t frame = 0; frame < frames + warming; ++frame) {
            played->again_if_over();  // untimed, as the bench leaves it out
            const auto begun = std::chrono::steady_clock::now();
            const auto done = played->frame();
            expect(fatal(done.has_value())) << (done ? "" : done.error());
            // The first ten (fewer where few are timed) warm the page's pipelines and caches, as the bench's warm-up does.
            if (frame >= warming) took.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begun).count());
        }
        std::ranges::sort(took);
        const auto at = [&](double share) { return took[std::min(took.size() - 1, static_cast<std::size_t>(share * static_cast<double>(took.size())))]; };
        const double p50 = at(0.5), p99 = at(0.99), worst = took.back();
        const auto& last = played->drawn().last();
        std::println("Quay at {}×{} on {} ({}{}): {} frames, p50 {:.2f} ms, p99 {:.2f} ms, worst {:.2f} ms; {} draws, {} lights, {} views of lamps", width, height, caps.device, rhi::name(caps.backend),
                     caps.software ? ", software: not performance" : "", took.size(), p50, p99, worst, last.draws, last.lamps, last.lamp_views);
        std::println("evidence quay-web-p99-ms: {:.2f}", p99);
        if (!caps.software) expect(le(p99, 16.7)) << std::format("D-187 on a browser: p99 {:.2f} ms against 16.7", p99);
    };

    return mcpplibs::mtest::run(static_cast<int>(args.size()), args.data());
}
