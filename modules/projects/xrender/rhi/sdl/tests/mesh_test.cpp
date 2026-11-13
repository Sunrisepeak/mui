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
    std::vector<geom::Bvh> grown;
    std::vector<const geom::Bvh*> trees;
    // `sun_only`: no lamp and no spot — the light whose shadows the device has.
    explicit Yard(bool sun_only = false, bool polished = false) {
        if (polished) materials[2] = {{0.15f, 0.4f, 0.8f}, {}, 0.25f, 0}, materials[3] = {{1.0f, 0.77f, 0.34f}, {}, 0.3f, 1};
        made.push_back(*geom::clustered(geom::box({1, 1, 1})));
        made.push_back(*geom::clustered(geom::sheet(200, 200, 4, 4)));
        made.push_back(*geom::clustered(geom::sphere(1, 32, 64), 32));
        made.push_back(*geom::clustered(geom::cylinder(0.7f, 2, 48), 32));
        for (const geom::ClusteredMesh& one : made) meshes.push_back(&one), grown.push_back(geom::bvh_of(one.mesh));
        for (const geom::Bvh& tree : grown) trees.push_back(&tree);
        const auto put = [&](scene::Id id, std::uint32_t which, std::uint32_t material, const math::Mat4& where) {
            expect(fatal(world.add({.id = id, .mesh = which, .material = material, .world = where, .bounds = geom::bounds(made[which].mesh)}).has_value()));
        };
        put(1, 1, 1, math::translation({0, -1, 0}));
        put(10, 0, 0, math::Transform{{-2.2f, 0, 0}, math::Quat::about({0, 1, 0}, 0.6f), {1, 1, 1}}.matrix());
        put(11, 2, 2, math::translation({0.4f, 0, 0.8f}));
        put(12, 3, 3, math::translation({2.6f, 0, -0.5f}));
        // The lamp's globe: the light is inside it, so it is not in its own light's way.
        expect(world.add({.id = 13, .mesh = 2, .material = 4, .world = math::Transform{{1.2f, 1.6f, -1.5f}, {}, {0.25f, 0.25f, 0.25f}}.matrix(), .bounds = geom::bounds(made[2].mesh), .casts = false}).has_value());
        expect(world.add(scene::Light{.id = 1, .kind = scene::LightKind::sun, .colour = {1, 0.96f, 0.9f}, .intensity = 0.9f, .toward = {-0.4f, -1, -0.5f}}).has_value());
        if (sun_only) return;
        expect(world.add(scene::Light{.id = 2, .kind = scene::LightKind::point, .colour = {1, 0.6f, 0.2f}, .intensity = 2.5f, .at = {1.2f, 1.6f, -1.5f}, .range = 6}).has_value());
        expect(world.add(scene::Light{.id = 3, .kind = scene::LightKind::spot, .colour = {0.4f, 0.7f, 1}, .intensity = 3, .at = {-2.2f, 4, 1}, .toward = {0, -1, -0.2f}, .range = 9, .cone = 0.5f}).has_value());
    }
    // The harbour of xrender.mesh's own test: a wet quay at night, containers, bollards, three lamps on posts and a
    // spot, a faint moon.
    struct AtNight {};
    explicit Yard(AtNight) {
        materials = {{{0.8f, 0.2f, 0.15f}, {}}, {{0.5f, 0.5f, 0.5f}, {}}, {{0.15f, 0.4f, 0.8f}, {}}, {{0.15f, 0.4f, 0.8f}, {}, 0.25f, 0}, {{0.05f, 0.055f, 0.065f}, {}, 0.28f, 0},
                     {{0.1f, 0.1f, 0.1f}, {9.0f, 5.0f, 1.6f}}, {{0.1f, 0.1f, 0.1f}, {3.0f, 5.0f, 8.0f}}, {{0.25f, 0.25f, 0.27f}, {}, 0.45f, 1}};
        made.push_back(*geom::clustered(geom::box({1, 1, 1})));
        made.push_back(*geom::clustered(geom::sheet(200, 200, 4, 4)));
        made.push_back(*geom::clustered(geom::sphere(1, 32, 64), 32));
        made.push_back(*geom::clustered(geom::cylinder(0.7f, 2, 48), 32));
        for (const geom::ClusteredMesh& one : made) meshes.push_back(&one), grown.push_back(geom::bvh_of(one.mesh));
        for (const geom::Bvh& tree : grown) trees.push_back(&tree);
        const auto put = [&](scene::Id id, std::uint32_t which, std::uint32_t material, const math::Mat4& where, bool casts = true) {
            expect(fatal(world.add({.id = id, .mesh = which, .material = material, .world = where, .bounds = geom::bounds(made[which].mesh), .casts = casts}).has_value()));
        };
        const auto scaled = [](math::Vec3 at, math::Vec3 size, float turned = 0) { return math::Transform{at, math::Quat::about({0, 1, 0}, turned), size}.matrix(); };
        put(1, 1, 4, math::translation({0, -1, 0}));
        put(10, 0, 0, scaled({-3.6f, -0.2f, -2.2f}, {1.7f, 0.8f, 0.8f}, 0.25f));
        put(11, 0, 2, scaled({-3.4f, 1.4f, -2.3f}, {1.7f, 0.8f, 0.8f}, 0.1f));
        put(12, 0, 1, scaled({4.2f, -0.2f, -4.0f}, {1.7f, 0.8f, 0.8f}, -0.5f));
        put(13, 2, 3, scaled({1.0f, -0.55f, 1.6f}, {0.45f, 0.45f, 0.45f}));
        std::uint32_t next = 20, light = 1;
        for (const float x : {-4.5f, -1.5f, 1.5f, 4.5f}) put(next++, 3, 7, scaled({x, -0.72f, 3.2f}, {0.32f, 0.28f, 0.32f}));
        struct Lamp {
            math::Vec3 foot;
            bool warm;
        };
        for (const Lamp lamp : {Lamp{{-1.4f, -1, -0.8f}, true}, Lamp{{2.4f, -1, 0.6f}, false}, Lamp{{0.6f, -1, -6.5f}, true}}) {
            const math::Vec3 top = lamp.foot + math::Vec3{0, 4.15f, 0};
            put(next++, 3, 7, scaled(lamp.foot + math::Vec3{0, 2, 0}, {0.1f, 2, 0.1f}));
            put(next++, 2, lamp.warm ? 5 : 6, scaled(top, {0.2f, 0.2f, 0.2f}), false);
            expect(world.add(scene::Light{.id = light++, .kind = scene::LightKind::point, .colour = lamp.warm ? math::Vec3{1, 0.62f, 0.25f} : math::Vec3{0.55f, 0.75f, 1}, .intensity = 3.2f, .at = top, .range = 10}).has_value());
        }
        expect(world.add(scene::Light{.id = light++, .kind = scene::LightKind::spot, .colour = {1, 0.97f, 0.9f}, .intensity = 5, .at = {5.5f, 6, -1}, .toward = math::normalized(math::Vec3{-0.45f, -1, -0.15f}), .range = 14, .cone = 0.3f}).has_value());
        expect(world.add(scene::Light{.id = light++, .kind = scene::LightKind::sun, .colour = {0.6f, 0.7f, 1}, .intensity = 0.05f, .toward = {0.3f, -1, 0.4f}}).has_value());
        eye = {-0.6f, 2.3f, 10.5f}, target = {0.2f, 0.2f, 0};
    }
    math::Vec3 eye{0.5f, 2.2f, 7.5f}, target{0.2f, 0, 0};
    [[nodiscard]] scene::View view() const { return {.view = math::look_at(eye, target, {0, 1, 0}), .projection = math::perspective(1.0f, 16.0f / 9.0f, 0.5f, 100.0f)}; }
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

    "the sun's shadows by a map are the reference's traced shadows: the same pixels dark, but for a line of them along a shadow's edge"_test = [] {
        auto device = open_device();
        if (!device) return;
        const Yard yard(true);
        scene::Extracted seen;
        scene::extract(yard.world, yard.view(), seen);
        mesh::Targets traced, lit;
        traced.resize(static_cast<int>(width), static_cast<int>(height)), lit.resize(static_cast<int>(width), static_cast<int>(height));
        mesh::Pass on_cpu;
        on_cpu.draw(yard.world, seen, yard.view(), {yard.meshes, yard.materials, yard.trees}, traced, {.shadows = true});
        on_cpu.draw(yard.world, seen, yard.view(), {yard.meshes, yard.materials, {}}, lit, {.shadows = false});

        auto pass = mesh::gpu::Pass::make(*device);
        expect(fatal(pass.has_value())) << (pass ? "" : pass.error().message);
        expect(fatal(pass->load(yard.meshes).has_value()));
        mesh::gpu::Shadows shadows{.size = 2048, .distance = 30};
        const auto colour = device->create(rhi::TextureDesc{rhi::Format::rgba8_unorm, width, height, rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, "mesh.colour"});
        const auto depth = device->create(rhi::TextureDesc{rhi::Format::d32_float, width, height, rhi::TextureUsage::depth_target, "mesh.depth"});
        const auto map = device->create(rhi::TextureDesc{rhi::Format::r32_float, shadows.size, shadows.size, rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, "mesh.shadow"});
        const auto map_depth = device->create(rhi::TextureDesc{rhi::Format::d32_float, shadows.size, shadows.size, rhi::TextureUsage::depth_target, "mesh.shadow.depth"});
        expect(fatal(colour.has_value() && depth.has_value() && map.has_value() && map_depth.has_value())) << (map ? "" : map.error().message);
        shadows.map = *map, shadows.depth = *map_depth;
        rhi::CommandList list;
        pass->record(list, yard.world, seen, yard.view(), yard.materials, *colour, *depth, width, height, {.shadows = true}, &shadows);
        const auto submitted = device->submit(list);
        expect(fatal(submitted.has_value())) << (submitted ? "" : submitted.error().message);
        expect(device->wait(*submitted).has_value());
        const auto pixels = device->read(*colour, rhi::Region{0, 0, width, height});
        expect(fatal(pixels.has_value())) << (pixels ? "" : pixels.error().message);
        keep(*pixels, "yard-shadows");
        expect(eq(pass->last().casters, 4u)) << "the floor, the box, the ball, the post: the lamp's globe casts nothing";

        // Where the reference is in shadow (darker than itself with no shadows), and where the device is.
        std::size_t traced_dark = 0, device_dark = 0, both_dark = 0;
        for (std::size_t pixel = 0; pixel < traced.ids.size(); ++pixel) {
            const auto level = [&](std::span<const std::uint8_t> from) { return from[pixel * 4] + from[pixel * 4 + 1] + from[pixel * 4 + 2]; };
            const int unshadowed = level(lit.colour);
            const int on_device = std::to_integer<int>((*pixels)[pixel * 4]) + std::to_integer<int>((*pixels)[pixel * 4 + 1]) + std::to_integer<int>((*pixels)[pixel * 4 + 2]);
            const bool dark_traced = level(traced.colour) < unshadowed - 24, dark_device = on_device < unshadowed - 24;
            traced_dark += dark_traced, device_dark += dark_device, both_dark += dark_traced && dark_device;
        }
        const Apart all = apart(traced, *pixels, false, 6);
        std::println("the sun's shadows, a map of {}² against rays: {} pixels in shadow by the reference, {} by the device, {} by both; {} of {} pixels further than 6 levels apart ({:.2f}%), mean {:.3f}",
                     shadows.size, traced_dark, device_dark, both_dark, all.off, all.compared, 100.0 * static_cast<double>(all.off) / static_cast<double>(all.compared), all.mean);
        // Measured on lavapipe: 1002 by the reference, 1036 by the device, 985 by both; 71 pixels apart.
        expect(gt(traced_dark, std::size_t{800})) << "the box, the ball and the post each throw one";
        expect(gt(static_cast<double>(both_dark), 0.97 * static_cast<double>(traced_dark))) << "what the rays shadow, the map shadows";
        expect(gt(static_cast<double>(both_dark), 0.93 * static_cast<double>(device_dark))) << "and little else: a texel's width along an edge";
        expect(lt(static_cast<double>(all.off), 0.002 * static_cast<double>(all.compared)));
        static_cast<void>(device->wait_idle());
        device->destroy(*colour), device->destroy(*depth), device->destroy(*map), device->destroy(*map_depth);
    };

    "a polished ball and a gold post on the device are the reference's: the highlights where it has them, as bright"_test = [] {
        auto device = open_device();
        if (!device) return;
        const Yard yard(false, true);
        const auto drawn = both(*device, yard, {.environment = {.on = true}});  // with a sky about it for the gold to mirror
        if (!drawn) return;
        keep(drawn->device, "yard-polished");
        const Apart all = apart(drawn->reference, drawn->device, false, 6);
        std::println("the yard with a polished ball and a gold post: {} of {} pixels further than 6 levels from the reference ({:.2f}%), mean {:.3f}, worst {}", all.off, all.compared,
                     100.0 * static_cast<double>(all.off) / static_cast<double>(all.compared), all.mean, all.worst);
        expect(lt(static_cast<double>(all.off), 0.001 * static_cast<double>(all.compared)));
        expect(lt(all.mean, 0.1));
    };
    "a lamp brighter than the display glows on the device as in the reference: the light kept before the curve, what is above it spread, then shown"_test = [] {
        auto device = open_device();
        if (!device) return;
        Yard yard;
        yard.materials[4].emissive = {9.0f, 5.0f, 1.6f};
        const mesh::Bloom bloom{.threshold = 1, .radius = 6, .strength = 0.6f};
        scene::Extracted seen;
        scene::extract(yard.world, yard.view(), seen);
        mesh::Targets reference;
        reference.resize(static_cast<int>(width), static_cast<int>(height));
        mesh::Pass on_cpu;
        on_cpu.draw(yard.world, seen, yard.view(), {yard.meshes, yard.materials, {}}, reference, {.shadows = false});
        const std::vector<std::uint8_t> plain = reference.colour;
        mesh::Glow glow_on_cpu;
        const auto told = glow_on_cpu.show(reference.light, static_cast<int>(width), static_cast<int>(height), bloom, reference.colour);

        auto pass = mesh::gpu::Pass::make(*device, false, true);
        expect(fatal(pass.has_value())) << (pass ? "" : pass.error().message);
        expect(fatal(pass->load(yard.meshes).has_value()));
        auto glow = mesh::gpu::Glow::make(*device);
        expect(fatal(glow.has_value())) << (glow ? "" : glow.error().message);
        const auto of_light = [&](std::string_view label) {
            const auto made = device->create(rhi::TextureDesc{rhi::Format::rgba16_float, width, height, rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, label});
            expect(fatal(made.has_value())) << (made ? "" : made.error().message);
            return *made;
        };
        const rhi::Texture light = of_light("mesh.light"), one = of_light("mesh.glow.one"), other = of_light("mesh.glow.other");
        const auto colour = device->create(rhi::TextureDesc{rhi::Format::rgba8_unorm, width, height, rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, "mesh.colour"});
        const auto depth = device->create(rhi::TextureDesc{rhi::Format::d32_float, width, height, rhi::TextureUsage::depth_target, "mesh.depth"});
        expect(fatal(colour.has_value() && depth.has_value()));
        const auto shown_with = [&](const mesh::Bloom& how) {
            rhi::CommandList list;
            pass->record(list, yard.world, seen, yard.view(), yard.materials, light, *depth, width, height, {.shadows = false});
            glow->record(list, light, one, other, *colour, width, height, how);
            const auto submitted = device->submit(list);
            expect(fatal(submitted.has_value())) << (submitted ? "" : submitted.error().message);
            expect(device->wait(*submitted).has_value());
            auto pixels = device->read(*colour, rhi::Region{0, 0, width, height});
            expect(fatal(pixels.has_value())) << (pixels ? "" : pixels.error().message);
            return std::move(*pixels);
        };
        const std::vector<std::byte> glowing = shown_with(bloom), without = shown_with({});
        keep(glowing, "yard-glow");
        const Apart all = apart(reference, glowing, false, 6);
        mesh::Targets unspread = reference;
        unspread.colour = plain;
        const Apart bare = apart(unspread, without, false, 6);
        std::size_t lighter = 0, lighter_on_cpu = 0;
        for (std::size_t pixel = 0; pixel < plain.size() / 4; ++pixel) {
            lighter += std::to_integer<int>(glowing[pixel * 4]) > std::to_integer<int>(without[pixel * 4]) + 2;
            lighter_on_cpu += reference.colour[pixel * 4] > plain[pixel * 4] + 2;
        }
        std::println("the yard with a lamp of (9, 5, 1.6): {} pixels brighter than the display; lighter by its glow, {} on the device and {} in the reference; {} of {} pixels further than 6 levels from the reference ({:.2f}%), mean {:.3f}, worst {}; with no radius {} further",
                     told.bright, lighter, lighter_on_cpu, all.off, all.compared, 100.0 * static_cast<double>(all.off) / static_cast<double>(all.compared), all.mean, all.worst, bare.off);
        expect(gt(told.bright, 20u));
        expect(gt(lighter, std::size_t{1000})) << "it shows about what is bright";
        expect(lt(lighter > lighter_on_cpu ? lighter - lighter_on_cpu : lighter_on_cpu - lighter, lighter_on_cpu / 50)) << "over as many pixels as in the reference";
        expect(lt(static_cast<double>(all.off), 0.001 * static_cast<double>(all.compared)));
        expect(lt(static_cast<double>(bare.off), 0.001 * static_cast<double>(bare.compared))) << "with no radius: the light through the curve, the plain pass's picture";
        static_cast<void>(device->wait_idle());
        device->destroy(light), device->destroy(one), device->destroy(other), device->destroy(*colour), device->destroy(*depth);
    };
    "a lamp's and a spot's shadows by their maps are the reference's traced shadows: the same pixels dark, but for a line of them along a shadow's edge"_test = [] {
        auto device = open_device();
        if (!device) return;
        const Yard yard;
        scene::Extracted seen;
        scene::extract(yard.world, yard.view(), seen);
        mesh::Targets traced, lit;
        traced.resize(static_cast<int>(width), static_cast<int>(height)), lit.resize(static_cast<int>(width), static_cast<int>(height));
        mesh::Pass on_cpu;
        on_cpu.draw(yard.world, seen, yard.view(), {yard.meshes, yard.materials, yard.trees}, traced, {.shadows = true});
        on_cpu.draw(yard.world, seen, yard.view(), {yard.meshes, yard.materials, {}}, lit, {.shadows = false});

        auto pass = mesh::gpu::Pass::make(*device);
        expect(fatal(pass.has_value())) << (pass ? "" : pass.error().message);
        expect(fatal(pass->load(yard.meshes).has_value()));
        mesh::gpu::Shadows shadows{.size = 2048, .distance = 30, .lamp_size = 512, .lamps = 2};
        const auto target = [&](rhi::Format format, std::uint32_t across, std::uint32_t down, std::string_view label) {
            const auto made = device->create(rhi::TextureDesc{format, across, down, format == rhi::Format::d32_float ? rhi::TextureUsage::depth_target : rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, label});
            expect(fatal(made.has_value())) << (made ? "" : made.error().message);
            return *made;
        };
        const rhi::Texture colour = target(rhi::Format::rgba8_unorm, width, height, "mesh.colour"), depth = target(rhi::Format::d32_float, width, height, "mesh.depth");
        shadows.map = target(rhi::Format::r32_float, shadows.size, shadows.size, "mesh.shadow"), shadows.depth = target(rhi::Format::d32_float, shadows.size, shadows.size, "mesh.shadow.depth");
        shadows.lamps_map = target(rhi::Format::r32_float, shadows.lamp_size * 6, shadows.lamp_size * shadows.lamps, "mesh.shadow.lamps");
        shadows.lamps_depth = target(rhi::Format::d32_float, shadows.lamp_size * 6, shadows.lamp_size * shadows.lamps, "mesh.shadow.lamps.depth");
        rhi::CommandList list;
        pass->record(list, yard.world, seen, yard.view(), yard.materials, colour, depth, width, height, {.shadows = true}, &shadows);
        const auto submitted = device->submit(list);
        expect(fatal(submitted.has_value())) << (submitted ? "" : submitted.error().message);
        expect(device->wait(*submitted).has_value());
        const auto pixels = device->read(colour, rhi::Region{0, 0, width, height});
        expect(fatal(pixels.has_value())) << (pixels ? "" : pixels.error().message);
        keep(*pixels, "yard-shadows-lamps");
        expect(eq(pass->last().lamps_shadowed, 2u)) << "the lamp and the spot";
        std::size_t traced_dark = 0, device_dark = 0, both_dark = 0;
        for (std::size_t pixel = 0; pixel < traced.ids.size(); ++pixel) {
            const auto level = [&](std::span<const std::uint8_t> from) { return from[pixel * 4] + from[pixel * 4 + 1] + from[pixel * 4 + 2]; };
            const int unshadowed = level(lit.colour);
            const int on_device = std::to_integer<int>((*pixels)[pixel * 4]) + std::to_integer<int>((*pixels)[pixel * 4 + 1]) + std::to_integer<int>((*pixels)[pixel * 4 + 2]);
            const bool dark_traced = level(traced.colour) < unshadowed - 12, dark_device = on_device < unshadowed - 12;
            traced_dark += dark_traced, device_dark += dark_device, both_dark += dark_traced && dark_device;
        }
        const Apart all = apart(traced, *pixels, false, 6);
        std::println("the sun's, a lamp's and a spot's shadows (six views of {}² a lamp, {} drawn): {} pixels dark in the reference, {} on the device, {} in both; {} of {} pixels further than 6 levels ({:.2f}%), mean {:.3f}",
                     shadows.lamp_size, pass->last().lamp_views, traced_dark, device_dark, both_dark, all.off, all.compared, 100.0 * static_cast<double>(all.off) / static_cast<double>(all.compared), all.mean);
        expect(gt(traced_dark, std::size_t{2000}));
        expect(gt(static_cast<double>(both_dark), 0.97 * static_cast<double>(traced_dark))) << "what the reference shadows, the device shadows";
        expect(gt(static_cast<double>(both_dark), 0.93 * static_cast<double>(device_dark))) << "and little else";
        expect(lt(static_cast<double>(all.off), 0.002 * static_cast<double>(all.compared)));
        expect(lt(pass->last().lamp_views, 12u)) << "a spot's views are only those its cone looks into";
        static_cast<void>(device->wait_idle());
        device->destroy(colour), device->destroy(depth), device->destroy(shadows.map), device->destroy(shadows.depth), device->destroy(shadows.lamps_map), device->destroy(shadows.lamps_depth);
    };
    "the harbour at night on the device is the reference's: lamps' and a spot's shadows by maps for its traced ones, the wet quay, fog, the lamps' glow"_test = [] {
        auto device = open_device();
        if (!device) return;
        const Yard harbour{Yard::AtNight{}};
        const mesh::Options night{.background = {0.004f, 0.005f, 0.01f}, .ambient = {0.006f, 0.007f, 0.012f}, .shadows = true, .fog = {.colour = {0.02f, 0.026f, 0.04f}, .density = 0.045f, .base = -1, .falloff = 0.35f},
                                  .environment = {.on = true, .above = {0.01f, 0.014f, 0.03f}, .level = {0.035f, 0.04f, 0.06f}, .below = {0.004f, 0.004f, 0.005f}}};
        const mesh::Bloom bloom{.threshold = 1, .radius = 7, .strength = 0.6f};
        scene::Extracted seen;
        scene::extract(harbour.world, harbour.view(), seen);
        mesh::Targets reference;
        reference.resize(static_cast<int>(width), static_cast<int>(height));
        mesh::Pass on_cpu;
        on_cpu.draw(harbour.world, seen, harbour.view(), {harbour.meshes, harbour.materials, harbour.trees}, reference, night);
        mesh::Glow glow_on_cpu;
        static_cast<void>(glow_on_cpu.show(reference.light, static_cast<int>(width), static_cast<int>(height), bloom, reference.colour));

        auto pass = mesh::gpu::Pass::make(*device, false, true);
        expect(fatal(pass.has_value())) << (pass ? "" : pass.error().message);
        expect(fatal(pass->load(harbour.meshes).has_value()));
        auto glow = mesh::gpu::Glow::make(*device);
        expect(fatal(glow.has_value())) << (glow ? "" : glow.error().message);
        mesh::gpu::Shadows shadows{.size = 2048, .distance = 40, .lamp_size = 512, .lamps = 4};
        const auto target = [&](rhi::Format format, std::uint32_t across, std::uint32_t down, std::string_view label) {
            const auto made = device->create(rhi::TextureDesc{format, across, down, format == rhi::Format::d32_float ? rhi::TextureUsage::depth_target : rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, label});
            expect(fatal(made.has_value())) << (made ? "" : made.error().message);
            return *made;
        };
        const rhi::Texture colour = target(rhi::Format::rgba8_unorm, width, height, "mesh.colour"), depth = target(rhi::Format::d32_float, width, height, "mesh.depth");
        const rhi::Texture light = target(rhi::Format::rgba16_float, width, height, "mesh.light"), one = target(rhi::Format::rgba16_float, width, height, "mesh.glow.one"),
                           other = target(rhi::Format::rgba16_float, width, height, "mesh.glow.other");
        shadows.map = target(rhi::Format::r32_float, shadows.size, shadows.size, "mesh.shadow"), shadows.depth = target(rhi::Format::d32_float, shadows.size, shadows.size, "mesh.shadow.depth");
        shadows.lamps_map = target(rhi::Format::r32_float, shadows.lamp_size * 6, shadows.lamp_size * shadows.lamps, "mesh.shadow.lamps");
        shadows.lamps_depth = target(rhi::Format::d32_float, shadows.lamp_size * 6, shadows.lamp_size * shadows.lamps, "mesh.shadow.lamps.depth");
        rhi::CommandList list;
        pass->record(list, harbour.world, seen, harbour.view(), harbour.materials, light, depth, width, height, night, &shadows);
        glow->record(list, light, one, other, colour, width, height, bloom);
        const auto submitted = device->submit(list);
        expect(fatal(submitted.has_value())) << (submitted ? "" : submitted.error().message);
        expect(device->wait(*submitted).has_value());
        const auto pixels = device->read(colour, rhi::Region{0, 0, width, height});
        expect(fatal(pixels.has_value())) << (pixels ? "" : pixels.error().message);
        keep(*pixels, "harbour");
        const Apart all = apart(reference, *pixels, false, 6);
        const mesh::gpu::Stats& stats = pass->last();
        std::println("the harbour on {}: {} draws, {} lights, {} lamps and spots with shadows in {} views, {} casters in the moon's map; {} of {} pixels further than 6 levels from the reference ({:.2f}%), mean {:.3f}, worst {}",
                     device->caps().device, stats.draws, stats.lamps, stats.lamps_shadowed, stats.lamp_views, stats.casters, all.off, all.compared, 100.0 * static_cast<double>(all.off) / static_cast<double>(all.compared), all.mean, all.worst);
        expect(eq(stats.lamps, 5u) && eq(stats.lamps_shadowed, 4u) && eq(stats.missing, 0u));
        expect(lt(static_cast<double>(all.off), 0.004 * static_cast<double>(all.compared)));
        static_cast<void>(device->wait_idle());
        for (const rhi::Texture made : {colour, depth, light, one, other, shadows.map, shadows.depth, shadows.lamps_map, shadows.lamps_depth}) device->destroy(made);
    };
}};
