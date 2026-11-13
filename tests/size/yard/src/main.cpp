// The least program that shows three dimensions on a page: a yard drawn once on the page's WebGPU (or its
// WebGL2) and shown on its canvas. It prints what it drew; what it weighs is the point (size.budget.omui.json).
import std;
import mlibs.rhi;
import mlibs.geom;
import mlibs.math;
import xrender.mesh.gpu;
import xrender.rhi.webgpu;
import xrender.rhi.webgl;

namespace mesh = xrender::mesh;
namespace scene = xrender::scene;
namespace math = mlibs::math;
namespace geom = mlibs::geom;
namespace rhi = mlibs::rhi;

int main() {
    constexpr std::uint32_t width = 960, height = 540;
    auto device = xrender::rhi::webgpu::create({.canvas = "#canvas"});
    if (!device) device = xrender::rhi::webgl::create({.canvas = "#canvas"});
    if (!device) {
        std::println("no device on this page: {}", device.error().message);
        return 1;
    }
    const std::array<geom::ClusteredMesh, 3> made{*geom::clustered(geom::box({1, 1, 1})), *geom::clustered(geom::sheet(200, 200, 4, 4)), *geom::clustered(geom::sphere(1, 32, 64), 32)};
    const std::array<const geom::ClusteredMesh*, 3> meshes{&made[0], &made[1], &made[2]};
    const std::array<mesh::Material, 3> materials{{{{0.8f, 0.2f, 0.15f}, {}}, {{0.5f, 0.5f, 0.5f}, {}}, {{0.15f, 0.4f, 0.8f}, {}, 0.25f, 0}}};
    scene::Scene world;
    bool placed = world.add({.id = 1, .mesh = 1, .material = 1, .world = math::translation({0, -1, 0}), .bounds = geom::bounds(made[1].mesh)}).has_value();
    placed = placed && world.add({.id = 10, .mesh = 0, .material = 0, .world = math::translation({-2.2f, 0, 0}), .bounds = geom::bounds(made[0].mesh)}).has_value();
    placed = placed && world.add({.id = 11, .mesh = 2, .material = 2, .world = math::translation({0.4f, 0, 0.8f}), .bounds = geom::bounds(made[2].mesh)}).has_value();
    placed = placed && world.add(scene::Light{.id = 1, .kind = scene::LightKind::sun, .colour = {1, 0.96f, 0.9f}, .intensity = 0.9f, .toward = {-0.4f, -1, -0.5f}}).has_value();
    const scene::View view{.view = math::look_at({0.5f, 2.2f, 7.5f}, {0.2f, 0, 0}, {0, 1, 0}), .projection = math::perspective(1.0f, 16.0f / 9.0f, 0.5f, 100.0f)};
    scene::Extracted seen;
    scene::extract(world, view, seen);

    auto pass = mesh::gpu::Pass::make(**device);
    const auto colour = (*device)->create(rhi::TextureDesc{rhi::Format::rgba8_unorm, width, height, rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, "yard.colour"});
    const auto depth = (*device)->create(rhi::TextureDesc{rhi::Format::d32_float, width, height, rhi::TextureUsage::depth_target, "yard.depth"});
    if (!placed || !pass || !colour || !depth || !pass->load(meshes)) {
        std::println("the yard could not be made");
        return 1;
    }
    rhi::CommandList list;
    pass->record(list, world, seen, view, materials, *colour, *depth, width, height, {.background = {0.5f, 0.6f, 0.7f}, .ambient = {0.2f, 0.24f, 0.3f}, .shadows = false});
    const auto submitted = (*device)->submit(list);
    const auto shown = submitted ? (*device)->present(*colour) : rhi::Result<bool>{false};
    std::println("a yard on {}: {} draws, {} triangles{}", (*device)->caps().device, pass->last().draws, pass->last().triangles, shown && *shown ? ", shown" : ", not shown");
    return submitted ? 0 : 1;
}
