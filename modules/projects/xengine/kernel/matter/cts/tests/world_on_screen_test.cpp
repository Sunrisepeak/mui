// A 2D world of matter with bodies and cells in flight on screen (M6.2 T2.5): the field shaded by the standard
// appearance (bodies are cells of it, so they are drawn as the field is), what flies drawn where it is by the same
// shade, the light as a glow; a frame at a time, read back as pixels and kept as PNG. The scene is the slice's
// signature: a basin with a crate of wood afloat, a plank burning on two pillars, gunpowder going off in sand.
import std;
import xengine.matter.json;
import xengine.cellular.rigid;
import xengine.cellular.present.matter;
import mlibs.image;
import mlibs.hash;
import mlibs.testing;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
namespace matter = xengine::matter;
namespace ca = xengine::cellular;
namespace bodies = xengine::cellular::rigid;
namespace geom = mlibs::geom;
namespace raster = xrender::raster;

namespace {
constexpr int scale = 6;
const matter::Program& standard() {
    static const matter::Program program = *matter::standard();
    return program;
}

struct Scene {
    std::unique_ptr<bodies::MatterWorld> world;
    xrender::ImageStore images;
    ca::FieldPresenter view;
    xrender::DisplayList list;
    std::vector<float> centres;
    ca::Shape shape{128, 72};

    Scene()
        : world(*bodies::MatterWorld::make(standard(), {.shape = {128, 72}, .tile = {8, 8}, .seed = 11, .fill = "air", .flying = {.throw_at = 300}})),
          view(ca::FieldPresenter::make(images, {128, 72}, static_cast<std::uint32_t>(standard().channels().size()), ca::lower(*matter::standard_look(standard())), {16, 16}).value()) {
        const auto material = [](std::string_view name) { return *standard().material(name); };
        world->place({0, 68, 128, 4}, material("stone"));
        // The basin, its water, and a crate of wood over it.
        world->place({4, 44, 2, 24}, material("stone"));
        world->place({60, 44, 2, 24}, material("stone"));
        world->place({6, 52, 54, 16}, material("water"));
        world->place({24, 30, 10, 8}, material("wood"));
        // A plank across two pillars, lit in the middle.
        world->place({74, 50, 3, 18}, material("stone"));
        world->place({108, 50, 3, 18}, material("stone"));
        world->place({74, 48, 37, 2}, material("wood"));
        // A heap of sand with gunpowder in it.
        world->place({84, 60, 16, 8}, material("sand"));
        world->place({90, 64, 4, 3}, material("gunpowder"));
        (void)world->carve({6, 20, 50, 20});
        (void)world->carve({74, 46, 37, 4});
        world->set(*standard().channel("temperature"), {91, 48, 2, 2}, 900 * 256);
        world->set(*standard().channel("temperature"), {90, 64, 4, 3}, 900 * 256);
    }
    raster::Surface frame() {
        expect(view.present(world->cells()).has_value());
        const float width = static_cast<float>(shape.width) * scale, height = static_cast<float>(shape.height) * scale;
        list.clear();
        list.fill_rect({0, 0, width, height}, geom::Color::hex(0x0d1117));
        view.draw(list, {0, 0, width, height});
        world->flying().centres(centres);
        view.draw_loose(list, {0, 0, width, height}, centres, world->flying().values());
        view.draw_glow(list, {0, 0, width, height}, 2.0f * scale, 0.9f);
        raster::Surface out(static_cast<int>(shape.width) * scale, static_cast<int>(shape.height) * scale);
        xrender::CpuRenderer renderer;
        (void)renderer.render(list, out, {&images, nullptr});
        return out;
    }
};

std::uint64_t hash_of(const raster::Surface& picture) {
    mlibs::hash::Fnv1a hash;
    for (const raster::Rgba8 pixel : picture.pixels()) hash.le(pixel.r, 1).le(pixel.g, 1).le(pixel.b, 1).le(pixel.a, 1);
    return hash.value();
}
std::filesystem::path keep(const raster::Surface& picture, std::string_view name) {
    static const std::filesystem::path directory = scratch_dir("matter-world-on-screen");
    const auto file = directory / std::format("{}.png", name);
    const mlibs::image::View view{reinterpret_cast<const std::uint8_t*>(picture.pixels().data()), picture.width(), picture.height(), picture.width() * 4};
    expect(mlibs::image::save_png(file, view).has_value()) << file.generic_string();
    return file;
}
}  // namespace

