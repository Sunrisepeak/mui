// omui.rhi on SDL_GPU, on whatever device this machine has (lavapipe in
// Linux CI). With OMUI_REQUIRE_GPU=1 (CI) a missing device fails; otherwise
// it is said and the cases pass vacuously. Uploads, clears, copies and
// readback, byte for byte; then the rhi conformance suite (openmui.rhi.cts),
// every case with the suite's own shaders.
import std;
import openmui.rhi;
import openmui.rhi.sdl;
import openmui.rhi.cts;
import mcpplibs.os;
import openmui.testing;

using namespace omui::testing;
using namespace omui::rhi;

namespace {

bool required() {
    return mcpplibs::os::env::get("OMUI_REQUIRE_GPU") == "1";
}

std::unique_ptr<Device> open_device() {
    sdl::Options o;
    o.formats = cts::shader_formats();  // what the suite's shaders were compiled to
    if (auto lib = mcpplibs::os::env::get("OMUI_VULKAN_LIBRARY")) o.vulkan_library = *lib;
    auto d = sdl::create(o);
    if (!d) {
        if (required()) check(false, std::format("OMUI_REQUIRE_GPU=1 and no device: {}", d.error().message));
        else std::println("no GPU device here ({}); set OMUI_REQUIRE_GPU=1 to make that a failure", d.error().message);
        return nullptr;
    }
    std::println("device: {} on {}{}", (*d)->caps().device, name((*d)->caps().backend), (*d)->caps().software ? " (software)" : "");
    return std::move(*d);
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"rhi.sdl"};

    s.test("a device: its backend, the shader formats it takes of those asked for", [] {
        auto d = open_device();
        if (!d) return;
        check(!d->caps().formats.empty(), "takes one of the formats asked for");
        check(!sdl::backends().empty());
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
        auto at = [&](int x, int y) {
            const auto* p = reinterpret_cast<const std::uint8_t*>(px->data()) + (y * 8 + x) * 4;
            return std::array<int, 4>{p[0], p[1], p[2], p[3]};
        };
        check(at(0, 0) == std::array<int, 4>{0, 0, 255, 255}, "the clear colour");
        check(at(3, 1) == std::array<int, 4>{255, 0, 0, 255} && at(4, 2) == std::array<int, 4>{10, 20, 30, 40}, "the copied texels");
        check(at(7, 3) == std::array<int, 4>{0, 0, 255, 255});
    });

    s.test("buffers: an upload at an offset, read back", [] {
        auto d = open_device();
        if (!d) return;
        const auto b = d->create(BufferDesc{.size = 64, .usage = BufferUsage::vertex});
        check(b.has_value());
        if (!b) return;
        std::array<std::uint32_t, 4> words{1, 2, 3, 0xdeadbeef};
        CommandList l;
        l.upload(*b, 16, std::span<const std::uint32_t>(words));
        check(d->submit(l).has_value());
        std::array<std::uint32_t, 4> back{};
        check(d->read(*b, 16, std::as_writable_bytes(std::span(back))).has_value());
        check(back == words, std::format("{}", back));
    });

    s.test("the omui.rhi conformance suite (level B): every case, exactly", [] {
        auto d = open_device();
        if (!d) return;
        const auto outcomes = cts::run(*d);
        check_eq(outcomes.size(), cts::cases().size());
        for (const cts::Outcome& o : outcomes) {
            std::println("  cts {:<16} {}", o.name, o.passed ? "ok" : o.detail);
            check(o.passed, std::format("{}: {}", o.name, o.detail));
        }
    });

    s.test("an invalid list never reaches SDL", [] {
        auto d = open_device();
        if (!d) return;
        CommandList l;
        l.draw(3);
        auto r = d->submit(l);
        check(!r && r.error().code == Errc::invalid);
    });

    return s.run(argc, argv);
}
