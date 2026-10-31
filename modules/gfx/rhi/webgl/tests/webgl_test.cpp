// omui.rhi on WebGL2. Natively and under node there is no WebGL2: create()
// says so (Errc::unsupported), and the cases that need a device say it and
// pass — unless OMUI_REQUIRE_WEBGL=1 or --require-webgl (the browser's run)
// makes that a failure. In a browser (tools/ci/browser.sh, headless
// Chromium) they run on the page's WebGL2: uploads, clears, copies and
// readback byte for byte, bgra8 kept as RGBA8 and its bytes as given, what
// WebGL2 lacks refused up front, then the rhi conformance suite
// (openmui.rhi.cts) — every case with the suite's own shaders, in ESSL.
import std;
import openmui.rhi;
import openmui.rhi.webgl;
import openmui.rhi.cts;
import mcpplibs.os;
import openmui.testing;

using namespace omui::testing;
using namespace omui::rhi;

namespace {

bool require_flag = false;  // --require-webgl

bool required() { return require_flag || mcpplibs::os::env::get("OMUI_REQUIRE_WEBGL") == "1"; }

std::unique_ptr<Device> open_device() {
    auto d = webgl::create();
    if (!d) {
        if (required()) check(false, std::format("WebGL2 is required here, and there is no device: {}", d.error().message));
        else std::println("no WebGL2 device here ({}); OMUI_REQUIRE_WEBGL=1 makes that a failure", d.error().message);
        return nullptr;
    }
    std::println("device: {} on {}{}", (*d)->caps().device, name((*d)->caps().backend), (*d)->caps().software ? " (software)" : "");
    return std::move(*d);
}

using Px = std::array<int, 4>;

Px at(const std::vector<std::byte>& px, std::uint32_t width, std::uint32_t x, std::uint32_t y) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(px.data()) + (y * width + x) * 4;
    return {p[0], p[1], p[2], p[3]};
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<char*> args(argv, argv + argc);
    require_flag = std::erase_if(args, [](const char* a) { return std::string_view(a) == "--require-webgl"; }) > 0;
    Suite s{"rhi.webgl"};

    s.test("without WebGL2: unsupported, and why", [] {
        if (required()) return;
        auto d = webgl::create();
        if (d) return;  // a page with WebGL2: the other cases speak for it
        check(d.error().code == Errc::unsupported && !d.error().message.empty(), d.error().message);
    });

    s.test("a device: WebGL2's, taking ESSL, depth, no compute, one at a time", [] {
        auto d = open_device();
        if (!d) return;
        check(d->caps().backend == Backend::webgl);
        check(d->caps().formats == formats({ShaderFormat::essl}));
        check(!d->caps().compute && d->caps().depth);
        check(d->caps().max_samples >= 4, std::format("{} samples", d->caps().max_samples));
        auto second = webgl::create();
        check(!second && second.error().code == Errc::unsupported, "a second device while the first lives");
    });

    s.test("upload, clear, copy, read back: the bytes, exactly", [] {
        auto d = open_device();
        if (!d) return;
        const auto target = d->create(TextureDesc{.width = 8, .height = 4, .usage = TextureUsage::color_target | TextureUsage::sampled, .label = "target"});
        const auto image = d->create(TextureDesc{.width = 2, .height = 2, .usage = TextureUsage::sampled});
        check(target && image, target ? (image ? "" : image.error().message) : target.error().message);
        if (!target || !image) return;
        const std::array<std::uint8_t, 16> texels{255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 10, 20, 30, 40};
        CommandList l;
        l.upload(*image, Region{0, 0, 2, 2}, std::as_bytes(std::span(texels)));
        l.begin_render({*target, LoadOp::clear, {0.0f, 0.0f, 1.0f, 1.0f}});
        l.end_render();
        l.copy(*image, Region{0, 0, 2, 2}, *target, 3, 1);
        auto sub = d->submit(l);
        check(sub.has_value(), sub ? "" : sub.error().message);
        if (!sub) return;
        check(d->wait(*sub).has_value());
        auto px = d->read(*target, Region{0, 0, 8, 4});
        check(px.has_value(), px ? "" : px.error().message);
        if (!px) return;
        check(at(*px, 8, 0, 0) == Px{0, 0, 255, 255}, "the clear colour");
        check(at(*px, 8, 3, 1) == Px{255, 0, 0, 255} && at(*px, 8, 4, 2) == Px{10, 20, 30, 40}, "the copied texels: row 0 the top");
        check(at(*px, 8, 7, 3) == Px{0, 0, 255, 255});
    });

