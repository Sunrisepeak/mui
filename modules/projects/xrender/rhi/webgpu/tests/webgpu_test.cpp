// omui.rhi on WebGPU. Natively and under node there is no WebGPU: create()
// says so (Errc::unsupported), and the cases that need a device say it and
// pass — unless OMUI_REQUIRE_WEBGPU=1 or --require-webgpu (the browser's
// run) makes that a failure. In a browser
// (tools/ci/browser.sh: the program linked to wait, -sASYNCIFY, in headless
// Chromium) they run on the page's device: uploads, clears, copies and
// readback byte for byte, an upload WebGPU cannot write refused before the
// page sees it, then the rhi conformance suite (openmui.rhi.cts) — every
// case with the suite's own shaders, in WGSL.
import std;
import mlibs.rhi;
import xrender.rhi.webgpu;
import mlibs.rhi.cts;
import mcpplibs.os;
import mlibs.testing;
import mcpplibs.mtest;

namespace webgpu = xrender::rhi::webgpu;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
using namespace mlibs::rhi;

namespace {

bool require_flag = false;  // --require-webgpu

bool required() { return require_flag || mcpplibs::os::env::get("OMUI_REQUIRE_WEBGPU") == "1"; }

std::unique_ptr<Device> open_device() {
    auto d = webgpu::create();
    if (!d) {
        if (required()) expect(false) << std::format("WebGPU is required here, and there is no device: {}", d.error().message);
        else std::println("no WebGPU device here ({}); OMUI_REQUIRE_WEBGPU=1 makes that a failure", d.error().message);
        return nullptr;
    }
    std::println("device: {} on {}{}{}", (*d)->caps().device, name((*d)->caps().backend), (*d)->caps().software ? " (software)" : "",
                 webgpu::can_wait() ? "" : "; this program cannot wait for it");
    return std::move(*d);
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<char*> args(argv, argv + argc);
    require_flag = std::erase_if(args, [](const char* a) { return std::string_view(a) == "--require-webgpu"; }) > 0;

    "without WebGPU: unsupported, and why"_test = [] {
        if (webgpu::available()) return;
        auto d = webgpu::create();
        expect(!d && d.error().code == Errc::unsupported) << (d ? "a device" : d.error().message);
        if (!d) expect(!d.error().message.empty());
    };

    "a device: WebGPU's, taking WGSL, four samples, one at a time"_test = [] {
        auto d = open_device();
        if (!d) return;
        expect(d->caps().backend == Backend::webgpu);
        expect(d->caps().formats == formats({ShaderFormat::wgsl}));
        expect(eq(d->caps().max_samples, 4u));
        expect(d->caps().max_texture_size >= 8192);
        auto second = webgpu::create();
        expect(!second && second.error().code == Errc::unsupported) << "a second device while the first lives";
    };

    "upload, clear, copy, read back: the bytes, exactly"_test = [] {
        auto d = open_device();
        if (!d || !webgpu::can_wait()) return;
        const auto target = d->create(TextureDesc{.width = 8, .height = 4, .usage = TextureUsage::color_target | TextureUsage::sampled, .label = "target"});
        const auto image = d->create(TextureDesc{.width = 2, .height = 2, .usage = TextureUsage::sampled});
        expect(target && image) << (target ? (image ? "" : image.error().message) : target.error().message);
        if (!target || !image) return;
        const std::array<std::uint8_t, 16> texels{255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 10, 20, 30, 40};
        CommandList l;
        l.upload(*image, Region{0, 0, 2, 2}, std::as_bytes(std::span(texels)));
        l.begin_render({*target, LoadOp::clear, {0.0f, 0.0f, 1.0f, 1.0f}});
        l.end_render();
        l.copy(*image, Region{0, 0, 2, 2}, *target, 3, 1);
        auto sub = d->submit(l);
        expect(sub.has_value()) << (sub ? "" : sub.error().message);
        if (!sub) return;
        expect(d->wait(*sub).has_value());
        auto px = d->read(*target, Region{0, 0, 8, 4});
        expect(px.has_value()) << (px ? "" : px.error().message);
        if (!px) return;
        auto at = [&](int x, int y) {
            const auto* p = reinterpret_cast<const std::uint8_t*>(px->data()) + (y * 8 + x) * 4;
            return std::array<int, 4>{p[0], p[1], p[2], p[3]};
        };
        expect(at(0, 0) == std::array<int, 4>{0, 0, 255, 255}) << "the clear colour";
        expect(at(3, 1) == std::array<int, 4>{255, 0, 0, 255} && at(4, 2) == std::array<int, 4>{10, 20, 30, 40}) << "the copied texels";
        expect(at(7, 3) == std::array<int, 4>{0, 0, 255, 255});
    };

    "buffers: an upload at an offset, a length that ends the buffer, read back"_test = [] {
        auto d = open_device();
        if (!d || !webgpu::can_wait()) return;
        const auto b = d->create(BufferDesc{.size = 64, .usage = BufferUsage::vertex});
        const auto odd = d->create(BufferDesc{.size = 6, .usage = BufferUsage::index});
        expect(b && odd);
        if (!b || !odd) return;
        std::array<std::uint32_t, 4> words{1, 2, 3, 0xdeadbeef};
        const std::array<std::uint16_t, 3> indices{7, 8, 9};
        CommandList l;
        l.upload(*b, 16, std::span<const std::uint32_t>(words));
        l.upload(*odd, 0, std::span<const std::uint16_t>(indices));
        expect(d->submit(l).has_value());
        std::array<std::uint32_t, 4> back{};
        expect(d->read(*b, 16, std::as_writable_bytes(std::span(back))).has_value());
        expect(back == words) << std::format("{}", back);
        std::array<std::uint16_t, 2> tail{};
        expect(d->read(*odd, 2, std::as_writable_bytes(std::span(tail))).has_value());
        expect(tail == std::array<std::uint16_t, 2>{8, 9}) << std::format("{}", tail);
    };

    "an upload WebGPU cannot write, and an invalid list, never reach the page"_test = [] {
        auto d = open_device();
        if (!d) return;
        const auto b = d->create(BufferDesc{.size = 64, .usage = BufferUsage::vertex});
        expect(b.has_value());
        if (!b) return;
        const std::array<std::uint8_t, 6> six{};
        CommandList l;
        l.upload(*b, 2, std::span<const std::uint8_t>(six));
        auto r = d->submit(l);
        expect(!r && r.error().code == Errc::unsupported) << (r ? "accepted" : r.error().message);
        if (!r) expect(r.error().message.contains("4 bytes at a time")) << r.error().message;
        CommandList bad;
        bad.draw(3);
        auto v = d->submit(bad);
        expect(!v && v.error().code == Errc::invalid);
    };

    "the omui.rhi conformance suite (level B): every case, exactly"_test = [] {
        auto d = open_device();
        if (!d) return;
        if (!webgpu::can_wait()) {
            std::println("the suite reads back, and this program cannot wait for WebGPU (link it with -sASYNCIFY)");
            expect(!required()) << "WebGPU is required here: the suite must run";
            return;
        }
        expect(cts::shader_formats().has(ShaderFormat::wgsl)) << "the suite's shaders were compiled to WGSL";
        // The suite's cases, this program's own: <device>/rhi.clear, …
        cts::declare(*d, {.subject = d->caps().device});
    };

    return mcpplibs::mtest::run(static_cast<int>(args.size()), args.data());
}
