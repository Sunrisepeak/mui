// The mesh pass on this machine's device (xrender.mesh.gpu on SDL_GPU; lavapipe in Linux CI), held to the CPU
// reference (xrender.mesh) pixel by pixel: the same scene, the same extraction, the same options. With
// OMUI_REQUIRE_GPU=1 (CI) a missing device fails; otherwise it is said and the cases pass vacuously.
import std;
import mlibs.rhi;
import xrender.rhi.sdl;
import xrender.mesh.gpu;
import mlibs.image;
import mcpplibs.os;
import mcpplibs.mtest;

namespace sdl = xrender::rhi::sdl;
namespace mesh = xrender::mesh;
namespace scene = xrender::scene;
namespace math = mlibs::math;
namespace geom = mlibs::geom;
namespace rhi = mlibs::rhi;

using namespace mcpplibs::mtest;

namespace {

std::unique_ptr<rhi::Device> open_device() {
    sdl::Options options;
    options.formats = mesh::gpu::Pass::shader_formats();
    if (auto library = mcpplibs::os::env::get("OMUI_VULKAN_LIBRARY")) options.vulkan_library = *library;
    if (auto video = mcpplibs::os::env::get("OMUI_GPU_VIDEO_DRIVER")) options.video_driver = *video;
    auto device = sdl::create(options);
    if (!device) {
        if (mcpplibs::os::env::get("OMUI_REQUIRE_GPU") == "1") expect(false) << std::format("OMUI_REQUIRE_GPU=1 and no device: {}", device.error().message);
        else std::println("no GPU device here ({}); set OMUI_REQUIRE_GPU=1 to make that a failure", device.error().message);
        return nullptr;
    }
    return std::move(*device);
}

// A yard: a floor, a red box turned a little, a blue ball, a yellow post, a lamp that glows; a sun, the lamp's
// light, and a spot from above.
struct Yard {
    std::vector<geom::ClusteredMesh> made;
    std::vector<const geom::ClusteredMesh*> meshes;
    std::vector<mesh::Material> materials{{{0.8f, 0.2f, 0.15f}, {}}, {{0.5f, 0.5f, 0.5f}, {}}, {{0.15f, 0.4f, 0.8f}, {}}, {{0.9f, 0.75f, 0.2f}, {}}, {{0.1f, 0.1f, 0.1f}, {0.9f, 0.5f, 0.1f}}};
    scene::Scene world;
    Yard() {
        made.push_back(*geom::clustered(geom::box({1, 1, 1})));
        made.push_back(*geom::clustered(geom::sheet(200, 200, 4, 4)));
        made.push_back(*geom::clustered(geom::sphere(1, 32, 64), 32));
        made.push_back(*geom::clustered(geom::cylinder(0.7f, 2, 48), 32));
        for (const geom::ClusteredMesh& one : made) meshes.push_back(&one);
        const auto put = [&](scene::Id id, std::uint32_t which, std::uint32_t material, const math::Mat4& where) {
            expect(fatal(world.add({.id = id, .mesh = which, .material = material, .world = where, .bounds = geom::bounds(made[which].mesh)}).has_value()));
        };
        put(1, 1, 1, math::translation({0, -1, 0}));
        put(10, 0, 0, math::Transform{{-2.2f, 0, 0}, math::Quat::about({0, 1, 0}, 0.6f), {1, 1, 1}}.matrix());
        put(11, 2, 2, math::translation({0.4f, 0, 0.8f}));
        put(12, 3, 3, math::translation({2.6f, 0, -0.5f}));
        put(13, 2, 4, math::Transform{{1.2f, 1.6f, -1.5f}, {}, {0.25f, 0.25f, 0.25f}}.matrix());
        expect(world.add(scene::Light{.id = 1, .kind = scene::LightKind::sun, .colour = {1, 0.96f, 0.9f}, .intensity = 0.9f, .toward = {-0.4f, -1, -0.5f}}).has_value());
        expect(world.add(scene::Light{.id = 2, .kind = scene::LightKind::point, .colour = {1, 0.6f, 0.2f}, .intensity = 2.5f, .at = {1.2f, 1.6f, -1.5f}, .range = 6}).has_value());
        expect(world.add(scene::Light{.id = 3, .kind = scene::LightKind::spot, .colour = {0.4f, 0.7f, 1}, .intensity = 3, .at = {-2.2f, 4, 1}, .toward = {0, -1, -0.2f}, .range = 9, .cone = 0.5f}).has_value());
    }
    [[nodiscard]] scene::View view() const {
        return {.view = math::look_at({0.5f, 2.2f, 7.5f}, {0.2f, 0, 0}, {0, 1, 0}), .projection = math::perspective(1.0f, 16.0f / 9.0f, 0.5f, 100.0f)};
    }
};
constexpr std::uint32_t width = 480, height = 270;

// How far the device's picture is from the reference's, over the pixels something was drawn at.
struct Apart {
    std::size_t compared = 0, off = 0;  // pixels; of them, a channel further than `near` apart
    double      mean = 0;               // of the channels' differences
    int         worst = 0;
};
Apart apart(const mesh::Targets& reference, std::span<const std::byte> device, bool drawn_only, int near) {
    Apart all;
    double sum = 0;
    for (std::size_t pixel = 0; pixel < reference.ids.size(); ++pixel) {
        if (drawn_only && reference.ids[pixel].draw == mesh::none) continue;
        ++all.compared;
        int most = 0;
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const int difference = std::abs(static_cast<int>(reference.colour[pixel * 4 + channel]) - std::to_integer<int>(device[pixel * 4 + channel]));
            most = std::max(most, difference), sum += difference;
        }
        all.off += most > near, all.worst = std::max(all.worst, most);
    }
    all.mean = all.compared ? sum / (static_cast<double>(all.compared) * 3) : 0;
    return all;
}
void keep(std::span<const std::byte> rgba, std::string_view name) {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "openmui-tests" / "xrender-mesh-gpu";
    std::filesystem::create_directories(directory);
    const mlibs::image::View view{reinterpret_cast<const std::uint8_t*>(rgba.data()), static_cast<int>(width), static_cast<int>(height), static_cast<int>(width) * 4, mlibs::image::Alpha::straight};
    expect(mlibs::image::save_png(directory / std::format("{}.png", name), view).has_value());
}

