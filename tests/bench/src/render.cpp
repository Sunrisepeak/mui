module omui.bench;

import std;
import xrender.scene;
import xrender.mesh;
import xrender.mesh.gpu;
import xrender.rhi.sdl;
import mlibs.rhi;
import mcpplibs.os;

namespace omui::bench {

namespace {

namespace scene = xrender::scene;
namespace math = mlibs::math;

float unit(math::SplitMix64& random) { return static_cast<float>(math::unit24(random.next())); }
// Ten thousand things through a space 400 across: 24 meshes of 12 materials.
void fill(scene::Scene& world, std::uint32_t count) {
    math::SplitMix64 random{77};
    for (std::uint32_t index = 0; index < count; ++index) {
        const math::Vec3 at{unit(random) * 400 - 200, unit(random) * 40 - 20, unit(random) * 400 - 200};
        const float size = 0.5f + unit(random) * 2;
        (void)world.add({.id = 1 + std::uint64_t{index},
                         .mesh = static_cast<std::uint32_t>(random.next() % 24),
                         .material = static_cast<std::uint32_t>(random.next() % 12),
                         .world = math::Transform{at, math::Quat::about({0, 1, 0}, unit(random) * 6.28f), {size, size, size}}.matrix(),
                         .bounds = {{-1, -1, -1}, {1, 1, 1}}});
    }
    (void)world.add(scene::Light{.id = 1});
}
// A camera walking a circle and looking across the field.
scene::View view_at(std::uint64_t frame) {
    const float turn = static_cast<float>(frame % 628) * 0.01f;
    return {.view = math::look_at({std::cos(turn) * 150, 12, std::sin(turn) * 150}, {0, 0, 0}, {0, 1, 0}), .projection = math::perspective(1.0f, 16.0f / 9.0f, 0.5f, 250.0f)};
}

}  // namespace

void add_render_benchmarks() {
    add("render/scene-extract-10k", "what a view sees of 10,000 instances, every instance looked at (the reference): culled, sorted by material, mesh and depth, cut into batches",
        [](State& s) {
            scene::Scene world;
            fill(world, 10000);
            scene::Extracted seen;
            for (std::uint64_t frame = 0; frame < 628; frame += 37) scene::extract(world, view_at(frame), seen);  // the buffers at their size
            s.items(10000);
            std::uint64_t frame = 0, drawn = 0;
            while (s.next()) scene::extract(world, view_at(frame++), seen), drawn += seen.draws.size();
            s.note("scope", std::format("{} draws a view of 10,000, {} batches", frame ? drawn / frame : 0, seen.batches.size()));
        });
    add("render/scene-extract-10k-tree", "the same by the scene's tree: only what a box of it lets through is looked at; the same draws",
        [](State& s) {
            scene::Scene world;
            fill(world, 10000);
            scene::Spatial spatial;
            (void)spatial.follow(world);
            scene::Extracted seen;
            for (std::uint64_t frame = 0; frame < 628; frame += 37) scene::extract(world, spatial, view_at(frame), seen);
            s.items(10000);
            std::uint64_t frame = 0, looked = 0;
            while (s.next()) scene::extract(world, spatial, view_at(frame++), seen), looked += seen.looked;
            s.note("scope", std::format("{} of 10,000 looked at a view", frame ? looked / frame : 0));
        });
    add("render/scene-follow-10k", "a frame's changes followed by the scene's mirror: 100 of 10,000 instances moved, the tree's boxes refitted",
        [](State& s) {
            scene::Scene world;
            fill(world, 10000);
            scene::Spatial spatial;
            (void)spatial.follow(world);
            world.settle();
            math::SplitMix64 random{5};
            s.items(100);
            while (s.next()) {
                for (int moved = 0; moved < 100; ++moved)
                    (void)world.move(1 + random.next() % 10000, math::translation({unit(random) * 400 - 200, unit(random) * 40 - 20, unit(random) * 400 - 200}));
                (void)spatial.follow(world);
                world.settle();
            }
            s.note("scope", std::format("the tree keeps its shape and its boxes are refitted; after 64 refits a new one is made beside it, {} things looked at a frame, and takes its place: {} times in this run", scene::Spatial::rebuild_things, spatial.remade()));
        });
    add("render/mesh-reference-yard", "the CPU reference of the mesh pass: a floor, a box, two balls and a post (6,092 triangles in 256 clusters) drawn at 480x270 with colour, depth, normal, velocity and ids",
        [](State& s) {
            namespace mesh = xrender::mesh;
            namespace geom = mlibs::geom;
            const std::array<geom::ClusteredMesh, 4> made{*geom::clustered(geom::box({1, 1, 1})), *geom::clustered(geom::sheet(200, 200, 4, 4)), *geom::clustered(geom::sphere(1, 32, 64), 32),
                                                          *geom::clustered(geom::cylinder(0.7f, 2, 48), 32)};
            const std::array<const geom::ClusteredMesh*, 4> meshes{&made[0], &made[1], &made[2], &made[3]};
            const std::array<mesh::Material, 4> materials{{{{0.8f, 0.2f, 0.15f}, {}}, {{0.5f, 0.5f, 0.5f}, {}}, {{0.15f, 0.4f, 0.8f}, {}}, {{0.9f, 0.75f, 0.2f}, {}}}};
            scene::Scene world;
            const auto put = [&](scene::Id id, std::uint32_t which, std::uint32_t material, const math::Mat4& where) {
                (void)world.add({.id = id, .mesh = which, .material = material, .world = where, .bounds = geom::bounds(made[which].mesh)});
            };
            put(1, 1, 1, math::translation({0, -1, 0}));
            put(10, 0, 0, math::Transform{{-2.2f, 0, 0}, math::Quat::about({0, 1, 0}, 0.6f), {1, 1, 1}}.matrix());
            put(11, 2, 2, math::translation({0.4f, 0, 0.8f}));
            put(12, 3, 3, math::translation({2.6f, 0, -0.5f}));
            put(13, 2, 3, math::Transform{{1.2f, 1.6f, -1.5f}, {}, {0.25f, 0.25f, 0.25f}}.matrix());
            (void)world.add(scene::Light{.id = 1, .intensity = 0.9f, .toward = {-0.4f, -1, -0.5f}});
            (void)world.add(scene::Light{.id = 2, .kind = scene::LightKind::point, .colour = {1, 0.6f, 0.2f}, .intensity = 2.5f, .at = {1.2f, 1.6f, -1.5f}, .range = 6});
            const scene::View view{.view = math::look_at({0.5f, 2.2f, 7.5f}, {0.2f, 0, 0}, {0, 1, 0}), .projection = math::perspective(1.0f, 16.0f / 9.0f, 0.5f, 100.0f)};
            scene::Extracted seen;
            scene::extract(world, view, seen);
            mesh::Pass pass;
            mesh::Targets frame;
            frame.resize(480, 270);
            pass.draw(world, seen, view, {meshes, materials}, frame);
            s.items(480 * 270);
            while (s.next()) pass.draw(world, seen, view, {meshes, materials}, frame);
            s.note("scope", std::format("{} pixels written, {} hidden; {} of {} triangles filled; one thread", pass.last().written, pass.last().hidden, pass.last().filled, pass.last().triangles));
        });
    // The yard on this machine's device, as a frame is made of it. `panes`: four sheets of glass before it, laid over the frame in four layers.
    // `about`: lit by a picture of the surroundings (a sky, a ground, a lamp: 1024 × 512, its squares from 512 across).
    const auto yard_on_device = [](State& s, bool panes, bool about) {
        namespace mesh = xrender::mesh;
        namespace geom = mlibs::geom;
        namespace rhi = mlibs::rhi;
        constexpr std::uint32_t width = 1920, height = 1080;
        xrender::rhi::sdl::Options options;
        options.formats = mesh::gpu::Pass::shader_formats();
        if (auto library = mcpplibs::os::env::get("OMUI_VULKAN_LIBRARY")) options.vulkan_library = *library;
        if (auto video = mcpplibs::os::env::get("OMUI_GPU_VIDEO_DRIVER")) options.video_driver = *video;
        auto device = xrender::rhi::sdl::create(options);
        if (!device) {
            s.skip(std::format("no GPU device ({})", device.error().message));
            return;
        }
        rhi::Device& on = **device;
        s.note("device", std::format("{} ({}{})", on.caps().device, rhi::name(on.caps().backend), on.caps().software ? ", software" : ""));
        const std::array<geom::ClusteredMesh, 5> made{*geom::clustered(geom::box({1, 1, 1})), *geom::clustered(geom::sheet(200, 200, 4, 4)), *geom::clustered(geom::sphere(1, 32, 64), 32),
                                                      *geom::clustered(geom::cylinder(0.7f, 2, 48), 32), *geom::clustered(geom::sheet(6, 3, 6, 3))};
        const std::array<const geom::ClusteredMesh*, 5> meshes{&made[0], &made[1], &made[2], &made[3], &made[4]};
        std::vector<mesh::Material> materials{{{0.8f, 0.2f, 0.15f}, {}}, {{0.5f, 0.5f, 0.5f}, {}}, {{0.15f, 0.4f, 0.8f}, {}, 0.25f, 0}, {{1.0f, 0.77f, 0.34f}, {}, 0.3f, 1}, {{0.1f, 0.1f, 0.1f}, {9.0f, 5.0f, 1.6f}}};
        materials.push_back({.base = {0.2f, 0.5f, 0.9f}, .roughness = 0.3f, .opacity = 0.35f, .two_sided = true, .see_through = true});
        materials.push_back({.base = {0.9f, 0.25f, 0.2f}, .roughness = 0.5f, .opacity = 0.6f, .two_sided = true, .see_through = true});
        materials.push_back({.base = {0.3f, 0.85f, 0.4f}, .roughness = 0.5f, .opacity = 0.5f, .two_sided = true, .see_through = true});
        materials.push_back({.base = {0.95f, 0.9f, 0.8f}, .roughness = 0.2f, .opacity = 0.25f, .two_sided = true, .see_through = true});
        scene::Scene world;
        const auto put = [&](scene::Id id, std::uint32_t which, std::uint32_t material, const math::Mat4& where, bool casts = true) {
            (void)world.add({.id = id, .mesh = which, .material = material, .world = where, .bounds = geom::bounds(made[which].mesh), .casts = casts});
        };
        put(1, 1, 1, math::translation({0, -1, 0}));
        put(10, 0, 0, math::Transform{{-2.2f, 0, 0}, math::Quat::about({0, 1, 0}, 0.6f), {1, 1, 1}}.matrix());
        put(11, 2, 2, math::translation({0.4f, 0, 0.8f}));
        put(12, 3, 3, math::translation({2.6f, 0, -0.5f}));
        put(13, 2, 4, math::Transform{{1.2f, 1.6f, -1.5f}, {}, {0.25f, 0.25f, 0.25f}}.matrix(), false);
        if (panes) {
            const math::Quat upright = math::Quat::about({1, 0, 0}, 1.5707964f);
            put(30, 4, 7, math::Transform{{0.6f, 0.6f, 1.6f}, math::Quat::about({0, 1, 0}, -0.2f) * upright, {1, 1, 1}}.matrix());
            put(31, 4, 5, math::Transform{{-0.2f, 0.5f, 4.2f}, math::Quat::about({0, 1, 0}, 0.15f) * upright, {1, 1, 1}}.matrix());
            put(32, 4, 6, math::Transform{{0.3f, 0.8f, 3.0f}, upright, {1, 1, 1}}.matrix());
            put(33, 4, 8, math::Transform{{0.9f, 0.4f, 5.0f}, math::Quat::about({0, 1, 0}, -0.3f) * upright, {1, 1, 1}}.matrix());
        }
        (void)world.add(scene::Light{.id = 1, .kind = scene::LightKind::sun, .colour = {1, 0.96f, 0.9f}, .intensity = 0.9f, .toward = {-0.4f, -1, -0.5f}});
        (void)world.add(scene::Light{.id = 2, .kind = scene::LightKind::point, .colour = {1, 0.6f, 0.2f}, .intensity = 2.5f, .at = {1.2f, 1.6f, -1.5f}, .range = 6});
        (void)world.add(scene::Light{.id = 3, .kind = scene::LightKind::spot, .colour = {0.4f, 0.7f, 1}, .intensity = 3, .at = {-2.2f, 4, 1}, .toward = {0, -1, -0.2f}, .range = 9, .cone = 0.5f});
        const scene::View view{.view = math::look_at({0.5f, 2.2f, 7.5f}, {0.2f, 0, 0}, {0, 1, 0}), .projection = math::perspective(1.0f, 16.0f / 9.0f, 0.5f, 100.0f)};
        scene::Extracted seen;
        scene::extract(world, view, seen);

        auto pass = mesh::gpu::Pass::make(on, false, true);
        auto open = mesh::gpu::Open::make(on);
        auto glow = mesh::gpu::Glow::make(on);
        if (!pass || !open || !glow || !pass->load(meshes)) {
            s.fail("the mesh pass could not be made on this device");
            return;
        }
        pass->use(materials);
        std::optional<mesh::Surroundings> surroundings;
        if (about) {
            constexpr int across = 1024, down_rows = 512;
            const math::Vec3 lamp_way = math::normalized(math::Vec3{1, 1, 0.6f});
            std::vector<float> rgb(static_cast<std::size_t>(across) * down_rows * 3);
            for (int row = 0; row < down_rows; ++row)
                for (int column = 0; column < across; ++column) {
                    const double down = (row + 0.5) / down_rows * math::det::pi, turn = ((column + 0.5) / across - 0.5) * math::det::two_pi;
                    const math::Vec3 way{static_cast<float>(math::det::sin(down) * math::det::cos(turn)), static_cast<float>(math::det::cos(down)), static_cast<float>(math::det::sin(down) * math::det::sin(turn))};
                    math::Vec3 from = way.y > 0 ? math::Vec3{0.3f, 0.5f, 0.9f} * (0.5f + 0.9f * way.y) : math::Vec3{0.3f, 0.24f, 0.18f};
                    from = from + math::Vec3{12, 10, 7} * static_cast<float>(math::det::exp(-(1.0 - static_cast<double>(math::dot(way, lamp_way))) / 0.01));
                    const std::size_t at = (static_cast<std::size_t>(row) * across + static_cast<std::size_t>(column)) * 3;
                    rgb[at] = from.x, rgb[at + 1] = from.y, rgb[at + 2] = from.z;
                }
            surroundings = mesh::Surroundings::made(rgb, across, down_rows);
            if (!surroundings || !pass->load_surroundings(&*surroundings)) {
                s.fail("the surroundings could not be loaded on this device");
                return;
            }
        }
        std::vector<rhi::Texture> textures;
        const auto target = [&](rhi::Format format, std::uint32_t across, std::uint32_t down, std::string_view label) {
            const auto one = on.create(rhi::TextureDesc{format, across, down, rhi::is_depth(format) ? rhi::TextureUsage::depth_target : rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, label});
            if (one) textures.push_back(*one);
            return one ? *one : rhi::Texture{};
        };
        mesh::gpu::Shadows shadows{.size = 2048, .distance = 30, .lamp_size = 512, .lamps = 2};
        const rhi::Texture colour = target(rhi::Format::rgba8_unorm, width, height, "bench.colour"), depth = target(rhi::Format::d32_float, width, height, "bench.depth"),
                           light = target(rhi::Format::rgba16_float, width, height, "bench.light"), one = target(rhi::Format::rgba16_float, width, height, "bench.one"),
                           other = target(rhi::Format::rgba16_float, width, height, "bench.other"), surface = target(rhi::Format::rgba16_float, width, height, "bench.surface");
        shadows.open = target(rhi::Format::r32_float, width, height, "bench.open");
        shadows.map = target(rhi::Format::r32_float, shadows.size, shadows.size, "bench.shadow"), shadows.depth = target(rhi::Format::d32_float, shadows.size, shadows.size, "bench.shadow.depth");
        shadows.lamps_map = target(rhi::Format::r32_float, shadows.lamp_size * 6, shadows.lamp_size * shadows.lamps, "bench.lamps");
        shadows.lamps_depth = target(rhi::Format::d32_float, shadows.lamp_size * 6, shadows.lamp_size * shadows.lamps, "bench.lamps.depth");
        mesh::gpu::Layers layers;
        if (panes) {
            layers.behind = target(rhi::Format::r32_float, width, height, "bench.through.behind"), layers.deep[0] = target(rhi::Format::r32_float, width, height, "bench.through.deep.0");
            layers.deep[1] = target(rhi::Format::r32_float, width, height, "bench.through.deep.1"), layers.depth = target(rhi::Format::d32_float, width, height, "bench.through.depth");
            layers.layer = target(rhi::Format::rgba16_float, width, height, "bench.through.layer"), layers.gathered = target(rhi::Format::rgba16_float, width, height, "bench.through.gathered");
        }
        const mesh::Options options_of{.background = {0.5f, 0.6f, 0.7f}, .ambient = {0.2f, 0.24f, 0.3f}, .shadows = true, .occlusion_rays = 8, .occlusion_reach = 1.5f, .environment = {.on = true, .picture = surroundings ? &*surroundings : nullptr}};
        const mesh::Bloom bloom{.threshold = 1, .radius = 7, .strength = 0.6f};
        rhi::CommandList list;  // kept: a list cleared keeps its room, so a frame allocates nothing for it
        const auto frame = [&] {
            const auto begun = std::chrono::steady_clock::now();
            list.clear();
            pass->record_surface(list, world, seen, view, surface, depth, width, height, options_of);
            open->record(list, view, surface, shadows.open, width, height, options_of);
            pass->record(list, world, seen, view, materials, light, depth, width, height, options_of, &shadows, panes ? &layers : nullptr);
            glow->record(list, light, one, other, colour, width, height, bloom, {.exposure = 1, .filmic = true});
            const auto submitted = on.submit(list);
            s.counter("encode_ms", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begun).count());
            const auto waited = on.wait_idle();
            if (!submitted || !waited) {
                s.fail(std::format("the frame failed: {}", !submitted ? submitted.error().message : waited.error().message));
                return;
            }
            s.counter("gpu_ms", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begun).count());
            s.counter("software", on.caps().software ? 1 : 0);
        };
        frame();
        s.items(width * height);
        while (s.next()) frame();
        s.note("scope", std::format("{} draws, {} casters in the sun's map, {} views of lamps; {} draws seen through, laid in {} layers; {} passes a frame; {}the same frame the device suite holds to the reference at 480x270",
                                    pass->last().draws, pass->last().casters, pass->last().lamp_views, pass->last().through, pass->last().layers, 12 + (pass->last().layers ? 2 + 2 * pass->last().layers : 0),
                                    pass->last().about ? "lit by a picture of the surroundings; " : ""));
        static_cast<void>(on.wait_idle());
        for (const rhi::Texture made_one : textures) on.destroy(made_one);
    };
    add("render/mesh-device-yard-1080p", "the mesh pass on this machine's device (xrender.mesh.gpu through SDL_GPU): the yard at 1920x1080 as a frame is made of it — the surface for occlusion, how open each place is (8 rays of 12 steps), the sun's map (2048²), a lamp's and a spot's six views (512²), the light, the glow and the display's curve — submitted and waited for",
        [yard_on_device](State& s) { yard_on_device(s, false, false); });
    add("render/mesh-device-panes-1080p", "the same frame with four panes of glass before the yard, each most of the picture across and up to four over one another (xrender.mesh.gpu's `Layers`): how deep what stops the eye is, four layers each drawn and put under the ones before, and all of them over the frame — submitted and waited for",
        [yard_on_device](State& s) { yard_on_device(s, true, false); });
    add("render/mesh-device-about-1080p", "the same frame, panes and all, lit by a picture of the surroundings (xrender.mesh.gpu's `load_surroundings`): what falls on each surface and what it mirrors read from the picture's squares on one sheet — at most five reads a pixel a layer more than the constant light takes",
        [yard_on_device](State& s) { yard_on_device(s, true, true); });
    add("render/mesh-device-tiles-1080p", "a textured floor on this machine's device (xrender.mesh.gpu through SDL_GPU): tiles of a 64x64 texture of seven levels to the horizon at 1920x1080, every pixel up to eight places of its strip each one read between texels and between levels, under a sun",
        [](State& s) {
            namespace mesh = xrender::mesh;
            namespace geom = mlibs::geom;
            namespace rhi = mlibs::rhi;
            constexpr std::uint32_t width = 1920, height = 1080;
            xrender::rhi::sdl::Options options;
            options.formats = mesh::gpu::Pass::shader_formats();
            if (auto library = mcpplibs::os::env::get("OMUI_VULKAN_LIBRARY")) options.vulkan_library = *library;
            if (auto video = mcpplibs::os::env::get("OMUI_GPU_VIDEO_DRIVER")) options.video_driver = *video;
            auto device = xrender::rhi::sdl::create(options);
            if (!device) {
                s.skip(std::format("no GPU device ({})", device.error().message));
                return;
            }
            rhi::Device& on = **device;
            s.note("device", std::format("{} ({}{})", on.caps().device, rhi::name(on.caps().backend), on.caps().software ? ", software" : ""));
            // The floor's texture coordinates repeat every four units.
            geom::ClusteredMesh floor = *geom::clustered(geom::sheet(200, 200, 4, 4));
            for (math::Vec2& uv : floor.mesh.uvs) uv = {uv.x * 50, uv.y * 50};
            const std::array<const geom::ClusteredMesh*, 1> meshes{&floor};
            std::vector<std::uint8_t> bytes(64 * 64 * 4, 255);
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x) {
                    const std::uint8_t level = ((x / 8) + (y / 8)) % 2 ? 235 : 70;
                    const std::size_t at = static_cast<std::size_t>(y * 64 + x) * 4;
                    bytes[at] = level, bytes[at + 1] = level, bytes[at + 2] = level;
                }
            const mesh::Texture tiles = *mesh::Texture::made(bytes, 64, 64);
            const std::array<const mesh::Texture*, 1> pictures{&tiles};
            const std::array<mesh::Material, 1> materials{{{{1, 1, 1}, {}, 1, 0, 0}}};
            scene::Scene world;
            (void)world.add({.id = 1, .mesh = 0, .material = 0, .world = math::translation({0, -1, 0}), .bounds = geom::bounds(floor.mesh)});
            (void)world.add(scene::Light{.id = 1, .kind = scene::LightKind::sun, .colour = {1, 0.96f, 0.9f}, .intensity = 0.9f, .toward = {-0.4f, -1, -0.5f}});
            const scene::View view{.view = math::look_at({0.5f, 2.2f, 7.5f}, {0.2f, 0, 0}, {0, 1, 0}), .projection = math::perspective(1.0f, 16.0f / 9.0f, 0.5f, 100.0f)};
            scene::Extracted seen;
            scene::extract(world, view, seen);
            auto pass = mesh::gpu::Pass::make(on);
            if (!pass || !pass->load(meshes) || !pass->load_textures(pictures)) {
                s.fail("the mesh pass and its texture could not be made on this device");
                return;
            }
            const auto colour = on.create(rhi::TextureDesc{rhi::Format::rgba8_unorm, width, height, rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, "bench.colour"});
            const auto depth = on.create(rhi::TextureDesc{rhi::Format::d32_float, width, height, rhi::TextureUsage::depth_target, "bench.depth"});
            if (!colour || !depth) {
                s.fail("the frame's targets could not be made");
                return;
            }
            const mesh::Options options_of{.background = {0.5f, 0.6f, 0.7f}, .ambient = {0.2f, 0.24f, 0.3f}, .shadows = false};
            rhi::CommandList list;
            const auto frame = [&] {
                const auto begun = std::chrono::steady_clock::now();
                list.clear();
                pass->record(list, world, seen, view, materials, *colour, *depth, width, height, options_of);
                const auto submitted = on.submit(list);
                s.counter("encode_ms", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begun).count());
                const auto waited = on.wait_idle();
                if (!submitted || !waited) {
                    s.fail(std::format("the frame failed: {}", !submitted ? submitted.error().message : waited.error().message));
                    return;
                }
                s.counter("gpu_ms", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begun).count());
                s.counter("software", on.caps().software ? 1 : 0);
            };
            frame();
            s.items(width * height);
            while (s.next()) frame();
            s.note("scope", std::format("{} draw textured; one pass a frame; the picture the device suite holds to the reference at 480x270 (mesh.textures)", pass->last().textured));
            static_cast<void>(on.wait_idle());
            on.destroy(*colour), on.destroy(*depth);
        });
    add("render/mesh-reference-glow", "the CPU reference of the glow: a picture of light, 480x270 with a lamp's globe and its pool of light brighter than the display, spread by 7 pixels three times each way and shown",
        [](State& s) {
            namespace mesh = xrender::mesh;
            constexpr int width = 480, height = 270;
            std::vector<math::Vec3> light(static_cast<std::size_t>(width) * height, math::Vec3{0.2f, 0.22f, 0.25f});
            // A globe of (9, 5, 1.6) and a pool of light under it a little over the display's white.
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x) {
                    const int globe = (x - 240) * (x - 240) + (y - 70) * (y - 70), pool = (x - 240) * (x - 240) / 4 + (y - 200) * (y - 200);
                    if (globe < 12 * 12) light[static_cast<std::size_t>(y) * width + x] = {9.0f, 5.0f, 1.6f};
                    else if (pool < 40 * 40) light[static_cast<std::size_t>(y) * width + x] = {1.3f, 1.1f, 0.8f};
                }
            mesh::Glow glow;
            std::vector<std::uint8_t> colour;
            const mesh::Bloom bloom{.threshold = 1, .radius = 7, .strength = 0.6f};
            mesh::Glow::Told told = glow.show(light, width, height, bloom, colour);
            s.items(width * height);
            while (s.next()) told = glow.show(light, width, height, bloom, colour);
            s.note("scope", std::format("{} pixels brighter than the display; six passes of 15 texels; one thread", told.bright));
        });
}

}  // namespace omui::bench
