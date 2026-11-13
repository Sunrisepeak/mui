// A world of matter on screen by a ladder (xengine.cellular.present's feature `world`; D-178): rich, lit, baked,
// plain. The world is the same bits whatever step it is shown at and however the step changes while it runs;
// the steps differ where they should (scenery alight only at rich, a glow at rich and lit, baked flames over
// what emits at baked, none at plain); what a step is doing is said; the baked frames are the rich form's own,
// the same bits each time. Each step's picture is kept as PNG, and what each costs to draw is printed.
import std;
import xengine.matter.json;
import xengine.cellular.present.world;
import mlibs.image;
import mlibs.json;
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
const matter::Look& look() {
    static const matter::Look value = *matter::standard_look(standard());
    return value;
}
std::uint32_t material(std::string_view name) { return *standard().material(name); }

// A cavern with a fire of its own: a floor, a post of wood alight at its foot, a crate afloat in a pool.
std::unique_ptr<bodies::MatterWorld> cavern() {
    auto world = *bodies::MatterWorld::make(standard(), {.shape = {96, 56}, .tile = {16, 16}, .seed = 5, .fill = "air"});
    world->place({0, 50, 96, 6}, material("stone"));
    world->place({20, 36, 3, 14}, material("wood"));
    world->set(*standard().channel("temperature"), {20, 48, 3, 2}, 1000 * 256);
    world->place({56, 42, 2, 8}, material("stone")), world->place({88, 42, 2, 8}, material("stone"));
    world->place({58, 44, 30, 6}, material("water"));
    world->place({68, 38, 5, 4}, material("wood"));
    (void)world->carve({68, 38, 5, 4});
    return world;
}
// What a flame is baked from: a post alight on a floor, alone.
std::unique_ptr<bodies::MatterWorld> hearth() {
    auto world = *bodies::MatterWorld::make(standard(), {.shape = {24, 24}, .tile = {8, 8}, .seed = 2, .fill = "air"});
    world->place({0, 21, 24, 3}, material("stone"));
    world->place({10, 13, 4, 8}, material("wood"));
    world->set(*standard().channel("temperature"), {10, 19, 4, 2}, 1000 * 256);
    return world;
}
struct Shown {
    xrender::ImageStore images;
    ca::WorldPicture picture;
    xrender::DisplayList list;
    xrender::CpuRenderer renderer;
    explicit Shown(ca::PictureStep richest = ca::PictureStep::rich)
        : picture(*ca::WorldPicture::make(images, standard(), look(), {96, 56}, {.fill = "air", .seed = 9, .richest = richest})) {
        picture.scenery({23, 46, 10, 4}, material("wood"));  // a hedge beside the post
        picture.scenery({40, 49, 12, 1}, material("wood"));  // and dry grass along the floor
    }
    raster::Surface frame(const bodies::MatterWorld& world) {
        picture.show(world);
        const float width = 96.0f * scale, height = 56.0f * scale;
        list.clear();
        list.fill_rect({0, 0, width, height}, geom::Color::hex(0x0d1117));
        picture.draw(list, {0, 0, width, height});
        raster::Surface out(96 * scale, 56 * scale);
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
    static const std::filesystem::path directory = scratch_dir("matter-world-ladder");
    const auto file = directory / std::format("{}.png", name);
    const mlibs::image::View view{reinterpret_cast<const std::uint8_t*>(picture.pixels().data()), picture.width(), picture.height(), picture.width() * 4};
    expect(mlibs::image::save_png(file, view).has_value()) << file.generic_string();
    return file;
}
// How bright a region of a picture is: the sum of its pixels' channels.
std::uint64_t brightness(const raster::Surface& picture, int x0, int y0, int x1, int y1) {
    std::uint64_t sum = 0;
    for (int y = y0 * scale; y < y1 * scale; ++y)
        for (int x = x0 * scale; x < x1 * scale; ++x) {
            const raster::Rgba8 pixel = picture.at(x, y);
            sum += pixel.r + pixel.g + pixel.b;
        }
    return sum;
}
std::string field(const std::string& described, std::string_view name) {
    const auto parsed = mlibs::json::Document::parse(described, "described");
    if (!parsed) return "?";
    const auto node = parsed->root()[name];
    if (const auto text = node.text()) return std::string(*text);
    if (const auto flag = node.boolean()) return *flag ? "true" : "false";
    if (const auto number = node.integer()) return std::to_string(*number);
    return "?";
}
}  // namespace