struct Drawn {
    mesh::Targets            reference;
    std::vector<std::byte>   device;
    mesh::gpu::Stats         stats;
};
std::optional<Drawn> both(rhi::Device& device, const Yard& yard, mesh::Options options) {
    Drawn out;
    scene::Extracted seen;
    scene::extract(yard.world, yard.view(), seen);
    options.shadows = false;
    out.reference.resize(static_cast<int>(width), static_cast<int>(height));
    mesh::Pass reference;
    reference.draw(yard.world, seen, yard.view(), {yard.meshes, yard.materials, {}}, out.reference, options);

    auto pass = mesh::gpu::Pass::make(device);
    expect(fatal(pass.has_value())) << (pass ? "" : pass.error().message);
    const auto loaded = pass->load(yard.meshes);
    expect(fatal(loaded.has_value())) << (loaded ? "" : loaded.error().message);
    const auto colour = device.create(rhi::TextureDesc{rhi::Format::rgba8_unorm, width, height, rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, "mesh.colour"});
    const auto depth = device.create(rhi::TextureDesc{rhi::Format::d32_float, width, height, rhi::TextureUsage::depth_target, "mesh.depth"});
    expect(fatal(colour.has_value() && depth.has_value())) << (colour ? (depth ? "" : depth.error().message) : colour.error().message);
    rhi::CommandList list;
    pass->record(list, yard.world, seen, yard.view(), yard.materials, *colour, *depth, width, height, options);
    out.stats = pass->last();
    const auto submitted = device.submit(list);
    expect(fatal(submitted.has_value())) << (submitted ? "" : submitted.error().message);
    expect(device.wait(*submitted).has_value());
    auto pixels = device.read(*colour, rhi::Region{0, 0, width, height});
    expect(fatal(pixels.has_value())) << (pixels ? "" : pixels.error().message);
    out.device = std::move(*pixels);
    static_cast<void>(device.wait_idle());
    device.destroy(*colour), device.destroy(*depth);
    return out;
}

}  // namespace

