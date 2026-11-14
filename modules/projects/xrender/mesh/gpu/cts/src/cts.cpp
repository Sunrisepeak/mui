module xrender.mesh.gpu.cts;

import std;
import mlibs.rhi;
import mlibs.cts;
import mlibs.math;
import mlibs.geom;
import mlibs.image;
import xrender.mesh.gpu;
import xrender.graph;

namespace xrender::mesh::gpu::cts {

namespace mesh = xrender::mesh;
namespace scene = xrender::scene;
namespace math = mlibs::math;
namespace geom = mlibs::geom;
namespace rhi = mlibs::rhi;

namespace {

// The case that is running, and where its pictures are kept.
thread_local mlibs::cts::Check*            current = nullptr;
thread_local const std::filesystem::path* scratch = nullptr;

// A check and what is said of it: `expect(found) << "why"`. Reported when the statement ends; as a condition it
// is whether the check held (`if (!(expect(made) << why)) return;`).
class Said {
public:
    Said(bool held, std::source_location at) : held_(held), at_(at) {}
    Said(const Said&) = delete;
    Said& operator=(const Said&) = delete;
    ~Said() {
        if (current) current->that(held_, why_.empty() ? std::string_view("the check did not hold") : std::string_view(why_), at_);
    }
    template <class Value>
    Said& operator<<(const Value& value) {
        if (!held_) why_ += std::format("{}", value);
        return *this;
    }
    explicit operator bool() const noexcept { return held_; }

private:
    bool                 held_;
    std::source_location at_;
    std::string          why_;
};
Said expect(bool held, std::source_location at = std::source_location::current()) { return {held, at}; }
template <class One, class Other>
bool eq(const One& one, const Other& other) { return one == other; }
template <class One, class Other>
bool lt(const One& one, const Other& other) { return one < other; }
template <class One, class Other>
bool gt(const One& one, const Other& other) { return one > other; }
template <class One, class Other>
bool ge(const One& one, const Other& other) { return one >= other; }

constexpr std::uint32_t width = 480, height = 270;

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
            if (!(expect(world.add({.id = id, .mesh = which, .material = material, .world = where, .bounds = geom::bounds(made[which].mesh)}).has_value()))) return;
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
            if (!(expect(world.add({.id = id, .mesh = which, .material = material, .world = where, .bounds = geom::bounds(made[which].mesh), .casts = casts}).has_value()))) return;
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
// A picture kept where the runner said (nowhere: not kept), for a person to look at.
void keep(std::span<const std::byte> rgba, std::string_view name) {
    if (!scratch || scratch->empty()) return;
    std::error_code failed;
    std::filesystem::create_directories(*scratch, failed);
    const mlibs::image::View view{reinterpret_cast<const std::uint8_t*>(rgba.data()), static_cast<int>(width), static_cast<int>(height), static_cast<int>(width) * 4, mlibs::image::Alpha::straight};
    expect(mlibs::image::save_png(*scratch / std::format("{}.png", name), view).has_value());
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
    if (!(expect(pass.has_value()) << (pass ? "" : pass.error().message))) return std::nullopt;
    const auto loaded = pass->load(yard.meshes);
    if (!(expect(loaded.has_value()) << (loaded ? "" : loaded.error().message))) return std::nullopt;
    const auto colour = device.create(rhi::TextureDesc{rhi::Format::rgba8_unorm, width, height, rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, "mesh.colour"});
    const auto depth = device.create(rhi::TextureDesc{rhi::Format::d32_float, width, height, rhi::TextureUsage::depth_target, "mesh.depth"});
    if (!(expect(colour.has_value() && depth.has_value()) << (colour ? (depth ? "" : depth.error().message) : colour.error().message))) return std::nullopt;
    rhi::CommandList list;
    pass->record(list, yard.world, seen, yard.view(), yard.materials, *colour, *depth, width, height, options);
    out.stats = pass->last();
    const auto submitted = device.submit(list);
    if (!(expect(submitted.has_value()) << (submitted ? "" : submitted.error().message))) return std::nullopt;
    expect(device.wait(*submitted).has_value());
    auto pixels = device.read(*colour, rhi::Region{0, 0, width, height});
    if (!(expect(pixels.has_value()) << (pixels ? "" : pixels.error().message))) return std::nullopt;
    out.device = std::move(*pixels);
    static_cast<void>(device.wait_idle());
    device.destroy(*colour), device.destroy(*depth);
    return out;
}

// What a case is given.
struct Rig {
    rhi::Device& device;
};
struct Listed {
    std::string               id;
    std::function<void(Rig&)> body;
};
using Cases = std::vector<Listed>;
void add(Cases& suite, std::string id, std::function<void(Rig&)> body) { suite.push_back({std::move(id), std::move(body)}); }

Cases suite_of() {
    Cases suite;
    // the yard on the device is the reference's yard: every draw drawn, and the picture within a few levels at all but the pixels of an edge
    add(suite, "mesh.yard", [](Rig& rig) {
        rhi::Device* const device = &rig.device;
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
    });

    // and in fog: what is drawn is the reference's, surface by surface, and the sky behind it clears upward as the reference's does
    add(suite, "mesh.fog", [](Rig& rig) {
        rhi::Device* const device = &rig.device;
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
    });

    // who drew a pixel, asked of the device: its ids picture names the reference's draw at every pixel but an edge's; and a pixel of the ball is the ball
    add(suite, "mesh.ids", [](Rig& rig) {
        rhi::Device* const device = &rig.device;
        const Yard yard;
        scene::Extracted seen;
        scene::extract(yard.world, yard.view(), seen);
        mesh::Targets reference;
        reference.resize(static_cast<int>(width), static_cast<int>(height));
        mesh::Pass on_cpu;
        on_cpu.draw(yard.world, seen, yard.view(), {yard.meshes, yard.materials, {}}, reference, {.shadows = false});

        auto pass = mesh::gpu::Pass::make(*device);
        if (!(expect(pass.has_value()) << (pass ? "" : pass.error().message))) return;
        if (!(expect(pass->load(yard.meshes).has_value()))) return;
        const auto ids = device->create(rhi::TextureDesc{rhi::Format::r32_uint, width, height, rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, "mesh.ids"});
        const auto depth = device->create(rhi::TextureDesc{rhi::Format::d32_float, width, height, rhi::TextureUsage::depth_target, "mesh.ids.depth"});
        if (!(expect(ids.has_value() && depth.has_value()) << (ids ? (depth ? "" : depth.error().message) : ids.error().message))) return;
        rhi::CommandList list;
        pass->record_ids(list, yard.world, seen, yard.view(), *ids, *depth, width, height);
        const auto submitted = device->submit(list);
        if (!(expect(submitted.has_value()) << (submitted ? "" : submitted.error().message))) return;
        expect(device->wait(*submitted).has_value());
        const auto pixels = device->read(*ids, rhi::Region{0, 0, width, height});
        if (!(expect(pixels.has_value()) << (pixels ? "" : pixels.error().message))) return;
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
        if (!(expect(ball.has_value() && said != mesh::none && said < seen.draws.size()))) return;
        expect(eq(yard.world.instances()[seen.draws[said].instance].id, scene::Id{11}) && eq(ball->instance, scene::Id{11}));
        static_cast<void>(device->wait_idle());
        device->destroy(*ids), device->destroy(*depth);
    });

    // the sun's shadows by a map are the reference's traced shadows: the same pixels dark, but for a line of them along a shadow's edge
    add(suite, "mesh.shadows.sun", [](Rig& rig) {
        rhi::Device* const device = &rig.device;
        const Yard yard(true);
        scene::Extracted seen;
        scene::extract(yard.world, yard.view(), seen);
        mesh::Targets traced, lit;
        traced.resize(static_cast<int>(width), static_cast<int>(height)), lit.resize(static_cast<int>(width), static_cast<int>(height));
        mesh::Pass on_cpu;
        on_cpu.draw(yard.world, seen, yard.view(), {yard.meshes, yard.materials, yard.trees}, traced, {.shadows = true});
        on_cpu.draw(yard.world, seen, yard.view(), {yard.meshes, yard.materials, {}}, lit, {.shadows = false});

        auto pass = mesh::gpu::Pass::make(*device);
        if (!(expect(pass.has_value()) << (pass ? "" : pass.error().message))) return;
        if (!(expect(pass->load(yard.meshes).has_value()))) return;
        mesh::gpu::Shadows shadows{.size = 2048, .distance = 30};
        const auto colour = device->create(rhi::TextureDesc{rhi::Format::rgba8_unorm, width, height, rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, "mesh.colour"});
        const auto depth = device->create(rhi::TextureDesc{rhi::Format::d32_float, width, height, rhi::TextureUsage::depth_target, "mesh.depth"});
        const auto map = device->create(rhi::TextureDesc{rhi::Format::r32_float, shadows.size, shadows.size, rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, "mesh.shadow"});
        const auto map_depth = device->create(rhi::TextureDesc{rhi::Format::d32_float, shadows.size, shadows.size, rhi::TextureUsage::depth_target, "mesh.shadow.depth"});
        if (!(expect(colour.has_value() && depth.has_value() && map.has_value() && map_depth.has_value()) << (map ? "" : map.error().message))) return;
        shadows.map = *map, shadows.depth = *map_depth;
        rhi::CommandList list;
        pass->record(list, yard.world, seen, yard.view(), yard.materials, *colour, *depth, width, height, {.shadows = true}, &shadows);
        const auto submitted = device->submit(list);
        if (!(expect(submitted.has_value()) << (submitted ? "" : submitted.error().message))) return;
        expect(device->wait(*submitted).has_value());
        const auto pixels = device->read(*colour, rhi::Region{0, 0, width, height});
        if (!(expect(pixels.has_value()) << (pixels ? "" : pixels.error().message))) return;
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
    });

    // a polished ball and a gold post on the device are the reference's: the highlights where it has them, as bright
    add(suite, "mesh.polished", [](Rig& rig) {
        rhi::Device* const device = &rig.device;
        const Yard yard(false, true);
        const auto drawn = both(*device, yard, {.environment = {.on = true}});  // with a sky about it for the gold to mirror
        if (!drawn) return;
        keep(drawn->device, "yard-polished");
        const Apart all = apart(drawn->reference, drawn->device, false, 6);
        std::println("the yard with a polished ball and a gold post: {} of {} pixels further than 6 levels from the reference ({:.2f}%), mean {:.3f}, worst {}", all.off, all.compared,
                     100.0 * static_cast<double>(all.off) / static_cast<double>(all.compared), all.mean, all.worst);
        expect(lt(static_cast<double>(all.off), 0.001 * static_cast<double>(all.compared)));
        expect(lt(all.mean, 0.1));
    });
    // a lamp brighter than the display glows on the device as in the reference: the light kept before the curve, what is above it spread, then shown
    add(suite, "mesh.glow", [](Rig& rig) {
        rhi::Device* const device = &rig.device;
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
        if (!(expect(pass.has_value()) << (pass ? "" : pass.error().message))) return;
        if (!(expect(pass->load(yard.meshes).has_value()))) return;
        auto glow = mesh::gpu::Glow::make(*device);
        if (!(expect(glow.has_value()) << (glow ? "" : glow.error().message))) return;
        const auto of_light = [&](std::string_view label) {
            const auto made = device->create(rhi::TextureDesc{rhi::Format::rgba16_float, width, height, rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, label});
            if (!(expect(made.has_value()) << (made ? "" : made.error().message))) return rhi::Texture{};
            return *made;
        };
        const rhi::Texture light = of_light("mesh.light"), one = of_light("mesh.glow.one"), other = of_light("mesh.glow.other");
        const auto colour = device->create(rhi::TextureDesc{rhi::Format::rgba8_unorm, width, height, rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, "mesh.colour"});
        const auto depth = device->create(rhi::TextureDesc{rhi::Format::d32_float, width, height, rhi::TextureUsage::depth_target, "mesh.depth"});
        if (!(expect(colour.has_value() && depth.has_value()))) return;
        const auto shown_with = [&](const mesh::Bloom& how, const mesh::Tone& tone = {}) {
            rhi::CommandList list;
            pass->record(list, yard.world, seen, yard.view(), yard.materials, light, *depth, width, height, {.shadows = false});
            glow->record(list, light, one, other, *colour, width, height, how, tone);
            const auto submitted = device->submit(list);
            if (!(expect(submitted.has_value()) << (submitted ? "" : submitted.error().message))) return std::vector<std::byte>{};
            expect(device->wait(*submitted).has_value());
            auto pixels = device->read(*colour, rhi::Region{0, 0, width, height});
            if (!(expect(pixels.has_value()) << (pixels ? "" : pixels.error().message))) return std::vector<std::byte>{};
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
        // And through a tone: half the exposure, bent toward white — the reference's curve.
        const mesh::Tone tone{.exposure = 0.5f, .filmic = true};
        mesh::Targets toned = reference;
        static_cast<void>(glow_on_cpu.show(reference.light, static_cast<int>(width), static_cast<int>(height), bloom, toned.colour, tone));
        const std::vector<std::byte> toned_on_device = shown_with(bloom, tone);
        keep(toned_on_device, "yard-glow-toned");
        const Apart with_tone = apart(toned, toned_on_device, false, 6);
        std::println("    through a tone: {} of {} pixels further than 6 levels, mean {:.3f}", with_tone.off, with_tone.compared, with_tone.mean);
        expect(lt(static_cast<double>(with_tone.off), 0.001 * static_cast<double>(with_tone.compared)));
        expect(toned.colour != reference.colour);
        static_cast<void>(device->wait_idle());
        device->destroy(light), device->destroy(one), device->destroy(other), device->destroy(*colour), device->destroy(*depth);
    });
    // a lamp's and a spot's shadows by their maps are the reference's traced shadows: the same pixels dark, but for a line of them along a shadow's edge
    add(suite, "mesh.shadows.lamps", [](Rig& rig) {
        rhi::Device* const device = &rig.device;
        const Yard yard;
        scene::Extracted seen;
        scene::extract(yard.world, yard.view(), seen);
        mesh::Targets traced, lit;
        traced.resize(static_cast<int>(width), static_cast<int>(height)), lit.resize(static_cast<int>(width), static_cast<int>(height));
        mesh::Pass on_cpu;
        on_cpu.draw(yard.world, seen, yard.view(), {yard.meshes, yard.materials, yard.trees}, traced, {.shadows = true});
        on_cpu.draw(yard.world, seen, yard.view(), {yard.meshes, yard.materials, {}}, lit, {.shadows = false});

        auto pass = mesh::gpu::Pass::make(*device);
        if (!(expect(pass.has_value()) << (pass ? "" : pass.error().message))) return;
        if (!(expect(pass->load(yard.meshes).has_value()))) return;
        mesh::gpu::Shadows shadows{.size = 2048, .distance = 30, .lamp_size = 512, .lamps = 2};
        const auto target = [&](rhi::Format format, std::uint32_t across, std::uint32_t down, std::string_view label) {
            const auto made = device->create(rhi::TextureDesc{format, across, down, format == rhi::Format::d32_float ? rhi::TextureUsage::depth_target : rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, label});
            if (!(expect(made.has_value()) << (made ? "" : made.error().message))) return rhi::Texture{};
            return *made;
        };
        const rhi::Texture colour = target(rhi::Format::rgba8_unorm, width, height, "mesh.colour"), depth = target(rhi::Format::d32_float, width, height, "mesh.depth");
        shadows.map = target(rhi::Format::r32_float, shadows.size, shadows.size, "mesh.shadow"), shadows.depth = target(rhi::Format::d32_float, shadows.size, shadows.size, "mesh.shadow.depth");
        shadows.lamps_map = target(rhi::Format::r32_float, shadows.lamp_size * 6, shadows.lamp_size * shadows.lamps, "mesh.shadow.lamps");
        shadows.lamps_depth = target(rhi::Format::d32_float, shadows.lamp_size * 6, shadows.lamp_size * shadows.lamps, "mesh.shadow.lamps.depth");
        rhi::CommandList list;
        pass->record(list, yard.world, seen, yard.view(), yard.materials, colour, depth, width, height, {.shadows = true}, &shadows);
        const auto submitted = device->submit(list);
        if (!(expect(submitted.has_value()) << (submitted ? "" : submitted.error().message))) return;
        expect(device->wait(*submitted).has_value());
        const auto pixels = device->read(colour, rhi::Region{0, 0, width, height});
        if (!(expect(pixels.has_value()) << (pixels ? "" : pixels.error().message))) return;
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
    });
    // the harbour at night on the device is the reference's: lamps' and a spot's shadows by maps for its traced ones, the wet quay, fog, the lamps' glow
    add(suite, "mesh.harbour", [](Rig& rig) {
        rhi::Device* const device = &rig.device;
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
        if (!(expect(pass.has_value()) << (pass ? "" : pass.error().message))) return;
        if (!(expect(pass->load(harbour.meshes).has_value()))) return;
        auto glow = mesh::gpu::Glow::make(*device);
        if (!(expect(glow.has_value()) << (glow ? "" : glow.error().message))) return;
        mesh::gpu::Shadows shadows{.size = 2048, .distance = 40, .lamp_size = 512, .lamps = 4};
        const auto target = [&](rhi::Format format, std::uint32_t across, std::uint32_t down, std::string_view label) {
            const auto made = device->create(rhi::TextureDesc{format, across, down, format == rhi::Format::d32_float ? rhi::TextureUsage::depth_target : rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, label});
            if (!(expect(made.has_value()) << (made ? "" : made.error().message))) return rhi::Texture{};
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
        if (!(expect(submitted.has_value()) << (submitted ? "" : submitted.error().message))) return;
        expect(device->wait(*submitted).has_value());
        const auto pixels = device->read(colour, rhi::Region{0, 0, width, height});
        if (!(expect(pixels.has_value()) << (pixels ? "" : pixels.error().message))) return;
        keep(*pixels, "harbour");
        const Apart all = apart(reference, *pixels, false, 6);
        const mesh::gpu::Stats& stats = pass->last();
        std::println("the harbour on {}: {} draws, {} lights, {} lamps and spots with shadows in {} views, {} casters in the moon's map; {} of {} pixels further than 6 levels from the reference ({:.2f}%), mean {:.3f}, worst {}",
                     device->caps().device, stats.draws, stats.lamps, stats.lamps_shadowed, stats.lamp_views, stats.casters, all.off, all.compared, 100.0 * static_cast<double>(all.off) / static_cast<double>(all.compared), all.mean, all.worst);
        expect(eq(stats.lamps, 5u) && eq(stats.lamps_shadowed, 4u) && eq(stats.missing, 0u));
        expect(lt(static_cast<double>(all.off), 0.004 * static_cast<double>(all.compared)));
        static_cast<void>(device->wait_idle());
        for (const rhi::Texture made : {colour, depth, light, one, other, shadows.map, shadows.depth, shadows.lamps_map, shadows.lamps_depth}) device->destroy(made);
    });
    // frames gathered over time on the device are the reference's: sixteen frames each sampled a little aside, then the box moved a long way in one — the picture the reference's resolve gives, kept from frame to frame by the graph's histories
    add(suite, "mesh.temporal", [](Rig& rig) {
        rhi::Device* const device = &rig.device;
        Yard yard;
        mesh::Pass on_cpu;
        mesh::Temporal gathered_on_cpu;
        mesh::Targets reference;
        reference.resize(static_cast<int>(width), static_cast<int>(height));
        std::vector<std::uint8_t> resolved;

        auto pass = mesh::gpu::Pass::make(*device, false, true);
        if (!(expect(pass.has_value()) << (pass ? "" : pass.error().message))) return;
        if (!(expect(pass->load(yard.meshes).has_value()))) return;
        auto temporal = mesh::gpu::Temporal::make(*device);
        auto glow = mesh::gpu::Glow::make(*device);
        if (!(expect(temporal.has_value() && glow.has_value()) << (temporal ? "" : temporal.error().message))) return;
        const auto colour = device->create(rhi::TextureDesc{rhi::Format::rgba8_unorm, width, height, rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, "mesh.colour"});
        if (!(expect(colour.has_value()))) return;
        {
            xrender::TexturePool pool(*device);
            const auto sampled_target = rhi::TextureUsage::color_target | rhi::TextureUsage::sampled;
            const xrender::TextureNeed of_light{width, height, rhi::Format::rgba16_float, sampled_target}, of_who{width, height, rhi::Format::r32_float, sampled_target},
                of_depth{width, height, rhi::Format::d32_float, rhi::TextureUsage::depth_target};
            std::uint32_t last_past = 0;
            // One frame on both: the reference's draw and resolve; the device's four passes in a graph.
            const auto frame = [&](std::uint32_t number) {
                const mesh::Options options{.shadows = false, .jitter = mesh::jitter_of(number)};
                scene::Extracted seen;
                scene::extract(yard.world, yard.view(), seen);
                on_cpu.draw(yard.world, seen, yard.view(), {yard.meshes, yard.materials, {}}, reference, options);
                const mesh::Temporal::Told told = gathered_on_cpu.resolve(reference, resolved);

                pool.begin_frame();
                xrender::Graph graph(pool);
                const auto light = graph.transient(of_light, "light"), velocity = graph.transient(of_light, "velocity"), depth = graph.transient(of_depth, "depth");
                const auto who = graph.history("mesh.who", of_who, "who"), gathered = graph.history("mesh.gathered", of_light, "gathered");
                if (!(expect(light.has_value() && velocity.has_value() && depth.has_value() && who.has_value() && gathered.has_value()))) return mesh::Temporal::Told{};
                const bool begun = who->past == 1 && gathered->past == 1;
                last_past = gathered->past;
                graph.pass({.name = "mesh.light", .writes = {*light, *depth}},
                           [&](rhi::CommandList& list) { pass->record(list, yard.world, seen, yard.view(), yard.materials, *light, *depth, width, height, options); });
                // Who and how far, of one drawing of the scene where the device has two colour targets a pass.
                graph.pass({.name = "mesh.velocity-who", .writes = {*velocity, who->now, *depth}}, [&](rhi::CommandList& list) {
                    pass->record_velocity_and_who(list, yard.world, seen, yard.view(), *velocity, who->now, *depth, width, height, options);
                });
                std::vector<rhi::Texture> reads{*light, *velocity, who->now};
                if (begun) reads.push_back(who->before), reads.push_back(gathered->before);
                graph.pass({.name = "mesh.resolve", .reads = reads, .writes = {gathered->now}},
                           [&](rhi::CommandList& list) { temporal->record(list, *light, *velocity, who->now, who->before, gathered->before, gathered->now, width, height, begun); });
                graph.pass({.name = "mesh.show", .reads = {gathered->now}, .writes = {*colour}},
                           [&](rhi::CommandList& list) { glow->record(list, gathered->now, *light, *velocity, *colour, width, height, {}); });
                rhi::CommandList list;
                const auto report = graph.record(list, {&*colour, 1});
                if (!(expect(report.has_value()) << (report ? "" : report.error().front().text))) return mesh::Temporal::Told{};
                expect(eq(report->recorded, std::size_t{4}) && eq(report->histories, std::size_t{2}));
                const auto submitted = device->submit(list);
                if (!(expect(submitted.has_value()) << (submitted ? "" : submitted.error().message))) return mesh::Temporal::Told{};
                expect(device->wait(*submitted).has_value());
                return told;
            };
            const auto compared = [&](std::string_view name) {
                auto pixels = device->read(*colour, rhi::Region{0, 0, width, height});
                if (!(expect(pixels.has_value()) << (pixels ? "" : pixels.error().message))) return Apart{};
                keep(*pixels, name);
                mesh::Targets shown = reference;
                shown.colour = resolved;
                return apart(shown, *pixels, false, 6);
            };
            mesh::Temporal::Told told;
            for (std::uint32_t number = 0; number < 16; ++number) told = frame(number);
            expect(eq(last_past, 1u)) << "each frame reads the last frame's";
            const Apart still = compared("yard-gathered");
            std::println("16 frames gathered ({} pixels carried in the last): {} of {} pixels further than 6 levels from the reference's resolve ({:.2f}%), mean {:.3f}, worst {}", told.carried, still.off,
                         still.compared, 100.0 * static_cast<double>(still.off) / static_cast<double>(still.compared), still.mean, still.worst);
            expect(lt(static_cast<double>(still.off), 0.001 * static_cast<double>(still.compared)));
            // The box goes a long way to the left in one frame: where it was is the floor again at once.
            const math::Mat4 was = yard.world.find(10)->world;
            expect(yard.world.move(10, math::translation({-1.5f, 0, 0}) * was).has_value());
            told = frame(16);
            const Apart moved = compared("yard-gathered-moved");
            std::println("    the box moved: the reference carried {} pixels, {} were another thing's; {} of {} pixels further than 6 levels ({:.2f}%), mean {:.3f}", told.carried, told.another, moved.off,
                         moved.compared, 100.0 * static_cast<double>(moved.off) / static_cast<double>(moved.compared), moved.mean);
            expect(gt(told.another, 500u));
            expect(lt(static_cast<double>(moved.off), 0.001 * static_cast<double>(moved.compared)));
            std::println("    the pool: {} textures made in 17 frames, {} histories", pool.made(), pool.histories());
            expect(eq(pool.made(), std::size_t{7})) << "three of a frame and two histories of two: made once";
            static_cast<void>(device->wait_idle());
        }
        device->destroy(*colour);
    });
    // who drew a pixel and how far it moved, of one drawing of the scene: the same two pictures, byte for byte, as a pass each gives — after a still frame and after the box has moved
    add(suite, "mesh.velocity-who", [](Rig& rig) {
        rhi::Device* const device = &rig.device;
        Yard yard(true);
        auto together = mesh::gpu::Pass::make(*device, false, true), apart = mesh::gpu::Pass::make(*device, false, true);
        if (!(expect(together.has_value() && apart.has_value()) << (together ? "" : together.error().message))) return;
        if (!(expect(together->load(yard.meshes).has_value() && apart->load(yard.meshes).has_value()))) return;
        const auto sampled_target = rhi::TextureUsage::color_target | rhi::TextureUsage::sampled;
        std::array<rhi::Texture, 5> made{};
        const std::array<rhi::TextureDesc, 5> wanted{rhi::TextureDesc{rhi::Format::rgba16_float, width, height, sampled_target, "velocity.together"},
                                                     rhi::TextureDesc{rhi::Format::r32_float, width, height, sampled_target, "who.together"},
                                                     rhi::TextureDesc{rhi::Format::rgba16_float, width, height, sampled_target, "velocity.apart"},
                                                     rhi::TextureDesc{rhi::Format::r32_float, width, height, sampled_target, "who.apart"},
                                                     rhi::TextureDesc{rhi::Format::d32_float, width, height, rhi::TextureUsage::depth_target, "depth"}};
        bool all = true;
        for (std::size_t index = 0; index < made.size(); ++index) {
            const auto one = device->create(wanted[index]);
            all = all && one.has_value();
            if (one) made[index] = *one;
        }
        if (expect(all)) {
            std::size_t moving = 0, named = 0;
            for (std::uint32_t number = 0; number < 3; ++number) {
                // The third frame: the box has gone to the left, and the view is aside by its jitter.
                if (number == 2) {
                    const math::Mat4 was = yard.world.find(10)->world;
                    expect(yard.world.move(10, math::translation({-0.4f, 0, 0}) * was).has_value());
                }
                const mesh::Options options{.shadows = false, .jitter = mesh::jitter_of(number)};
                scene::Extracted seen;
                scene::extract(yard.world, yard.view(), seen);
                rhi::CommandList list;
                together->record_velocity_and_who(list, yard.world, seen, yard.view(), made[0], made[1], made[4], width, height, options);
                apart->record_who(list, yard.world, seen, yard.view(), made[3], made[4], width, height, options);
                apart->record_velocity(list, yard.world, seen, yard.view(), made[2], made[4], width, height, options);
                const auto submitted = device->submit(list);
                if (!(expect(submitted.has_value()) << (submitted ? "" : submitted.error().message))) break;
                expect(device->wait(*submitted).has_value());
                const auto velocity_one = device->read(made[0], rhi::Region{0, 0, width, height}), velocity_two = device->read(made[2], rhi::Region{0, 0, width, height});
                const auto who_one = device->read(made[1], rhi::Region{0, 0, width, height}), who_two = device->read(made[3], rhi::Region{0, 0, width, height});
                if (!(expect(velocity_one.has_value() && velocity_two.has_value() && who_one.has_value() && who_two.has_value()))) break;
                std::size_t velocity_differs = 0, who_differs = 0;
                for (std::size_t at = 0; at < velocity_one->size(); ++at) velocity_differs += (*velocity_one)[at] != (*velocity_two)[at];
                for (std::size_t at = 0; at < who_one->size(); ++at) who_differs += (*who_one)[at] != (*who_two)[at];
                expect(eq(velocity_differs, std::size_t{0}) && eq(who_differs, std::size_t{0})) << std::format("frame {}: {} bytes of the velocity and {} of who differ", number, velocity_differs, who_differs);
                if (number == 2) {
                    // The pictures say something: pixels that moved (a half float's x that is not 0), pixels someone drew.
                    for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(width) * height; ++pixel) {
                        std::uint16_t across = 0;
                        float         who = 0;
                        std::memcpy(&across, velocity_one->data() + pixel * 8, 2);
                        std::memcpy(&who, who_one->data() + pixel * 4, 4);
                        moving += (across & 0x7fffu) != 0;
                        named += who > 0;
                    }
                }
            }
            std::println("    who and how far of one drawing ({} colour targets a pass): the same bytes as a pass each over three frames; {} pixels moved in the third, {} drawn by someone",
                         device->caps().max_color_targets, moving, named);
            expect(gt(moving, std::size_t{100}) && gt(named, std::size_t{1000}));
        }
        static_cast<void>(device->wait_idle());
        for (const rhi::Texture texture : made)
            if (texture) device->destroy(texture);
    });
    // how open a place is, on the device: rays stepped through what the eye sees darken the yard where the reference's rays through the scene do — under the ball, by the box and the post
    add(suite, "mesh.open", [](Rig& rig) {
        rhi::Device* const device = &rig.device;
        // No lights: the light from everywhere alone, so that what is compared is how much of it arrives.
        Yard yard(true);
        expect(yard.world.remove_light(1).has_value());
        scene::Extracted seen;
        scene::extract(yard.world, yard.view(), seen);
        const mesh::Options plainly{.background = {0.5f, 0.6f, 0.7f}, .ambient = {0.5f, 0.6f, 0.7f}, .shadows = false};
        mesh::Options by_rays = plainly;
        by_rays.shadows = true, by_rays.occlusion_rays = 32, by_rays.occlusion_reach = 1.5f;
        mesh::Targets plain, traced;
        plain.resize(static_cast<int>(width), static_cast<int>(height)), traced.resize(static_cast<int>(width), static_cast<int>(height));
        mesh::Pass on_cpu;
        on_cpu.draw(yard.world, seen, yard.view(), {yard.meshes, yard.materials, {}}, plain, plainly);
        on_cpu.draw(yard.world, seen, yard.view(), {yard.meshes, yard.materials, yard.trees}, traced, by_rays);
        expect(gt(on_cpu.last().occluded, std::uint64_t{10000}));

        auto pass = mesh::gpu::Pass::make(*device);
        auto open = mesh::gpu::Open::make(*device);
        if (!(expect(pass.has_value() && open.has_value()) << (pass ? (open ? "" : open.error().message) : pass.error().message))) return;
        if (!(expect(pass->load(yard.meshes).has_value()))) return;
        const auto target = [&](rhi::Format format, std::string_view label) {
            const auto made = device->create(rhi::TextureDesc{format, width, height, rhi::is_depth(format) ? rhi::TextureUsage::depth_target : rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, label});
            if (!(expect(made.has_value()) << (made ? "" : made.error().message))) return rhi::Texture{};
            return *made;
        };
        const rhi::Texture colour = target(rhi::Format::rgba8_unorm, "mesh.colour"), depth = target(rhi::Format::d32_float, "mesh.depth"), surface = target(rhi::Format::rgba16_float, "mesh.surface"),
                           opened = target(rhi::Format::r32_float, "mesh.open");
        const auto drawn = [&](bool with_open) {
            rhi::CommandList list;
            mesh::gpu::Shadows about{};
            if (with_open) {
                pass->record_surface(list, yard.world, seen, yard.view(), surface, depth, width, height, by_rays);
                open->record(list, yard.view(), surface, opened, width, height, by_rays);
                about.open = opened;
            }
            pass->record(list, yard.world, seen, yard.view(), yard.materials, colour, depth, width, height, plainly, &about);
            const auto submitted = device->submit(list);
            if (!(expect(submitted.has_value()) << (submitted ? "" : submitted.error().message))) return std::vector<std::byte>{};
            expect(device->wait(*submitted).has_value());
            auto pixels = device->read(colour, rhi::Region{0, 0, width, height});
            if (!(expect(pixels.has_value()) << (pixels ? "" : pixels.error().message))) return std::vector<std::byte>{};
            return std::move(*pixels);
        };
        const std::vector<std::byte> without = drawn(false), with = drawn(true), again = drawn(true);
        keep(with, "yard-open");
        expect(with == again) << "the same picture each time";
        expect(lt(static_cast<double>(apart(plain, without, false, 6).off), 0.002 * static_cast<double>(plain.ids.size()))) << "with no picture of it every place is open: the pass as before";

        // A pixel's rays are its own on each side, so pixels are compared in blocks of 15 × 15: each block's mean
        // green, the reference's with rays against the device's, and against the device's with none.
        constexpr int block = 15;
        double off_with = 0, off_without = 0, darkened_reference = 0, darkened_device = 0, worst = 0;
        int blocks = 0, dark_blocks = 0, dark_both = 0;
        for (int top = 0; top + block <= static_cast<int>(height); top += block)
            for (int left = 0; left + block <= static_cast<int>(width); left += block) {
                double reference = 0, lit = 0, device_with = 0, device_without = 0;
                for (int y = top; y < top + block; ++y)
                    for (int x = left; x < left + block; ++x) {
                        const std::size_t at = (static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)) * 4 + 1;
                        reference += traced.colour[at], lit += plain.colour[at], device_with += std::to_integer<int>(with[at]), device_without += std::to_integer<int>(without[at]);
                    }
                const double count = block * block;
                reference /= count, lit /= count, device_with /= count, device_without /= count;
                off_with += std::abs(device_with - reference), off_without += std::abs(device_without - reference), worst = std::max(worst, std::abs(device_with - reference));
                darkened_reference += lit - reference, darkened_device += device_without - device_with;
                const bool dark = lit - reference > 4, dark_on_device = device_without - device_with > 2;
                dark_blocks += dark, dark_both += dark && dark_on_device, ++blocks;
            }
        off_with /= blocks, off_without /= blocks, darkened_reference /= blocks, darkened_device /= blocks;
        std::println("how open a place is, 32 rays to 1.5: the reference darkens a block by {:.2f} levels on the whole, the device by {:.2f}; a block is {:.2f} levels from the reference with it, {:.2f} without "
                     "(worst {:.1f}); of {} blocks the reference darkens by over 4 levels the device darkens {}",
                     darkened_reference, darkened_device, off_with, off_without, worst, dark_blocks, dark_both);
        // Measured on lavapipe: the reference darkens a block by 1.69 levels on the whole and the device by 1.45 (it
        // knows what the eye sees: the far side of the ball shuts nothing); a block is 0.27 levels from the
        // reference with it and 1.69 without, the worst 5.3; all 65 blocks the reference darkens, the device darkens.
        expect(gt(dark_blocks, 40)) << "under the ball, by the box, by the post";
        expect(ge(dark_both * 100, dark_blocks * 95)) << "where the reference's rays darken, the device's do";
        expect(lt(off_with, 0.3 * off_without) && lt(off_with, 0.5)) << "and by about as much: " << off_with << " against " << off_without;
        expect(lt(darkened_device, darkened_reference * 1.05) && gt(darkened_device, darkened_reference * 0.7)) << "a little less, never more: what the eye does not see shuts nothing";
        expect(lt(worst, 9.0));
        static_cast<void>(device->wait_idle());
        device->destroy(colour), device->destroy(depth), device->destroy(surface), device->destroy(opened);
    });
    // a floor of tiles on the device is the reference's: sharp near, the mean of its tiles far away, each size of the picture read as the reference reads it
    add(suite, "mesh.textures", [](Rig& rig) {
        rhi::Device* const device = &rig.device;
        Yard yard(true);
        // The floor's texture coordinates repeat every four units; its material is the tiles' own colours.
        for (math::Vec2& uv : yard.made[1].mesh.uvs) uv = {uv.x * 50, uv.y * 50};
        std::vector<std::uint8_t> bytes(64 * 64 * 4, 255);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                const std::uint8_t level = ((x / 8) + (y / 8)) % 2 ? 235 : 70;
                bytes[static_cast<std::size_t>(y * 64 + x) * 4] = level, bytes[static_cast<std::size_t>(y * 64 + x) * 4 + 1] = level, bytes[static_cast<std::size_t>(y * 64 + x) * 4 + 2] = level;
            }
        const mesh::Texture tiles = *mesh::Texture::made(bytes, 64, 64);
        const std::array<const mesh::Texture*, 1> textures{&tiles};
        yard.materials.push_back({{1, 1, 1}, {}, 1, 0, 0});
        scene::Instance floor = *yard.world.find(1);
        floor.material = static_cast<std::uint32_t>(yard.materials.size() - 1);
        expect(yard.world.set(floor).has_value());
        scene::Extracted seen;
        scene::extract(yard.world, yard.view(), seen);
        mesh::Targets reference;
        reference.resize(static_cast<int>(width), static_cast<int>(height));
        mesh::Pass on_cpu;
        on_cpu.draw(yard.world, seen, yard.view(), {yard.meshes, yard.materials, {}, textures}, reference, {.shadows = false});

        auto pass = mesh::gpu::Pass::make(*device);
        if (!(expect(pass.has_value()) << (pass ? "" : pass.error().message))) return;
        if (!(expect(pass->load(yard.meshes).has_value()))) return;
        const auto loaded = pass->load_textures(textures);
        if (!(expect(loaded.has_value()) << (loaded ? "" : loaded.error().message))) return;
        const auto colour = device->create(rhi::TextureDesc{rhi::Format::rgba8_unorm, width, height, rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, "mesh.colour"});
        const auto depth = device->create(rhi::TextureDesc{rhi::Format::d32_float, width, height, rhi::TextureUsage::depth_target, "mesh.depth"});
        if (!(expect(colour.has_value() && depth.has_value()))) return;
        rhi::CommandList list;
        pass->record(list, yard.world, seen, yard.view(), yard.materials, *colour, *depth, width, height, {.shadows = false});
        const auto submitted = device->submit(list);
        if (!(expect(submitted.has_value()) << (submitted ? "" : submitted.error().message))) return;
        expect(device->wait(*submitted).has_value());
        const auto pixels = device->read(*colour, rhi::Region{0, 0, width, height});
        if (!(expect(pixels.has_value()) << (pixels ? "" : pixels.error().message))) return;
        keep(*pixels, "yard-tiles");
        const Apart all = apart(reference, *pixels, false, 6), loosely = apart(reference, *pixels, false, 16);
        std::println("a floor of tiles: {} draw textured; {} of {} pixels further than 6 levels from the reference ({:.2f}%), {} further than 16; mean {:.3f}, worst {}", pass->last().textured, all.off,
                     all.compared, 100.0 * static_cast<double>(all.off) / static_cast<double>(all.compared), loosely.off, all.mean, all.worst);
        expect(eq(pass->last().textured, 1u));
        // How many places are taken along a pixel's strip turns on the screen's differences of the texture coordinate,
        // which a device takes over two pixels by two and the reference from each pixel to the next: where the
        // count steps, a few pixels step a pixel apart, a few levels off.
        expect(lt(static_cast<double>(all.off), 0.01 * static_cast<double>(all.compared)));
        expect(lt(static_cast<double>(loosely.off), 0.0005 * static_cast<double>(loosely.compared)));
        static_cast<void>(device->wait_idle());
        device->destroy(*colour), device->destroy(*depth);
    });
    return suite;
}

}  // namespace

mlibs::rhi::ShaderFormats shader_formats() { return Pass::shader_formats(); }

std::vector<std::string> cases() {
    std::vector<std::string> out;
    for (const Listed& one : suite_of()) out.push_back(one.id);
    return out;
}

void declare(rhi::Device& device, mlibs::cts::Against who, Options options, std::source_location at) {
    if (who.subject.empty()) who.subject = device.caps().device;
    Rig rig{device};
    Cases all = suite_of();
    mlibs::cts::against(
        std::move(who),
        [&rig, &all, &options, at] {
            for (Listed& one : all)
                mlibs::cts::declare({.id = std::move(one.id), .contract = "xrender.mesh.gpu@1", .level = mlibs::cts::Level::tolerance, .needs = {},
                                     .body = [&rig, &options, body = std::move(one.body)](mlibs::cts::Check& check) {
                                         current = &check, scratch = &options.scratch;
                                         body(rig);
                                         current = nullptr, scratch = nullptr;
                                     }},
                                    at);
        },
        at);
}

}  // namespace xrender::mesh::gpu::cts