UTest cases{"matter.world.ladder", [] {
    "the world is the same bits at every step of the ladder, and while the step changes under it"_test = [] {
        const auto alone = cavern();
        std::vector<std::uint64_t> expected;
        for (int frame = 0; frame < 360; ++frame) alone->step(), expected.push_back(alone->state_hash());
        for (const ca::PictureStep step : {ca::PictureStep::rich, ca::PictureStep::lit, ca::PictureStep::baked, ca::PictureStep::plain}) {
            const auto world = cavern();
            Shown shown;
            expect(shown.picture.bake(*hearth()).has_value());
            shown.picture.set_step(step, "the test's");
            for (int frame = 0; frame < 360; ++frame) {
                world->step();
                if (frame % 30 == 0) (void)shown.frame(*world);
                else shown.picture.show(*world);
                expect(fatal(eq(world->state_hash(), expected[static_cast<std::size_t>(frame)]))) << std::format("shown {}: frame {}", ca::picture_step_names[static_cast<std::size_t>(step)], frame);
            }
        }
        // And a governor's kind of afternoon: down and up the ladder every few frames.
        const auto world = cavern();
        Shown shown;
        expect(shown.picture.bake(*hearth()).has_value());
        for (int frame = 0; frame < 360; ++frame) {
            world->step();
            shown.picture.set_step(static_cast<ca::PictureStep>((frame / 17) % 4), "up and down");
            shown.picture.show(*world);
            expect(fatal(eq(world->state_hash(), expected[static_cast<std::size_t>(frame)]))) << std::format("frame {}", frame);
        }
    };

    "the steps differ where they should: scenery alight only at rich, a glow at rich and lit, baked flames at baked, none at plain; each says what it is doing"_test = [] {
        std::array<raster::Surface, 4> pictures;
        std::array<std::string, 4> said;
        std::array<double, 4> cost{};
        for (std::size_t index = 0; index < 4; ++index) {
            const auto world = cavern();
            Shown shown;
            expect(fatal(shown.picture.bake(*hearth()).has_value()));
            shown.picture.set_step(static_cast<ca::PictureStep>(index), "the test's");
            for (int frame = 0; frame < 420; ++frame) world->step(), shown.picture.show(*world);
            pictures[index] = shown.frame(*world);
            said[index] = shown.picture.describe();
            std::println("kept {}  {}", keep(pictures[index], ca::picture_step_names[index]).generic_string(), said[index]);
            // What a frame of this step costs: the step's own work and its drawing, the world's step apart.
            const auto began = std::chrono::steady_clock::now();
            for (int frame = 0; frame < 20; ++frame) (void)shown.frame(*world);
            cost[index] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count() / 20;
        }
        std::println("a frame shown and drawn on the CPU, 576x336: rich {:.2f} ms, lit {:.2f} ms, baked {:.2f} ms, plain {:.2f} ms", cost[0], cost[1], cost[2], cost[3]);
        const std::array<std::string_view, 4> layer{"stepped", "still", "still", "still"}, glow{"true", "true", "false", "false"};
        for (std::size_t index = 0; index < 4; ++index) {
            expect(eq(field(said[index], "step"), std::string(ca::picture_step_names[index]))) << said[index];
            expect(eq(field(said[index], "layer"), std::string(layer[index])) && eq(field(said[index], "glow"), std::string(glow[index]))) << said[index];
            expect(eq(field(said[index], "why"), std::string("the test's")));
        }
        // The hedge: alight (or burnt away) only where the layer is stepped — there the layer's cells are not the world's.
        expect(std::stoi(field(said[0], "differing")) > 40) << said[0];
        // Below rich the scenery is as placed — 40 cells of hedge, 12 of grass — but for where the world has put its own (ash on the grass).
        const int still = std::stoi(field(said[1], "differing"));
        expect(still >= 46 && still <= 52) << said[1];
        // Light about the world's own post: the glow adds to it, a baked flame too; the plain picture has the
        // shade alone.
        const std::array<std::uint64_t, 4> above{brightness(pictures[0], 12, 34, 30, 50), brightness(pictures[1], 12, 34, 30, 50), brightness(pictures[2], 12, 34, 30, 50),
                                                 brightness(pictures[3], 12, 34, 30, 50)};
        std::println("light about the post: rich {}, lit {}, baked {}, plain {}", above[0], above[1], above[2], above[3]);
        expect(above[1] > above[3] + above[3] / 20) << "lit glows where plain does not";
        expect(above[2] > above[3] + above[3] / 20) << "baked has its flame there";
        expect(std::stoi(field(said[2], "flames")) > 0 && eq(field(said[3], "flames"), std::string("0")));
        // Away from any fire the steps below rich agree to the pixel (the pool's far end).
        expect(eq(brightness(pictures[1], 80, 20, 94, 40), brightness(pictures[3], 80, 20, 94, 40)));
        // Cheaper going down: the plain step costs less than the lit, the lit less than the rich.
        expect(cost[3] < cost[1] && cost[1] < cost[0]) << std::format("rich {:.2f}, lit {:.2f}, baked {:.2f}, plain {:.2f} ms", cost[0], cost[1], cost[2], cost[3]);
        expect(cost[2] < cost[1]) << std::format("baked {:.2f} ms against lit {:.2f} ms", cost[2], cost[1]);
    };

    "a device shows no richer than it can; the baked frames are the rich form's own, the same bits each time; nothing emitting is refused"_test = [] {
        Shown modest(ca::PictureStep::baked);
        expect(modest.picture.step() == ca::PictureStep::baked);
        modest.picture.set_step(ca::PictureStep::rich, "room in the frame");
        expect(modest.picture.step() == ca::PictureStep::baked);
        expect(field(modest.picture.describe(), "why").contains("this device shows baked at most")) << modest.picture.describe();
        modest.picture.set_step(ca::PictureStep::plain, "frames over their budget");
        expect(modest.picture.step() == ca::PictureStep::plain);
        // Baked twice: the same key; from a scene with nothing alight: refused, and what was baked stays.
        Shown one, other;
        expect(fatal(one.picture.bake(*hearth()).has_value() && other.picture.bake(*hearth()).has_value()));
        expect(eq(one.picture.flames().key, other.picture.flames().key));
        expect(eq(one.picture.flames().frames.size(), std::size_t{12}));
        auto cold = *bodies::MatterWorld::make(standard(), {.shape = {24, 24}, .tile = {8, 8}, .fill = "air"});
        cold->place({0, 21, 24, 3}, material("stone"));
        const auto refused = one.picture.bake(*cold);
        expect(!refused && refused.error().contains("emitted")) << (refused ? "" : refused.error());
        expect(eq(one.picture.flames().frames.size(), std::size_t{12}));
        // The scene given to bake is not stepped.
        const auto scene = hearth();
        const std::uint64_t before = scene->state_hash();
        expect(one.picture.bake(*scene).has_value());
        expect(eq(scene->state_hash(), before));
    };
}};