UTest cases{"mesh.gpu.sdl", [] {
    "the yard on the device is the reference's yard: every draw drawn, and the picture within a few levels at all but the pixels of an edge"_test = [] {
        auto device = open_device();
        if (!device) return;
        const Yard yard;
        const auto drawn = both(*device, yard, {});
        if (!drawn) return;
        keep(drawn->device, "yard");
        expect(eq(drawn->stats.draws, 5u) && eq(drawn->stats.missing, 0u) && eq(drawn->stats.lamps, 3u));
        const Apart all = apart(drawn->reference, drawn->device, false, 6);
        std::println("the yard, {} × {}, on {}: {} of {} pixels further than 6 levels from the reference ({:.2f}%), mean difference {:.3f} levels, worst {}", width, height,
                     device->caps().device, all.off, all.compared, 100.0 * static_cast<double>(all.off) / static_cast<double>(all.compared), all.mean, all.worst);
        // Measured on lavapipe: 5 pixels, mean 0.008. The bound is a thousandth of the picture, and a tenth of a level.
        expect(lt(static_cast<double>(all.off), 0.001 * static_cast<double>(all.compared))) << "edges: a device samples a pixel at its middle by its own rule";
        expect(lt(all.mean, 0.1));
    };

    "and in fog: what is drawn is the reference's, surface by surface, and the sky behind it clears upward as the reference's does"_test = [] {
        auto device = open_device();
        if (!device) return;
        const Yard yard;
        const auto drawn = both(*device, yard, {.fog = {.colour = {0.5f, 0.6f, 0.7f}, .density = 0.08f, .base = -1, .falloff = 0.35f}});
        if (!drawn) return;
        keep(drawn->device, "yard-fog");
        const Apart surfaces = apart(drawn->reference, drawn->device, true, 6);
        std::println("the yard in fog: {} of {} drawn pixels further than 6 levels from the reference ({:.2f}%), mean {:.3f}, worst {}", surfaces.off, surfaces.compared,
                     100.0 * static_cast<double>(surfaces.off) / static_cast<double>(surfaces.compared), surfaces.mean, surfaces.worst);
        expect(lt(static_cast<double>(surfaces.off), 0.001 * static_cast<double>(surfaces.compared)));
        expect(lt(surfaces.mean, 0.1));
        // And the sky: every pixel of the picture, what nothing drew too — clearing upward as the reference's does.
        const Apart all = apart(drawn->reference, drawn->device, false, 6);
        std::println("    with the sky: {} of {} pixels further than 6 levels ({:.2f}%), mean {:.3f}, worst {}", all.off, all.compared, 100.0 * static_cast<double>(all.off) / static_cast<double>(all.compared),
                     all.mean, all.worst);
        expect(lt(static_cast<double>(all.off), 0.001 * static_cast<double>(all.compared)));
        expect(lt(all.mean, 0.1));
    };

    "who drew a pixel, asked of the device: its ids picture names the reference's draw at every pixel but an edge's; and a pixel of the ball is the ball"_test = [] {
        auto device = open_device();
        if (!device) return;
        const Yard yard;
        scene::Extracted seen;
        scene::extract(yard.world, yard.view(), seen);
        mesh::Targets reference;
        reference.resize(static_cast<int>(width), static_cast<int>(height));
        mesh::Pass on_cpu;
        on_cpu.draw(yard.world, seen, yard.view(), {yard.meshes, yard.materials, {}}, reference, {.shadows = false});

        auto pass = mesh::gpu::Pass::make(*device);
        expect(fatal(pass.has_value())) << (pass ? "" : pass.error().message);
        expect(fatal(pass->load(yard.meshes).has_value()));
        const auto ids = device->create(rhi::TextureDesc{rhi::Format::r32_uint, width, height, rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, "mesh.ids"});
        const auto depth = device->create(rhi::TextureDesc{rhi::Format::d32_float, width, height, rhi::TextureUsage::depth_target, "mesh.ids.depth"});
        expect(fatal(ids.has_value() && depth.has_value())) << (ids ? (depth ? "" : depth.error().message) : ids.error().message);
        rhi::CommandList list;
        pass->record_ids(list, yard.world, seen, yard.view(), *ids, *depth, width, height);
        const auto submitted = device->submit(list);
        expect(fatal(submitted.has_value())) << (submitted ? "" : submitted.error().message);
        expect(device->wait(*submitted).has_value());
        const auto pixels = device->read(*ids, rhi::Region{0, 0, width, height});
        expect(fatal(pixels.has_value())) << (pixels ? "" : pixels.error().message);
        expect(eq(pixels->size(), std::size_t{width} * height * 4));

        std::size_t other = 0, drawn = 0;
        for (std::uint32_t y = 0; y < height; ++y)
            for (std::uint32_t x = 0; x < width; ++x) {
                const std::uint32_t said = mesh::gpu::who(*pixels, width, x, y), is = reference.ids[static_cast<std::size_t>(y) * width + x].draw;
                drawn += said != mesh::none, other += said != is;
            }
        std::println("who drew each pixel, on the device: {} pixels drawn; {} of {} name another draw than the reference ({:.3f}%)", drawn, other, std::size_t{width} * height,
                     100.0 * static_cast<double>(other) / (static_cast<double>(width) * height));
        expect(lt(static_cast<double>(other), 0.001 * width * height)) << "an edge's pixels";
        // The middle of the ball, as the scene's own id: draw → instance → id.
        const std::optional<mesh::Source> ball = mesh::at(reference, 225, 140);
        const std::uint32_t said = mesh::gpu::who(*pixels, width, 225, 140);
        expect(fatal(ball.has_value() && said != mesh::none && said < seen.draws.size()));
        expect(eq(yard.world.instances()[seen.draws[said].instance].id, scene::Id{11}) && eq(ball->instance, scene::Id{11}));
        static_cast<void>(device->wait_idle());
        device->destroy(*ids), device->destroy(*depth);
    };
}};
