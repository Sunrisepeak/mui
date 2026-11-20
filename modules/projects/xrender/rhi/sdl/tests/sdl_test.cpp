// omui.rhi on SDL_GPU, on whatever device this machine has (lavapipe in
// Linux CI). With OMUI_REQUIRE_GPU=1 (CI) a missing device fails; otherwise
// it is said and the cases pass vacuously. Uploads, clears, copies and
// readback, byte for byte; then the rhi conformance suite (openmui.rhi.cts),
// every case with the suite's own shaders.
import std;
import mlibs.rhi;
import xrender.rhi.sdl;
import mlibs.rhi.cts;
import mlibs.testing;
import mcpplibs.mtest;

namespace sdl = xrender::rhi::sdl;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
using namespace mlibs::rhi;

namespace {

bool required() {
    return xrender::rhi::sdl::device_required();
}

std::unique_ptr<Device> open_device() {
    sdl::Options o;
    o.formats = cts::shader_formats();  // what the suite's shaders were compiled to
    o = environment_options(std::move(o));  // OMUI_VULKAN_LIBRARY, OMUI_GPU_VIDEO_DRIVER
    auto d = sdl::create(o);
    if (!d) {
        if (required()) expect(false) << std::format("OMUI_REQUIRE_GPU=1 and no device: {}", d.error().message);
        else std::println("no GPU device here ({}); set OMUI_REQUIRE_GPU=1 to make that a failure", d.error().message);
        return nullptr;
    }
    std::println("device: {} on {}{}", (*d)->caps().device, name((*d)->caps().backend), (*d)->caps().software ? " (software)" : "");
    return std::move(*d);
}

}  // namespace

UTest cases{"rhi.sdl", [] {

    "hardware_only: the loader told to load the drivers of the machine's GPUs, or every one but those that draw on the CPU"_test = [] {
        const auto choice = [](std::vector<std::uint32_t> vendors) { return sdl::vulkan_driver_choice(vendors); };
        const auto nvidia = choice({0x10de});
        expect(nvidia.variable == "VK_LOADER_DRIVERS_SELECT" && nvidia.globs == "nvidia_icd*,nouveau_icd*") << nvidia.globs;
        const auto laptop = choice({0x8086, 0x10de, 0x8086});
        expect(laptop.variable == "VK_LOADER_DRIVERS_SELECT" && laptop.globs == "intel_icd*,intel_hasvk_icd*,nvidia_icd*,nouveau_icd*") << laptop.globs;
        for (const auto& unknown : {choice({}), choice({0x10de, 0x15ad})}) {
            expect(unknown.variable == "VK_LOADER_DRIVERS_DISABLE" && unknown.globs.contains("lvp_icd*") && unknown.globs.contains("swiftshader"))
                << "no GPU found, or one of a vendor not known here: every driver but the CPU's";
        }
        for (const std::uint32_t vendor : sdl::display_vendors()) expect(vendor != 0) << "this machine's display vendors read";
    };

    "a device: its backend, the shader formats it takes of those asked for"_test = [] {
        auto d = open_device();
        if (!d) return;
        expect(!d->caps().formats.empty()) << "takes one of the formats asked for";
        expect(!sdl::backends().empty());
    };

    "a device made on a thread of its own (create_async): taken once, it draws; one not taken is let go"_test = [] {
        sdl::Options o;
        o.formats = cts::shader_formats();
        o = environment_options(std::move(o));
        auto making = sdl::create_async(o);
        expect(making.valid());
        auto made = making.get();
        if (!made) {
            if (required()) expect(false) << std::format("OMUI_REQUIRE_GPU=1 and no device: {}", made.error().message);
            return;
        }
        expect(!making.valid() && !making.get().has_value()) << "taken once";
        const auto target = (*made)->create(TextureDesc{.width = 4, .height = 4, .usage = TextureUsage::color_target | TextureUsage::sampled});
        expect(fatal(target.has_value()));
        CommandList l;
        l.begin_render({*target, LoadOp::clear, {1.0f, 0.0f, 0.0f, 1.0f}});
        l.end_render();
        auto sub = (*made)->submit(l);
        expect(fatal(sub.has_value()));
        expect((*made)->wait(*sub).has_value());
        auto px = (*made)->read(*target, Region{0, 0, 4, 4});
        expect(px.has_value() && static_cast<std::uint8_t>((*px)[0]) == 255 && static_cast<std::uint8_t>((*px)[1]) == 0) << "it draws";
        (*made)->destroy(*target);
        { auto untaken = sdl::create_async(o); }  // waited for and let go, its video released
        auto again = sdl::create(o);
        expect(again.has_value()) << "SDL's video still right for the next device";
    };

    "upload, clear, copy, read back: the bytes, exactly"_test = [] {
        auto d = open_device();
        if (!d) return;
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

    "buffers: an upload at an offset, read back"_test = [] {
        auto d = open_device();
        if (!d) return;
        const auto b = d->create(BufferDesc{.size = 64, .usage = BufferUsage::vertex});
        expect(b.has_value());
        if (!b) return;
        std::array<std::uint32_t, 4> words{1, 2, 3, 0xdeadbeef};
        CommandList l;
        l.upload(*b, 16, std::span<const std::uint32_t>(words));
        expect(d->submit(l).has_value());
        std::array<std::uint32_t, 4> back{};
        expect(d->read(*b, 16, std::as_writable_bytes(std::span(back))).has_value());
        expect(back == words) << std::format("{}", back);
    };

    "the omui.rhi conformance suite (level B): every case, exactly"_test = [] {
        auto d = open_device();
        if (!d) return;
        // The suite's cases, this program's own: <device>/rhi.clear, …
        cts::declare(*d, {.subject = d->caps().device});
    };

    "an invalid list never reaches SDL"_test = [] {
        auto d = open_device();
        if (!d) return;
        CommandList l;
        l.draw(3);
        auto r = d->submit(l);
        expect(!r && r.error().code == Errc::invalid);
    };
}};
