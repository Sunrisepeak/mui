module omui.bench;

import std;
import xrender.scene;
import xrender.mesh;

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
            s.note("scope", "the tree keeps its shape and its boxes are refitted; it is made again every 64th frame (the largest time)");
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
}

}  // namespace omui::bench
