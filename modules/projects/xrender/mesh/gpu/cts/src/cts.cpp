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
std::optional<Drawn> both(rhi::Device& device, const Yard& yard, mesh::Options options, const mesh::Surroundings* about = nullptr) {
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
    if (about) {
        const auto wrapped = pass->load_surroundings(about);
        if (!(expect(wrapped.has_value()) << (wrapped ? "" : wrapped.error().message))) return std::nullopt;
    }
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
        // Clusters outside the view or wholly facing away are left out, as the reference leaves them out — and no
        // pixel changes: the same bytes as every cluster drawn.
        const auto whole = both(*device, yard, {.cull_clusters = false});
        if (!whole) return;
        std::size_t differing = 0;
        for (std::size_t at = 0; at < drawn->device.size() && at < whole->device.size(); ++at) differing += drawn->device[at] != whole->device[at];
        const auto& kept = drawn->stats;
        std::println("    clusters: {} looked at, {} outside the view, {} facing away; {} triangles in {} runs, where every cluster is {} triangles in {}; {} bytes differ", kept.clusters,
                     kept.clusters_outside, kept.clusters_away, kept.triangles, kept.runs, whole->stats.triangles, whole->stats.runs, differing);
        expect(eq(differing, std::size_t{0}) && eq(drawn->device.size(), whole->device.size()));
        expect(eq(kept.clusters, whole->stats.clusters) && eq(whole->stats.clusters_outside + whole->stats.clusters_away, 0u) && eq(whole->stats.runs, whole->stats.draws));
        expect(gt(kept.clusters_away, 0u) && lt(kept.triangles, whole->stats.triangles)) << "the far sides of the ball and the post face away (nothing of the yard is outside this view)";
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
        // Instances their places mirror (a negative scale): the box and the ball are their own mirror images, so
        // the frame is the one above — in the reference and on the device, neither thing inside out or left out.
        Yard mirrored(false, true);
        expect(mirrored.world.move(10, math::Transform{{-2.2f, 0, 0}, math::Quat::about({0, 1, 0}, 0.6f), {1, 1, -1}}.matrix()).has_value());
        expect(mirrored.world.move(11, math::Transform{{0.4f, 0, 0.8f}, {}, {-1, 1, 1}}.matrix()).has_value());
        const auto turned = both(*device, mirrored, {.environment = {.on = true}});
        if (!turned) return;
        const Apart turned_all = apart(turned->reference, turned->device, false, 6);
        std::size_t other = 0;
        for (std::size_t byte = 0; byte + 3 < turned->reference.colour.size(); byte += 4)
            for (std::size_t channel = 0; channel < 3; ++channel)
                if (std::abs(static_cast<int>(turned->reference.colour[byte + channel]) - static_cast<int>(drawn->reference.colour[byte + channel])) > 6) {
                    ++other;
                    break;
                }
        std::println("with the box and the ball mirrored by their places: {} of {} pixels further than 6 levels from the reference, mean {:.3f}; {} pixels of the reference other than unmirrored", turned_all.off,
                     turned_all.compared, turned_all.mean, other);
        expect(lt(static_cast<double>(turned_all.off), 0.001 * static_cast<double>(turned_all.compared)));
        expect(lt(turned_all.mean, 0.1));
        expect(lt(static_cast<double>(other), 0.001 * static_cast<double>(turned_all.compared)));
    });
    // a clear coat, a sheen and what a surface mirrors by its specular strength and colour, on the device: a coated
    // box, a polished ball mirroring a red and a post of cloth, the reference's
    add(suite, "mesh.coat", [](Rig& rig) {
        rhi::Device* const device = &rig.device;
        Yard yard(false, true);
        yard.materials[0].clearcoat = 1, yard.materials[0].clearcoat_roughness = 0.15f;
        yard.materials[2].specular = 0.8f, yard.materials[2].specular_colour = {1, 0.3f, 0.3f}, yard.materials[2].ior = 1.7f;
        yard.materials[3].sheen = {1, 0.5f, 0.7f}, yard.materials[3].sheen_roughness = 0.5f, yard.materials[3].metallic = 0;  // the post: cloth
        const auto drawn = both(*device, yard, {.environment = {.on = true}});
        if (!drawn) return;
        keep(drawn->device, "yard-coat");
        const Apart all = apart(drawn->reference, drawn->device, false, 6);
        std::println("a coated box, a ball mirroring red and a post with a sheen: {} of {} pixels further than 6 levels from the reference ({:.2f}%), mean {:.3f}, worst {}", all.off, all.compared,
                     100.0 * static_cast<double>(all.off) / static_cast<double>(all.compared), all.mean, all.worst);
        expect(lt(static_cast<double>(all.off), 0.001 * static_cast<double>(all.compared)));
        expect(lt(all.mean, 0.1));
    });
    // lit by a picture of the surroundings — a sky, a ground and a lamp — the yard's matt floor and box, its polished ball and its gold post are on the device what the reference draws: the same numbers, read the same way
    add(suite, "mesh.about", [](Rig& rig) {
        rhi::Device* const device = &rig.device;
        const Yard yard(true, true);
        // A sky brighter overhead, a warm ground, and a lamp eight degrees wide up to the right.
        const math::Vec3 lamp_way = math::normalized(math::Vec3{1, 1, 0.6f});
        std::vector<float> rgb(256 * 128 * 3);
        for (int row = 0; row < 128; ++row)
            for (int column = 0; column < 256; ++column) {
                const double down = (row + 0.5) / 128 * math::det::pi, turn = ((column + 0.5) / 256 - 0.5) * math::det::two_pi;
                const math::Vec3 way{static_cast<float>(math::det::sin(down) * math::det::cos(turn)), static_cast<float>(math::det::cos(down)), static_cast<float>(math::det::sin(down) * math::det::sin(turn))};
                math::Vec3 light = way.y > 0 ? math::Vec3{0.3f, 0.5f, 0.9f} * (0.5f + 0.9f * way.y) : math::Vec3{0.3f, 0.24f, 0.18f};
                light = light + math::Vec3{12, 10, 7} * static_cast<float>(math::det::exp(-(1.0 - static_cast<double>(math::dot(way, lamp_way))) / 0.01));
                const std::size_t at = (static_cast<std::size_t>(row) * 256 + static_cast<std::size_t>(column)) * 3;
                rgb[at] = light.x, rgb[at + 1] = light.y, rgb[at + 2] = light.z;
            }
        const auto surroundings = mesh::Surroundings::made(rgb, 256, 128);
        if (!(expect(surroundings.has_value()))) return;
        const mesh::Options lit_by{.environment = {.on = true, .picture = &*surroundings}};
        const auto drawn = both(*device, yard, lit_by, &*surroundings);
        if (!drawn) return;
        keep(drawn->device, "yard-about");
        const Apart all = apart(drawn->reference, drawn->device, false, 6);
        std::println("the yard lit by a picture of the surroundings: {} of {} pixels further than 6 levels from the reference ({:.2f}%), mean {:.3f}, worst {}", all.off, all.compared,
                     100.0 * static_cast<double>(all.off) / static_cast<double>(all.compared), all.mean, all.worst);
        expect(eq(drawn->stats.about, 1u));
        expect(lt(static_cast<double>(all.off), 0.001 * static_cast<double>(all.compared)));
        expect(lt(all.mean, 0.1));
        // With a picture named and none loaded the frame is drawn as with no picture, and says so.
        const auto without = both(*device, yard, lit_by);
        if (!without) return;
        expect(eq(without->stats.about, 0u));
        // With the picture behind everything as well (`Environment::behind`): where nothing is drawn, the picture
        // along the pixel's own way, on the device as in the reference.
        mesh::Options and_behind = lit_by;
        and_behind.environment.behind = true;
        const auto whole = both(*device, yard, and_behind, &*surroundings);
        if (!whole) return;
        keep(whole->device, "yard-about-behind");
        const Apart with_behind = apart(whole->reference, whole->device, false, 6);
        std::size_t other_than_bare = 0;
        for (std::size_t byte = 0; byte + 3 < whole->reference.colour.size(); byte += 4)
            other_than_bare += std::abs(static_cast<int>(whole->reference.colour[byte + 2]) - static_cast<int>(drawn->reference.colour[byte + 2])) > 6;
        std::println("with the picture behind everything: {} of {} pixels further than 6 levels from the reference ({:.2f}%), mean {:.3f}, worst {}; {} pixels of the reference are other than without it", with_behind.off,
                     with_behind.compared, 100.0 * static_cast<double>(with_behind.off) / static_cast<double>(with_behind.compared), with_behind.mean, with_behind.worst, other_than_bare);
        expect(lt(static_cast<double>(with_behind.off), 0.001 * static_cast<double>(with_behind.compared)));
        expect(lt(with_behind.mean, 0.1));
        expect(gt(other_than_bare, std::size_t{1000}));
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
        // And through the neutral curve, at the exposure the lamp was made for.
        const mesh::Tone neutral{.neutral = true};
        mesh::Targets plainly = reference;
        static_cast<void>(glow_on_cpu.show(reference.light, static_cast<int>(width), static_cast<int>(height), bloom, plainly.colour, neutral));
        const std::vector<std::byte> plainly_on_device = shown_with(bloom, neutral);
        keep(plainly_on_device, "yard-glow-neutral");
        const Apart with_neutral = apart(plainly, plainly_on_device, false, 6);
        std::println("    through the neutral curve: {} of {} pixels further than 6 levels, mean {:.3f}", with_neutral.off, with_neutral.compared, with_neutral.mean);
        expect(lt(static_cast<double>(with_neutral.off), 0.001 * static_cast<double>(with_neutral.compared)));
        expect(plainly.colour != reference.colour && plainly.colour != toned.colour);
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
        // A light's view is drawn only the clusters it can hold — and no texel of a map changes, nor a byte of
        // the picture: the same as every cluster drawn into every view.
        const mesh::gpu::Stats kept = pass->last();
        const auto sun_map = device->read(shadows.map, rhi::Region{0, 0, shadows.size, shadows.size});
        const auto lamps_map = device->read(shadows.lamps_map, rhi::Region{0, 0, shadows.lamp_size * 6, shadows.lamp_size * shadows.lamps});
        rhi::CommandList again;
        pass->record(again, yard.world, seen, yard.view(), yard.materials, colour, depth, width, height, {.cull_clusters = false, .shadows = true}, &shadows);
        const auto resubmitted = device->submit(again);
        if (!(expect(resubmitted.has_value()) << (resubmitted ? "" : resubmitted.error().message))) return;
        expect(device->wait(*resubmitted).has_value());
        const mesh::gpu::Stats whole = pass->last();
        const auto whole_pixels = device->read(colour, rhi::Region{0, 0, width, height});
        const auto whole_sun = device->read(shadows.map, rhi::Region{0, 0, shadows.size, shadows.size});
        const auto whole_lamps = device->read(shadows.lamps_map, rhi::Region{0, 0, shadows.lamp_size * 6, shadows.lamp_size * shadows.lamps});
        if (!(expect(sun_map.has_value() && lamps_map.has_value() && whole_pixels.has_value() && whole_sun.has_value() && whole_lamps.has_value()) << "the maps and the picture are read back")) return;
        std::println("    the lights' views: {} clusters looked at, {} outside their view; {} triangles in {} draw calls, where every cluster is {} in {}", kept.shadow_clusters,
                     kept.shadow_clusters_outside, kept.shadow_triangles, kept.shadow_runs, whole.shadow_triangles, whole.shadow_runs);
        expect(*sun_map == *whole_sun) << "the sun's map, texel for texel";
        expect(*lamps_map == *whole_lamps) << "the lamps' map, texel for texel";
        expect(*pixels == *whole_pixels) << "the picture, byte for byte";
        expect(eq(kept.shadow_clusters, whole.shadow_clusters) && eq(whole.shadow_clusters_outside, 0u));
        expect(gt(kept.shadow_clusters_outside, 0u) && lt(kept.shadow_triangles, whole.shadow_triangles)) << "a face of a lamp's six sees a sixth of what is around it";
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
        mesh::Texture tiles = *mesh::Texture::made(bytes, 64, 64);
        const std::array<const mesh::Texture*, 1> textures{&tiles};
        yard.materials.push_back({{1, 1, 1}, {}, 1, 0, 0});
        scene::Instance floor = *yard.world.find(1);
        floor.material = static_cast<std::uint32_t>(yard.materials.size() - 1);
        expect(yard.world.set(floor).has_value());
        // Drawn by both and held: as the picture repeats, and with it turned about past its edge one way and
        // stopped at its edge the other, the box a surface no light changes; then besides with a colour at each
        // corner and the picture laid by the floor's second set of coordinates.
        enum class Kind { repeated, edged, cornered };
        const auto held = [&](Kind kind) {
            const bool edged = kind != Kind::repeated;
            // … and the floor's material laying the picture its own way: coarser, turned and moved.
            if (kind == Kind::edged) tiles.across = mesh::Edge::mirrors, tiles.down = mesh::Edge::stops, yard.materials[0].unlit = true, yard.materials.back().placing = mesh::Placings::all(mesh::Placing::made({0.3f, 0.6f}, 0.5f, {0.3f, 0.2f}));
            if (kind == Kind::cornered) {
                // The floor's coordinates moved to its second set (its first all 0), the picture laid by that; its
                // corners red to blue across it. The box hides by its corners from none at its foot to all at its
                // top, and is cut where it hides less than half.
                geom::Mesh& ground = yard.made[1].mesh;
                ground.second_uvs = ground.uvs, ground.uvs.assign(ground.uvs.size(), {0, 0});
                const geom::Box3 extent = geom::bounds(ground);
                for (const math::Vec3& at : ground.positions) {
                    const float share = (at.x - extent.low.x) / (extent.high.x - extent.low.x);
                    ground.colours.push_back({1 - share * 0.8f, 0.6f, 0.2f + share * 0.8f, 1});
                }
                yard.materials.back().placing = mesh::Placings::all(mesh::Placing::made({0.3f, 0.6f}, 0.5f, {0.3f, 0.2f}, 1));
                geom::Mesh& box = yard.made[0].mesh;
                const geom::Box3 tall = geom::bounds(box);
                for (const math::Vec3& at : box.positions) box.colours.push_back({1, 1, 1, (at.y - tall.low.y) / (tall.high.y - tall.low.y)});
                yard.materials[0].cutout = 0.5f;
            }
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
            keep(*pixels, kind == Kind::cornered ? "yard-tiles-cornered" : edged ? "yard-tiles-edged" : "yard-tiles");
            const Apart all = apart(reference, *pixels, false, 6), loosely = apart(reference, *pixels, false, 16);
            std::println("a floor of tiles{}{}: {} draw textured; {} of {} pixels further than 6 levels from the reference ({:.2f}%), {} further than 16; mean {:.3f}, worst {}",
                         edged ? ", laid coarser, turned and moved by its material, turned about past its edge one way and stopped at it the other, the box unlit" : "",
                         kind == Kind::cornered ? "; laid by its second set of coordinates, coloured at its corners, the box cut out by its corners' own" : "", pass->last().textured, all.off, all.compared, 100.0 * static_cast<double>(all.off) / static_cast<double>(all.compared), loosely.off, all.mean, all.worst);
            expect(eq(pass->last().textured, 1u));
            // The reference takes the screen's differences of the texture coordinate as a device does (within a
            // pixel's own two by two), so both take the same places along a pixel's strip: what is left is the
            // few pixels at the edges of things.
            expect(lt(static_cast<double>(all.off), 0.002 * static_cast<double>(all.compared)));
            expect(lt(static_cast<double>(loosely.off), 0.0002 * static_cast<double>(loosely.compared)));
            static_cast<void>(device->wait_idle());
            device->destroy(*colour), device->destroy(*depth);
        };
        held(Kind::repeated);
        held(Kind::edged);
        held(Kind::cornered);
    });
    // the surface's other pictures on the device are the reference's: roughness and metal, the normal (by the mesh's tangents and, on a mesh with none, worked out at the pixel), emission and shade from pictures
    add(suite, "mesh.surface", [](Rig& rig) {
        rhi::Device* const device = &rig.device;
        Yard yard;
        for (math::Vec2& uv : yard.made[1].mesh.uvs) uv = {uv.x * 50, uv.y * 50};
        // The box's faces with no tangents: the picture's ways are worked out from where its corners are.
        yard.made[0].mesh.tangents.clear();
        const auto picture = [](mesh::Coding coding, auto&& texel) {
            std::vector<std::uint8_t> bytes(64 * 64 * 4, 255);
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x) {
                    const std::array<std::uint8_t, 3> said = texel(x, y);
                    for (std::size_t channel = 0; channel < 3; ++channel) bytes[static_cast<std::size_t>(y * 64 + x) * 4 + channel] = said[channel];
                }
            return *mesh::Texture::made(bytes, 64, 64, coding);
        };
        // Tiles; bumps (a dome a tile, as a normal); the surface's numbers — shade in red toward a tile's edge,
        // rough and smooth tiles in green, strips of metal in blue; and a glow of two colours.
        const mesh::Texture tiles = picture(mesh::Coding::colour, [](int x, int y) {
            const std::uint8_t level = ((x / 8) + (y / 8)) % 2 ? 235 : 110;
            return std::array<std::uint8_t, 3>{level, level, level};
        });
        const mesh::Texture bumps = picture(mesh::Coding::plain, [](int x, int y) {
            const float across = (static_cast<float>(x % 16) + 0.5f) / 8.0f - 1.0f, down = (static_cast<float>(y % 16) + 0.5f) / 8.0f - 1.0f;
            const math::Vec3 way = math::normalized(math::Vec3{across * 0.7f, -down * 0.7f, 1});
            const auto byte = [](float value) { return static_cast<std::uint8_t>(std::clamp((value * 0.5f + 0.5f) * 255.0f + 0.5f, 0.0f, 255.0f)); };
            return std::array<std::uint8_t, 3>{byte(way.x), byte(way.y), byte(way.z)};
        });
        const mesh::Texture numbers = picture(mesh::Coding::plain, [](int x, int y) {
            const int from_edge = std::min({x % 16, 15 - x % 16, y % 16, 15 - y % 16});
            return std::array<std::uint8_t, 3>{static_cast<std::uint8_t>(95 + from_edge * 20), static_cast<std::uint8_t>(((x / 16) + (y / 16)) % 2 ? 230 : 70), static_cast<std::uint8_t>((x / 8) % 2 ? 255 : 0)};
        });
        const mesh::Texture lit = picture(mesh::Coding::colour, [](int x, int y) {
            return (x / 8 + y / 4) % 2 ? std::array<std::uint8_t, 3>{255, 160, 40} : std::array<std::uint8_t, 3>{20, 40, 200};
        });
        const std::array<const mesh::Texture*, 4> textures{&tiles, &bumps, &numbers, &lit};
        // The floor: all of them but the glow. The box: bumps, half again as steep. The ball: its roughness and
        // metal from the picture, and what it gives off.
        yard.materials.push_back({.base = {1, 1, 1}, .roughness = 1, .metallic = 1, .texture = 0, .surface = 2, .normal = 1, .shade = 2, .shade_strength = 0.8f});
        yard.materials.push_back({.base = {0.8f, 0.2f, 0.15f}, .normal = 1, .normal_scale = 1.5f});
        yard.materials.push_back({.base = {0.15f, 0.4f, 0.8f}, .emissive = {0.5f, 0.5f, 0.5f}, .roughness = 0.9f, .metallic = 1, .surface = 2, .glow = 3});
        const std::array<std::pair<scene::Id, std::uint32_t>, 3> given{{{1, 5}, {10, 6}, {11, 7}}};
        for (const auto& [id, material] : given) {
            scene::Instance instance = *yard.world.find(id);
            instance.material = material;
            expect(yard.world.set(instance).has_value());
        }
        scene::Extracted seen;
        scene::extract(yard.world, yard.view(), seen);
        const mesh::Options options{.shadows = false, .environment = {.on = true}};
        mesh::Targets reference;
        reference.resize(static_cast<int>(width), static_cast<int>(height));
        mesh::Pass on_cpu;
        on_cpu.draw(yard.world, seen, yard.view(), {yard.meshes, yard.materials, {}, textures}, reference, options);

        auto pass = mesh::gpu::Pass::make(*device);
        if (!(expect(pass.has_value()) << (pass ? "" : pass.error().message))) return;
        if (!(expect(pass->load(yard.meshes).has_value()))) return;
        const auto loaded = pass->load_textures(textures);
        if (!(expect(loaded.has_value()) << (loaded ? "" : loaded.error().message))) return;
        const auto colour = device->create(rhi::TextureDesc{rhi::Format::rgba8_unorm, width, height, rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, "mesh.colour"});
        const auto depth = device->create(rhi::TextureDesc{rhi::Format::d32_float, width, height, rhi::TextureUsage::depth_target, "mesh.depth"});
        if (!(expect(colour.has_value() && depth.has_value()))) return;
        rhi::CommandList list;
        pass->record(list, yard.world, seen, yard.view(), yard.materials, *colour, *depth, width, height, options);
        const auto submitted = device->submit(list);
        if (!(expect(submitted.has_value()) << (submitted ? "" : submitted.error().message))) return;
        expect(device->wait(*submitted).has_value());
        const auto pixels = device->read(*colour, rhi::Region{0, 0, width, height});
        if (!(expect(pixels.has_value()) << (pixels ? "" : pixels.error().message))) return;
        keep(*pixels, "yard-surface");
        std::vector<std::byte> of_reference(reference.colour.size());
        std::memcpy(of_reference.data(), reference.colour.data(), reference.colour.size());
        keep(of_reference, "yard-surface-reference");
        // Each thing by itself: the pixels the reference says it drew.
        const auto apart_of = [&](scene::Id id, int near) {
            Apart one;
            double sum = 0;
            for (std::size_t pixel = 0; pixel < reference.ids.size(); ++pixel) {
                if (reference.ids[pixel].draw == mesh::none || reference.draws[reference.ids[pixel].draw].id != id) continue;
                ++one.compared;
                int most = 0;
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    const int difference = std::abs(static_cast<int>(reference.colour[pixel * 4 + channel]) - std::to_integer<int>((*pixels)[pixel * 4 + channel]));
                    most = std::max(most, difference), sum += difference;
                }
                one.off += most > near, one.worst = std::max(one.worst, most);
            }
            one.mean = one.compared ? sum / (static_cast<double>(one.compared) * 3) : 0;
            return one;
        };
        const Apart all = apart(reference, *pixels, false, 6), floor = apart_of(1, 6), box = apart_of(10, 6), ball = apart_of(11, 6);
        std::println("the surface's pictures: {} draws with pictures; {} of {} pixels further than 6 levels from the reference ({:.2f}%), mean {:.3f}, worst {}; the floor {} of {} (mean {:.3f}), the box {} of {} (mean {:.3f}), the ball {} of {} (mean {:.3f})",
                     pass->last().textured, all.off, all.compared, 100.0 * static_cast<double>(all.off) / static_cast<double>(all.compared), all.mean, all.worst, floor.off, floor.compared, floor.mean, box.off,
                     box.compared, box.mean, ball.off, ball.compared, ball.mean);
        expect(eq(pass->last().textured, 3u));
        expect(gt(floor.compared, 10000u) && gt(box.compared, 1500u) && gt(ball.compared, 1500u));
        expect(lt(static_cast<double>(all.off), 0.004 * static_cast<double>(all.compared)));
        expect(lt(floor.mean, 0.8) && lt(box.mean, 0.8) && lt(ball.mean, 0.8));
        static_cast<void>(device->wait_idle());
        device->destroy(*colour), device->destroy(*depth);
    });
    // surfaces seen from both sides and cut out by their picture, on the device: a fence of pickets seen from before and one seen from behind are the reference's — in the colour, in the shadows the pickets throw and the gaps do not, in who drew each pixel
    add(suite, "mesh.cut", [](Rig& rig) {
        rhi::Device* const device = &rig.device;
        Yard yard(true);
        // A sheet stood up, three long and one and a half high, its picture across it once.
        yard.made.push_back(*geom::clustered(geom::sheet(3, 1.5f, 6, 3)));
        yard.meshes.clear(), yard.grown.clear(), yard.trees.clear();
        for (const geom::ClusteredMesh& one : yard.made) yard.meshes.push_back(&one), yard.grown.push_back(geom::bvh_of(one.mesh));
        for (const geom::Bvh& tree : yard.grown) yard.trees.push_back(&tree);
        // Pickets: eight across, each pointed at its top, two rails behind them; between them nothing (clear, and
        // black as clear texels are kept).
        std::vector<std::uint8_t> bytes(128 * 64 * 4, 0);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 128; ++x) {
                const int across = x % 16, from_middle = std::abs(across * 2 - 15);
                const bool picket = across >= 3 && across <= 12 && y >= 6 + from_middle, rail = (y >= 20 && y < 25) || (y >= 48 && y < 53);
                if (!picket && !rail) continue;
                const std::size_t at = static_cast<std::size_t>(y * 128 + x) * 4;
                bytes[at] = picket ? 222 : 150, bytes[at + 1] = picket ? 184 : 110, bytes[at + 2] = picket ? 120 : 70, bytes[at + 3] = 255;
            }
        const mesh::Texture pickets = *mesh::Texture::made(bytes, 128, 64);
        const std::array<const mesh::Texture*, 1> textures{&pickets};
        yard.materials.push_back({.base = {1, 1, 1}, .texture = 0, .cutout = 0.5f, .two_sided = true});
        const std::uint32_t fence = static_cast<std::uint32_t>(yard.materials.size() - 1), sheet = static_cast<std::uint32_t>(yard.made.size() - 1);
        // One facing the eye, before the ball; one seen from behind, to the right, turned a little.
        const math::Quat upright = math::Quat::about({1, 0, 0}, 1.5707964f);
        expect(yard.world.add({.id = 20, .mesh = sheet, .material = fence, .world = math::Transform{{-0.9f, -0.25f, 2.6f}, upright, {1, 1, 1}}.matrix(), .bounds = geom::bounds(yard.made[sheet].mesh)}).has_value());
        expect(yard.world.add({.id = 21, .mesh = sheet, .material = fence, .world = math::Transform{{2.9f, -0.25f, 1.9f}, math::Quat::about({0, 1, 0}, 2.7f) * upright, {1, 1, 1}}.matrix(), .bounds = geom::bounds(yard.made[sheet].mesh)}).has_value());
        scene::Extracted seen;
        scene::extract(yard.world, yard.view(), seen);
        mesh::Targets reference;
        reference.resize(static_cast<int>(width), static_cast<int>(height));
        mesh::Pass on_cpu;
        on_cpu.draw(yard.world, seen, yard.view(), {yard.meshes, yard.materials, yard.trees, textures}, reference, {.shadows = true});
        const std::uint64_t cut_by_reference = on_cpu.last().cut;
        std::size_t front = 0, back = 0;
        for (const mesh::Pixel& pixel : reference.ids)
            if (pixel.draw != mesh::none) front += reference.draws[pixel.draw].id == 20, back += reference.draws[pixel.draw].id == 21;
        expect(gt(cut_by_reference, std::uint64_t{3000}) && gt(front, std::size_t{3000}) && gt(back, std::size_t{2000})) << cut_by_reference << ' ' << front << ' ' << back;

        auto pass = mesh::gpu::Pass::make(*device);
        if (!(expect(pass.has_value()) << (pass ? "" : pass.error().message))) return;
        if (!(expect(pass->load(yard.meshes).has_value()))) return;
        const auto loaded = pass->load_textures(textures);
        if (!(expect(loaded.has_value()) << (loaded ? "" : loaded.error().message))) return;
        pass->use(yard.materials);
        mesh::gpu::Shadows shadows{.size = 2048, .distance = 30};
        const auto target = [&](rhi::Format format, std::uint32_t wide, std::uint32_t high, std::string_view label) {
            const auto made = device->create(rhi::TextureDesc{format, wide, high, rhi::is_depth(format) ? rhi::TextureUsage::depth_target : rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, label});
            if (!(expect(made.has_value()) << (made ? "" : made.error().message))) return rhi::Texture{};
            return *made;
        };
        const rhi::Texture colour = target(rhi::Format::rgba8_unorm, width, height, "mesh.colour"), depth = target(rhi::Format::d32_float, width, height, "mesh.depth");
        const rhi::Texture ids = target(rhi::Format::r32_uint, width, height, "mesh.ids"), who = target(rhi::Format::r32_float, width, height, "mesh.who");
        shadows.map = target(rhi::Format::r32_float, shadows.size, shadows.size, "mesh.shadow"), shadows.depth = target(rhi::Format::d32_float, shadows.size, shadows.size, "mesh.shadow.depth");
        if (!colour || !depth || !ids || !who || !shadows.map || !shadows.depth) return;
        rhi::CommandList list;
        pass->record(list, yard.world, seen, yard.view(), yard.materials, colour, depth, width, height, {.shadows = true}, &shadows);
        pass->record_ids(list, yard.world, seen, yard.view(), ids, depth, width, height);
        pass->record_who(list, yard.world, seen, yard.view(), who, depth, width, height);
        const auto submitted = device->submit(list);
        if (!(expect(submitted.has_value()) << (submitted ? "" : submitted.error().message))) return;
        expect(device->wait(*submitted).has_value());
        const auto pixels = device->read(colour, rhi::Region{0, 0, width, height}), named = device->read(ids, rhi::Region{0, 0, width, height}), by_instance = device->read(who, rhi::Region{0, 0, width, height});
        if (!(expect(pixels.has_value() && named.has_value() && by_instance.has_value()) << (pixels ? "" : pixels.error().message))) return;
        keep(*pixels, "yard-fence");
        std::vector<std::byte> of_reference(reference.colour.size());
        std::memcpy(of_reference.data(), reference.colour.data(), reference.colour.size());
        keep(of_reference, "yard-fence-reference");

        // Who drew each pixel: the device's names against the reference's, and of the fences' own pixels how many
        // the two agree on (a picket's outline is the picture's, read by each as it reads it). The pass that names
        // instances leaves out the same pixels as the one that names draws.
        std::size_t other = 0, fence_either = 0, fence_both = 0, passes_differ = 0;
        for (std::uint32_t y = 0; y < height; ++y)
            for (std::uint32_t x = 0; x < width; ++x) {
                const std::size_t pixel = static_cast<std::size_t>(y) * width + x;
                const std::uint32_t said = mesh::gpu::who(*named, width, x, y), is = reference.ids[pixel].draw;
                other += said != is;
                const bool fence_said = said != mesh::none && said < seen.draws.size() && seen.draws[said].id >= 20, fence_is = is != mesh::none && reference.draws[is].id >= 20;
                fence_either += fence_said || fence_is, fence_both += fence_said && fence_is;
                float instance = 0;
                std::memcpy(&instance, by_instance->data() + pixel * 4, 4);
                passes_differ += (instance > 0) != (said != mesh::none) || (said != mesh::none && said < seen.draws.size() && instance != static_cast<float>((seen.draws[said].id & 0xffffffu) + 1));
            }
        const Apart all = apart(reference, *pixels, false, 6), loosely = apart(reference, *pixels, false, 24);
        std::println("fences cut out by their picture, one seen from behind: the reference leaves {} samples out; {} of {} pixels name another draw than the reference ({:.3f}%); of {} pixels either says are a "
                     "fence's, both say {} ({:.2f}%); {} of {} pixels further than 6 levels from the reference ({:.2f}%), {} further than 24, mean {:.3f}; the two passes that name differ at {} pixels",
                     cut_by_reference, other, std::size_t{width} * height, 100.0 * static_cast<double>(other) / (static_cast<double>(width) * height), fence_either, fence_both,
                     100.0 * static_cast<double>(fence_both) / static_cast<double>(std::max<std::size_t>(fence_either, 1)), all.off, all.compared,
                     100.0 * static_cast<double>(all.off) / static_cast<double>(all.compared), loosely.off, all.mean, passes_differ);
        expect(eq(passes_differ, std::size_t{0}));
        // Measured on an RTX 4080: 6 pixels name another draw; of 9,297 fence pixels the two agree on 9,295; 138
        // pixels further than 6 levels (a texel's width along the shadows' edges), mean 0.13.
        expect(lt(static_cast<double>(other), 0.001 * width * height)) << "an edge's pixels, and a picket's";
        expect(gt(static_cast<double>(fence_both), 0.99 * static_cast<double>(fence_either)));
        expect(lt(static_cast<double>(all.off), 0.004 * static_cast<double>(all.compared)));
        expect(lt(all.mean, 0.3));
        static_cast<void>(device->wait_idle());
        for (const rhi::Texture made : {colour, depth, ids, who, shadows.map, shadows.depth}) device->destroy(made);
    });
    // light through a surface, on the device: a thin amber pane and a ball of glass whose inside takes the red's
    // complement by the way gone, before the yard — what is whole drawn first, its light kept at every size, the
    // glass reading it where the eye's ray, bent in and gone its thickness, leaves — are the reference's
    add(suite, "mesh.glass", [](Rig& rig) {
        rhi::Device* const device = &rig.device;
        Yard yard(true);
        yard.made.push_back(*geom::clustered(geom::sheet(2.4f, 1.6f, 4, 4)));
        yard.meshes.clear(), yard.grown.clear(), yard.trees.clear();
        for (const geom::ClusteredMesh& one : yard.made) yard.meshes.push_back(&one), yard.grown.push_back(geom::bvh_of(one.mesh));
        for (const geom::Bvh& tree : yard.grown) yard.trees.push_back(&tree);
        const std::uint32_t first = static_cast<std::uint32_t>(yard.materials.size()), sheet = static_cast<std::uint32_t>(yard.made.size() - 1);
        yard.materials.push_back({.base = {1.0f, 0.8f, 0.45f}, .roughness = 0.05f, .two_sided = true, .transmission = 1});
        yard.materials.push_back({.base = {1, 1, 1}, .roughness = 0.08f, .transmission = 1, .thickness = 2, .attenuation = {0.95f, 0.55f, 0.3f}, .attenuation_distance = 2.5f});
        const math::Quat upright = math::Quat::about({1, 0, 0}, 1.5707964f);
        expect(yard.world.add({.id = 40, .mesh = sheet, .material = first, .world = math::Transform{{-1.3f, 0.1f, 2.2f}, math::Quat::about({0, 1, 0}, 0.3f) * upright, {1, 1, 1}}.matrix(),
                               .bounds = geom::bounds(yard.made[sheet].mesh)})
                   .has_value());
        expect(yard.world.add({.id = 41, .mesh = 2, .material = first + 1, .world = math::Transform{{1.5f, -0.2f, 2.0f}, {}, {0.8f, 0.8f, 0.8f}}.matrix(), .bounds = geom::bounds(yard.made[2].mesh)})
                   .has_value());
        scene::Extracted seen;
        scene::extract(yard.world, yard.view(), seen);
        mesh::Targets reference;
        reference.resize(static_cast<int>(width), static_cast<int>(height));
        mesh::Pass on_cpu;
        on_cpu.draw(yard.world, seen, yard.view(), {yard.meshes, yard.materials, {}}, reference, {.shadows = false});
        mesh::Glow shown_on_cpu;
        shown_on_cpu.show(reference.light, static_cast<int>(width), static_cast<int>(height), {}, reference.colour);

        auto pass = mesh::gpu::Pass::make(*device, false, true);
        if (!(expect(pass.has_value()) << (pass ? "" : pass.error().message))) return;
        if (!(expect(pass->load(yard.meshes).has_value()))) return;
        pass->use(yard.materials);
        auto glow = mesh::gpu::Glow::make(*device);
        if (!(expect(glow.has_value()) << (glow ? "" : glow.error().message))) return;
        std::vector<rhi::Texture> all_made;
        const auto target = [&](rhi::Format format, std::string_view label) {
            const auto made = device->create(rhi::TextureDesc{format, width, height, rhi::is_depth(format) ? rhi::TextureUsage::depth_target : rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, label});
            if (!(expect(made.has_value()) << (made ? "" : made.error().message))) return rhi::Texture{};
            all_made.push_back(*made);
            return *made;
        };
        const rhi::Texture light = target(rhi::Format::rgba16_float, "mesh.light"), one = target(rhi::Format::rgba16_float, "mesh.glow.one"), other = target(rhi::Format::rgba16_float, "mesh.glow.other"),
                           colour = target(rhi::Format::rgba8_unorm, "mesh.colour"), depth = target(rhi::Format::d32_float, "mesh.depth");
        if (std::ranges::any_of(all_made, [](rhi::Texture made) { return !made; })) return;
        rhi::CommandList list;
        pass->record(list, yard.world, seen, yard.view(), yard.materials, light, depth, width, height, {.shadows = false});
        const mesh::gpu::Stats told = pass->last();
        glow->record(list, light, one, other, colour, width, height, {}, {});
        const auto submitted = device->submit(list);
        if (!(expect(submitted.has_value()) << (submitted ? "" : submitted.error().message))) return;
        expect(device->wait(*submitted).has_value());
        const auto pixels = device->read(colour, rhi::Region{0, 0, width, height});
        if (!(expect(pixels.has_value()) << (pixels ? "" : pixels.error().message))) return;
        keep(*pixels, "yard-glass");
        std::vector<std::byte> of_reference(reference.colour.size());
        std::memcpy(of_reference.data(), reference.colour.data(), reference.colour.size());
        keep(of_reference, "yard-glass-reference");
        expect(eq(told.glass, 2u) && gt(told.behind_sizes, 8u)) << told.glass << ' ' << told.behind_sizes;
        // Over all of it, and over the pixels the glass is seen at (the reference's own).
        const Apart all = apart(reference, *pixels, false, 6);
        std::size_t glass = 0, glass_off = 0;
        for (std::size_t pixel = 0; pixel < reference.ids.size(); ++pixel) {
            const std::uint32_t is = reference.ids[pixel].draw;
            if (is == mesh::none || reference.draws[is].id < 40) continue;
            ++glass;
            for (std::size_t channel = 0; channel < 3; ++channel)
                if (std::abs(static_cast<int>(reference.colour[pixel * 4 + channel]) - std::to_integer<int>((*pixels)[pixel * 4 + channel])) > 6) {
                    ++glass_off;
                    break;
                }
        }
        std::println("light through a pane and a ball of glass: {} of {} pixels further than 6 levels from the reference ({:.2f}%), mean {:.3f}, worst {}; of the {} the glass is seen at, {} ({:.2f}%); {} sizes kept",
                     all.off, all.compared, 100.0 * static_cast<double>(all.off) / static_cast<double>(all.compared), all.mean, all.worst, glass, glass_off,
                     100.0 * static_cast<double>(glass_off) / static_cast<double>(std::max<std::size_t>(glass, 1)), told.behind_sizes);
        expect(gt(glass, std::size_t{4000})) << glass;
        // Measured: 3 of 129,600 pixels and none of the glass's on an RTX 4080 and on lavapipe; 33 and 21 on SwiftShader (WebGPU, WebGL2).
        expect(lt(static_cast<double>(all.off), 0.0005 * static_cast<double>(all.compared)));
        expect(lt(static_cast<double>(glass_off), 0.002 * static_cast<double>(glass)));
        static_cast<void>(device->wait_idle());
        for (const rhi::Texture made : all_made) device->destroy(made);
    });
    // surfaces seen through, on the device: panes of glass before the yard and before one another, one of them thinning across its picture, are the reference's — each pixel's light what the nearest hides of it, then the next of what is left, whatever order they are drawn in; they throw no shadow and are not who stops the eye
    add(suite, "mesh.through", [](Rig& rig) {
        rhi::Device* const device = &rig.device;
        Yard yard(true);
        yard.made.push_back(*geom::clustered(geom::sheet(3, 1.5f, 6, 3)));
        yard.meshes.clear(), yard.grown.clear(), yard.trees.clear();
        for (const geom::ClusteredMesh& one : yard.made) yard.meshes.push_back(&one), yard.grown.push_back(geom::bvh_of(one.mesh));
        for (const geom::Bvh& tree : yard.grown) yard.trees.push_back(&tree);
        // A picture that hides less and less from left to right: all of the pane's light at its left, none at its right.
        std::vector<std::uint8_t> bytes(64 * 16 * 4, 0);
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 64; ++x) {
                const std::size_t at = static_cast<std::size_t>(y * 64 + x) * 4;
                bytes[at] = 255, bytes[at + 1] = 236, bytes[at + 2] = 200, bytes[at + 3] = static_cast<std::uint8_t>(255 - x * 4);
            }
        const mesh::Texture thinning = *mesh::Texture::made(bytes, 64, 16);
        const std::array<const mesh::Texture*, 1> textures{&thinning};
        const std::uint32_t first = static_cast<std::uint32_t>(yard.materials.size()), sheet = static_cast<std::uint32_t>(yard.made.size() - 1);
        yard.materials.push_back({.base = {0.2f, 0.5f, 0.9f}, .roughness = 0.3f, .opacity = 0.35f, .two_sided = true, .see_through = true});
        yard.materials.push_back({.base = {0.9f, 0.25f, 0.2f}, .roughness = 0.5f, .opacity = 0.6f, .two_sided = true, .see_through = true});
        yard.materials.push_back({.base = {0.3f, 0.85f, 0.4f}, .roughness = 0.5f, .opacity = 0.5f, .two_sided = true, .see_through = true});
        yard.materials.push_back({.base = {1, 1, 1}, .texture = 0, .opacity = 0.9f, .two_sided = true, .see_through = true});
        // Stood up before the eye one behind another and aside of one another — given the farthest first, then the
        // nearest, then the two between: where they are over one another the order they are laid in is their depths'.
        const math::Quat upright = math::Quat::about({1, 0, 0}, 1.5707964f);
        const auto pane = [&](scene::Id id, std::uint32_t material, math::Vec3 at, float turned) {
            expect(yard.world.add({.id = id, .mesh = sheet, .material = first + material, .world = math::Transform{at, math::Quat::about({0, 1, 0}, turned) * upright, {1, 1, 1}}.matrix(), .bounds = geom::bounds(yard.made[sheet].mesh)}).has_value());
        };
        pane(30, 2, {0.6f, 0.2f, 1.2f}, -0.2f), pane(31, 0, {-0.1f, 0.1f, 3.4f}, 0.15f), pane(32, 1, {0.3f, 0.5f, 2.3f}, 0.0f), pane(33, 3, {2.6f, -0.2f, 2.9f}, -0.5f);
        scene::Extracted seen;
        scene::extract(yard.world, yard.view(), seen);
        auto pass = mesh::gpu::Pass::make(*device, false, true);
        if (!(expect(pass.has_value()) << (pass ? "" : pass.error().message))) return;
        if (!(expect(pass->load(yard.meshes).has_value()))) return;
        const auto loaded = pass->load_textures(textures);
        if (!(expect(loaded.has_value()) << (loaded ? "" : loaded.error().message))) return;
        pass->use(yard.materials);
        auto glow = mesh::gpu::Glow::make(*device);
        if (!(expect(glow.has_value()) << (glow ? "" : glow.error().message))) return;
        std::vector<rhi::Texture> all_made;
        const auto target = [&](rhi::Format format, std::uint32_t wide, std::uint32_t high, std::string_view label) {
            const auto made = device->create(rhi::TextureDesc{format, wide, high, rhi::is_depth(format) ? rhi::TextureUsage::depth_target : rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, label});
            if (!(expect(made.has_value()) << (made ? "" : made.error().message))) return rhi::Texture{};
            all_made.push_back(*made);
            return *made;
        };
        const rhi::Texture light = target(rhi::Format::rgba16_float, width, height, "mesh.light"), one = target(rhi::Format::rgba16_float, width, height, "mesh.glow.one"),
                           other = target(rhi::Format::rgba16_float, width, height, "mesh.glow.other"), colour = target(rhi::Format::rgba8_unorm, width, height, "mesh.colour"),
                           depth = target(rhi::Format::d32_float, width, height, "mesh.depth"), who = target(rhi::Format::r32_float, width, height, "mesh.who");
        mesh::gpu::Shadows shadows{.size = 2048, .distance = 30};
        shadows.map = target(rhi::Format::r32_float, shadows.size, shadows.size, "mesh.shadow"), shadows.depth = target(rhi::Format::d32_float, shadows.size, shadows.size, "mesh.shadow.depth");
        mesh::gpu::Layers layers;
        layers.behind = target(rhi::Format::r32_float, width, height, "mesh.through.behind"), layers.deep[0] = target(rhi::Format::r32_float, width, height, "mesh.through.deep.0");
        layers.deep[1] = target(rhi::Format::r32_float, width, height, "mesh.through.deep.1"), layers.depth = target(rhi::Format::d32_float, width, height, "mesh.through.depth");
        layers.layer = target(rhi::Format::rgba16_float, width, height, "mesh.through.layer"), layers.gathered = target(rhi::Format::rgba16_float, width, height, "mesh.through.gathered");
        if (std::ranges::any_of(all_made, [](rhi::Texture made) { return !made; }) || all_made.size() != 14) return;
        bool shadowed = false;
        mesh::gpu::Stats told;
        const auto drawn = [&](const mesh::gpu::Layers* with, bool named) {
            rhi::CommandList list;
            pass->record(list, yard.world, seen, yard.view(), yard.materials, light, depth, width, height, {.shadows = shadowed}, &shadows, with);
            told = pass->last();
            glow->record(list, light, one, other, colour, width, height, {}, {});
            if (named) pass->record_who(list, yard.world, seen, yard.view(), who, depth, width, height);
            const auto submitted = device->submit(list);
            if (!(expect(submitted.has_value()) << (submitted ? "" : submitted.error().message))) return std::vector<std::byte>{};
            expect(device->wait(*submitted).has_value());
            auto pixels = device->read(colour, rhi::Region{0, 0, width, height});
            if (!(expect(pixels.has_value()) << (pixels ? "" : pixels.error().message))) return std::vector<std::byte>{};
            return std::move(*pixels);
        };
        // Twice: without shadows — the laying over by itself — and with the sun's, which the panes take and do not throw.
        std::array<std::size_t, 5> compared_bare{};
        std::vector<std::byte> pixels_bare;
        std::vector<std::uint8_t> panes;
        const auto against = [&] {
            mesh::Targets reference;
            reference.resize(static_cast<int>(width), static_cast<int>(height));
            mesh::Pass on_cpu;
            on_cpu.draw(yard.world, seen, yard.view(), {yard.meshes, yard.materials, yard.trees, textures}, reference, {.shadows = shadowed});
            mesh::Glow shown_on_cpu;
            shown_on_cpu.show(reference.light, static_cast<int>(width), static_cast<int>(height), {}, reference.colour);
            // How many of them are over one another, a pixel: the reference's own count.
            std::array<std::size_t, 6> deep{};
            panes.assign(std::size_t{width} * height, 0);
            for (const mesh::Through& one : reference.through) ++panes[one.pixel];
            for (const std::uint8_t count : panes) ++deep[std::min<std::size_t>(count, 5)];
            expect(gt(deep[1], std::size_t{5000}) && gt(deep[2], std::size_t{3000}) && gt(deep[3], std::size_t{500}) && gt(deep[4], std::size_t{300}) && eq(deep[5], std::size_t{0}))
                << deep[1] << ' ' << deep[2] << ' ' << deep[3] << ' ' << deep[4] << ' ' << deep[5];

            const std::vector<std::byte> pixels = drawn(&layers, true);
            if (pixels.empty()) return false;
            expect(eq(told.through, 4u) && eq(told.layers, 4u) && eq(told.through_whole, 0u)) << told.through << ' ' << told.layers << ' ' << told.through_whole;
            keep(pixels, shadowed ? "yard-panes-shadowed" : "yard-panes");
            std::vector<std::byte> of_reference(reference.colour.size());
            std::memcpy(of_reference.data(), reference.colour.data(), reference.colour.size());
            keep(of_reference, shadowed ? "yard-panes-shadowed-reference" : "yard-panes-reference");
            // Who stops the eye: never a pane (the pass that names instances, against the reference's names).
            const auto by_instance = device->read(who, rhi::Region{0, 0, width, height});
            if (!(expect(by_instance.has_value()))) return false;
            std::size_t named_a_pane = 0, named_another = 0;
            for (std::size_t pixel = 0; pixel < reference.ids.size(); ++pixel) {
                float instance = 0;
                std::memcpy(&instance, by_instance->data() + pixel * 4, 4);
                const std::uint32_t is = reference.ids[pixel].draw;
                const float is_instance = is == mesh::none ? 0.0f : static_cast<float>((reference.draws[is].id & 0xffffffu) + 1);
                named_a_pane += instance >= 31 && instance <= 34, named_another += instance != is_instance;
            }
            // The picture: over all of it, and over the pixels a pane is seen at — by how many are over one another.
            const Apart all = apart(reference, pixels, false, 6);
            // Within: a pixel whose eight neighbours have the same panes over the same thing — no edge of anything
            // goes through it, so what it shows is the laying over itself, not where an edge fell.
            std::array<std::size_t, 5> off{}, compared{}, off_within{}, within{};
            std::array<double, 5> sum{};
            const auto same_about = [&](std::uint32_t x, std::uint32_t y) {
                if (x == 0 || y == 0 || x + 1 == width || y + 1 == height) return false;
                const std::size_t pixel = std::size_t{y} * width + x;
                for (int down = -1; down <= 1; ++down)
                    for (int across = -1; across <= 1; ++across) {
                        const std::size_t beside = static_cast<std::size_t>(static_cast<int>(y) + down) * width + static_cast<std::size_t>(static_cast<int>(x) + across);
                        if (panes[beside] != panes[pixel] || reference.ids[beside].draw != reference.ids[pixel].draw) return false;
                    }
                return true;
            };
            for (std::uint32_t y = 0; y < height; ++y)
                for (std::uint32_t x = 0; x < width; ++x) {
                    const std::size_t pixel = std::size_t{y} * width + x, count = std::min<std::size_t>(panes[pixel], 4);
                    int most = 0;
                    for (std::size_t channel = 0; channel < 3; ++channel) most = std::max(most, std::abs(static_cast<int>(reference.colour[pixel * 4 + channel]) - std::to_integer<int>(pixels[pixel * 4 + channel])));
                    ++compared[count], off[count] += most > 6, sum[count] += most;
                    if (same_about(x, y)) ++within[count], off_within[count] += most > 6;
                }
            std::println("panes seen through, four of them, up to four over one another{}: {} of {} pixels further than 6 levels from the reference ({:.2f}%), mean {:.3f}, worst {}; behind one pane {} of {} "
                         "(a pixel's largest difference {:.2f} on average), behind two {} of {} ({:.2f}), behind three {} of {} ({:.2f}), behind four {} of {} ({:.2f}), with none {} of {}; away from every edge {} of {}, {} of {}, {} of {}, {} of {}; the pass that names who stops the eye names a "
                         "pane at {} pixels, another than the reference at {}",
                         shadowed ? ", with the sun's shadows by its map" : "", all.off, all.compared, 100.0 * static_cast<double>(all.off) / static_cast<double>(all.compared), all.mean, all.worst, off[1], compared[1], sum[1] / static_cast<double>(compared[1]), off[2],
                         compared[2], sum[2] / static_cast<double>(compared[2]), off[3], compared[3], sum[3] / static_cast<double>(compared[3]), off[4], compared[4], sum[4] / static_cast<double>(compared[4]), off[0], compared[0], off_within[1], within[1], off_within[2], within[2], off_within[3], within[3], off_within[4], within[4], named_a_pane, named_another);
            expect(eq(named_a_pane, std::size_t{0}));
            expect(lt(static_cast<double>(named_another), 0.001 * width * height));
            expect(lt(static_cast<double>(all.off), 0.002 * static_cast<double>(all.compared)));
            expect(lt(all.mean, 0.2));
            // Away from every edge the laying over is the reference's, however many are over one another. (With the
            // sun's shadows a shadow's edge crosses the panes too: the map's against the reference's ray, as everywhere.)
            for (const std::size_t count : {std::size_t{1}, std::size_t{2}, std::size_t{3}, std::size_t{4}})
                expect(gt(within[count], std::size_t{200}) && lt(static_cast<double>(off_within[count]), (shadowed ? 0.05 : 0.0005) * static_cast<double>(within[count]))) << count << ' ' << off_within[count] << ' ' << within[count];
            if (!shadowed) compared_bare = compared, pixels_bare = pixels;
            return true;
        };
        if (!against()) return;
        shadowed = true;
        if (!against()) return;
        shadowed = false;
        // One layer only: a pixel keeps its nearest pane and loses the others — where one pane is, nothing changes.
        layers.count = 1;
        const std::vector<std::byte> nearest_only = drawn(&layers, false);
        if (nearest_only.empty()) return;
        std::array<std::size_t, 5> changed{};
        for (std::size_t pixel = 0; pixel < panes.size(); ++pixel)
            changed[std::min<std::size_t>(panes[pixel], 4)] += std::memcmp(nearest_only.data() + pixel * 4, pixels_bare.data() + pixel * 4, 3) != 0;
        expect(eq(told.layers, 1u));
        expect(eq(changed[0], std::size_t{0}) && lt(static_cast<double>(changed[1]), 0.005 * static_cast<double>(compared_bare[1])) && gt(static_cast<double>(changed[3] + changed[4]), 0.9 * static_cast<double>(compared_bare[3] + compared_bare[4])))
            << changed[0] << ' ' << changed[1] << ' ' << changed[2] << ' ' << changed[3] << ' ' << changed[4];
        // With no layers given they are drawn whole, and say so.
        const std::vector<std::byte> whole = drawn(nullptr, false);
        expect(eq(told.through, 4u) && eq(told.through_whole, 4u) && eq(told.layers, 0u));
        std::println("with one layer {} of {} pixels behind one pane change, {} of {} behind two, {} of {} behind three, {} of {} behind four; drawn whole {} draws", changed[1], compared_bare[1], changed[2], compared_bare[2], changed[3],
                     compared_bare[3], changed[4], compared_bare[4], told.through_whole);
        static_cast<void>(device->wait_idle());
        for (const rhi::Texture made : all_made) device->destroy(made);
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