UTest cases{"matter.world.on_screen", [] {
    "the slice's scene on screen: what flies is drawn where it is in its own colour, the crate where its body is, the same frames the same pixels"_test = [] {
        Scene scene;
        std::vector<std::uint64_t> hashes;
        std::size_t most_flying = 0;
        bool drawn_in_flight = false;
        for (int frame = 0; frame <= 240; ++frame) {
            if (frame) scene.world->step();
            most_flying = std::max(most_flying, scene.world->flying().flying());
            const bool keep_it = frame == 0 || frame == 30 || frame == 90 || frame == 240;
            const bool look_at_flight = !drawn_in_flight && scene.world->flying().flying() > 10;
            if (!keep_it && !look_at_flight) continue;
            const auto picture = scene.frame();
            hashes.push_back(hash_of(picture));
            if (keep_it) std::println("kept {}", keep(picture, std::format("frame-{:03}", frame)).generic_string());
            if (look_at_flight) {
                // Each particle's middle pixel is its cell's shade, not the backdrop or the air's.
                scene.world->flying().centres(scene.centres);
                std::size_t matched = 0, looked = 0;
                for (std::size_t particle = 0; particle < scene.centres.size() / 2; ++particle) {
                    const int px = static_cast<int>(scene.centres[2 * particle] * scale), py = static_cast<int>(scene.centres[2 * particle + 1] * scale);
                    if (px < 0 || py < 0 || px >= picture.width() || py >= picture.height()) continue;
                    ++looked;
                    const raster::Rgba8 pixel = picture.at(px, py);
                    matched += pixel.r + pixel.g + pixel.b > 3 * 0x30;  // lighter than the night air behind it
                }
                expect(looked > 5u && matched * 10 >= looked * 8) << std::format("{} of {} particles drawn in their colour", matched, looked);
                std::println("kept {}", keep(picture, "in-flight").generic_string());
                drawn_in_flight = true;
            }
        }
        expect(drawn_in_flight) << std::format("sand flew ({} at most)", most_flying);
        // The crate: where its body holds cells, the picture is wood, not water or air.
        const auto all = scene.world->bodies().bodies();
        expect(fatal(!all.empty()));
        const auto last = scene.frame();
        std::size_t wood = 0, looked = 0;
        for (std::uint32_t y = 0; y < scene.shape.height; ++y)
            for (std::uint32_t x = 0; x < 60; ++x)
                if (const auto owner = scene.world->bodies().owner(x, y); owner && *owner == all[0].body) {
                    ++looked;
                    const auto expected = scene.view.color(scene.world->cells(), x, y);
                    const raster::Rgba8 pixel = last.at(static_cast<int>(x) * scale + scale / 2, static_cast<int>(y) * scale + scale / 2);
                    wood += std::abs(pixel.r - expected.r * 255) < 24 && std::abs(pixel.g - expected.g * 255) < 24 && std::abs(pixel.b - expected.b * 255) < 24;
                }
        expect(looked > 40u && wood * 10 >= looked * 9) << std::format("{} of the crate's {} cells drawn as wood", wood, looked);
        // Again from the start: the same pictures.
        Scene again;
        std::vector<std::uint64_t> second;
        bool seen_flight = false;
        for (int frame = 0; frame <= 240; ++frame) {
            if (frame) again.world->step();
            const bool keep_it = frame == 0 || frame == 30 || frame == 90 || frame == 240;
            const bool look_at_flight = !seen_flight && again.world->flying().flying() > 10;
            if (!keep_it && !look_at_flight) continue;
            second.push_back(hash_of(again.frame()));
            if (look_at_flight) seen_flight = true;
        }
        expect(hashes == second) << "the same frames, the same pixels";
    };
}};