    s.test("bgra8: kept as RGBA8, its bytes as given — uploaded, cleared, read back", [] {
        auto d = open_device();
        if (!d) return;
        const auto t = d->create(TextureDesc{.format = Format::bgra8_unorm, .width = 2, .height = 1, .usage = TextureUsage::color_target | TextureUsage::sampled});
        check(t.has_value(), t ? "" : t.error().message);
        if (!t) return;
        const std::array<std::uint8_t, 8> bgra{10, 20, 30, 255, 40, 50, 60, 128};
        CommandList up;
        up.upload(*t, Region{0, 0, 2, 1}, std::as_bytes(std::span(bgra)));
        check(d->submit(up).has_value());
        auto back = d->read(*t, Region{0, 0, 2, 1});
        check(back && std::memcmp(back->data(), bgra.data(), 8) == 0, "the bytes as uploaded");
        CommandList red;
        red.begin_render({*t, LoadOp::clear, {1.0f, 0.0f, 0.0f, 1.0f}});
        red.end_render();
        check(d->submit(red).has_value());
        back = d->read(*t, Region{0, 0, 2, 1});
        check(back && at(*back, 2, 0, 0) == Px{0, 0, 255, 255}, "red, in blue-green-red-alpha order");
    });

    s.test("buffers: an upload at any offset, read back", [] {
        auto d = open_device();
        if (!d) return;
        const auto b = d->create(BufferDesc{.size = 64, .usage = BufferUsage::vertex});
        const auto i = d->create(BufferDesc{.size = 6, .usage = BufferUsage::index});
        check(b && i);
        if (!b || !i) return;
        const std::array<std::uint32_t, 4> words{1, 2, 3, 0xdeadbeef};
        const std::array<std::uint8_t, 3> odd{7, 8, 9};
        const std::array<std::uint16_t, 3> indices{4, 5, 6};
        CommandList l;
        l.upload(*b, 16, std::span<const std::uint32_t>(words));
        l.upload(*b, 1, std::span<const std::uint8_t>(odd));
        l.upload(*i, 0, std::span<const std::uint16_t>(indices));
        check(d->submit(l).has_value());
        std::array<std::uint32_t, 4> back{};
        check(d->read(*b, 16, std::as_writable_bytes(std::span(back))).has_value());
        check(back == words, std::format("{}", back));
        std::array<std::uint8_t, 3> back_odd{};
        check(d->read(*b, 1, std::as_writable_bytes(std::span(back_odd))).has_value());
        check(back_odd == odd, std::format("{}", back_odd));
        std::array<std::uint16_t, 3> back_indices{};
        check(d->read(*i, 0, std::as_writable_bytes(std::span(back_indices))).has_value());
        check(back_indices == indices, std::format("{}", back_indices));
    });

    s.test("what WebGL2 lacks is refused up front: storage, compute, a buffer both indices and vertices; invalid lists never reach it", [] {
        auto d = open_device();
        if (!d) return;
        auto st = d->create(TextureDesc{.width = 4, .height = 4, .usage = TextureUsage::compute_write | TextureUsage::sampled});
        check(!st && st.error().code == Errc::unsupported, st ? "made" : st.error().message);
        auto sb = d->create(BufferDesc{.size = 16, .usage = BufferUsage::storage_read});
        check(!sb && sb.error().code == Errc::unsupported);
        auto both = d->create(BufferDesc{.size = 16, .usage = BufferUsage::vertex | BufferUsage::index});
        check(!both && both.error().code == Errc::unsupported, both ? "made" : both.error().message);
        auto cs = d->create(ComputePipelineDesc{&cts::shaders::compute::pattern});
        check(!cs && cs.error().code == Errc::unsupported);
        CommandList bad;
        bad.draw(3);
        auto v = d->submit(bad);
        check(!v && v.error().code == Errc::invalid);
    });

    s.test("the omui.rhi conformance suite (level B): every case, exactly", [] {
        auto d = open_device();
        if (!d) return;
        check(cts::shader_formats().has(ShaderFormat::essl), "the suite's shaders were compiled to ESSL");
        const auto outcomes = cts::run(*d);
        check_eq(outcomes.size(), cts::cases().size());
        for (const cts::Outcome& o : outcomes) {
            std::println("  cts {:<16} {}", o.name, o.passed ? (o.detail.empty() ? "ok" : "ok — " + o.detail) : o.detail);
            check(o.passed, std::format("{}: {}", o.name, o.detail));
        }
    });

    return s.run(static_cast<int>(args.size()), args.data());
}
