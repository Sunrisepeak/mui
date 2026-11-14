// A world of matter on screen by ladders (xengine.cellular.present's feature `world`; D-178): the light (glow,
// baked, none) and the scenery (alive, still). The world is the same bits wherever the ladders stand and however
// they move while it runs; the pictures differ where they should (scenery alight only where it is alive, a glow
// only at glow, baked flames over what emits at baked); where each ladder stands and why is said; the baked
// frames are the glow's own form, the same bits each time. Each picture is kept as PNG, and what each costs to
// draw is printed.
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
    explicit Shown(ca::Light best = ca::Light::glow, bool alive = true)
        : picture(*ca::WorldPicture::make(images, standard(), look(), {96, 56}, {.fill = "air", .seed = 9, .best_light = best, .scenery_alive = alive})) {
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
// Where the ladders stand in a picture of the tests': the light, and whether the scenery is alive.
struct Standing {
    std::string_view name;
    ca::Light light;
    bool alive;
};
constexpr std::array<Standing, 5> standings{{{"glow-alive", ca::Light::glow, true},
                                              {"glow-still", ca::Light::glow, false},
                                              {"baked-still", ca::Light::baked, false},
                                              {"none-still", ca::Light::none, false},
                                              {"none-alive", ca::Light::none, true}}};
}  // namespace

static UTest cases{"matter.world.ladder", [] {
    "the world is the same bits wherever the ladders stand, and while they move under it"_test = [] {
        const auto alone = cavern();
        std::vector<std::uint64_t> expected;
        for (int frame = 0; frame < 360; ++frame) alone->step(), expected.push_back(alone->state_hash());
        for (const Standing& standing : standings) {
            const auto world = cavern();
            Shown shown;
            expect(shown.picture.bake(*hearth()).has_value());
            shown.picture.set_light(standing.light, "the test's"), shown.picture.set_scenery(standing.alive, "the test's");
            for (int frame = 0; frame < 360; ++frame) {
                world->step();
                if (frame % 30 == 0) (void)shown.frame(*world);
                else shown.picture.show(*world);
                expect(fatal(eq(world->state_hash(), expected[static_cast<std::size_t>(frame)]))) << std::format("shown {}: frame {}", standing.name, frame);
            }
        }
        // And a governor's kind of afternoon: each ladder down and up every few frames, out of step with the other.
        const auto world = cavern();
        Shown shown;
        expect(shown.picture.bake(*hearth()).has_value());
        for (int frame = 0; frame < 360; ++frame) {
            world->step();
            shown.picture.set(ca::WorldPicture::light_ladder, static_cast<std::uint8_t>((frame / 17) % 3), "up and down");
            shown.picture.set(ca::WorldPicture::scenery_ladder, static_cast<std::uint8_t>((frame / 29) % 2), "up and down");
            shown.picture.show(*world);
            expect(fatal(eq(world->state_hash(), expected[static_cast<std::size_t>(frame)]))) << std::format("frame {}", frame);
        }
    };

    "the pictures differ where they should: scenery alight only where it is alive, a glow only at glow, baked flames at baked; each says where it stands and why"_test = [] {
        std::array<raster::Surface, 5> pictures;
        std::array<std::string, 5> said;
        std::array<double, 5> cost{};
        for (std::size_t index = 0; index < standings.size(); ++index) {
            const auto world = cavern();
            Shown shown;
            expect(fatal(shown.picture.bake(*hearth()).has_value()));
            shown.picture.set_light(standings[index].light, "the test's"), shown.picture.set_scenery(standings[index].alive, "the test's own");
            for (int frame = 0; frame < 420; ++frame) world->step(), shown.picture.show(*world);
            pictures[index] = shown.frame(*world);
            said[index] = shown.picture.describe();
            std::println("kept {}  {}", keep(pictures[index], standings[index].name).generic_string(), said[index]);
            // What a frame costs here: the picture's own work and its drawing, the world's step apart.
            const auto began = std::chrono::steady_clock::now();
            for (int frame = 0; frame < 20; ++frame) (void)shown.frame(*world);
            cost[index] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count() / 20;
        }
        std::println("a frame shown and drawn on the CPU, 576x336: glow-alive {:.2f} ms, glow-still {:.2f}, baked-still {:.2f}, none-still {:.2f}, none-alive {:.2f}", cost[0], cost[1], cost[2],
                     cost[3], cost[4]);
        for (std::size_t index = 0; index < standings.size(); ++index) {
            expect(eq(field(said[index], "light"), std::string(ca::light_names[static_cast<std::size_t>(standings[index].light)]))) << said[index];
            expect(eq(field(said[index], "scenery"), std::string(standings[index].alive ? "alive" : "still"))) << said[index];
            expect(eq(field(said[index], "light_why"), std::string("the test's")) && eq(field(said[index], "scenery_why"), std::string("the test's own")));
        }
        // The hedge: alight (or burnt away) only where the scenery is alive — there the layer's cells are not as placed.
        // Still, the scenery is as placed — 40 cells of hedge, 12 of grass — but for where the world has put its own.
        const int still = std::stoi(field(said[1], "differing"));
        expect(still >= 46 && still <= 52) << said[1];
        expect(std::stoi(field(said[0], "differing")) != still && eq(field(said[0], "differing"), field(said[4], "differing"))) << said[0];
        expect(eq(field(said[2], "differing"), field(said[1], "differing")) && eq(field(said[3], "differing"), field(said[1], "differing")));
        // Light about the world's own post: the glow adds to it, a baked flame too; with no light, the shade alone.
        const auto about = [&](std::size_t index) { return brightness(pictures[index], 12, 34, 30, 50); };
        std::println("light about the post: glow-alive {}, glow-still {}, baked-still {}, none-still {}, none-alive {}", about(0), about(1), about(2), about(3), about(4));
        expect(about(1) > about(3) + about(3) / 20) << "a glow where none-still has none";
        expect(about(2) > about(3) + about(3) / 20) << "baked has its flame there";
        expect(std::stoi(field(said[2], "flames")) > 0 && eq(field(said[3], "flames"), std::string("0")));
        // Away from any fire the still pictures agree to the pixel (the pool's far end).
        expect(eq(brightness(pictures[1], 80, 20, 94, 40), brightness(pictures[3], 80, 20, 94, 40)));
        // The light is the dear ladder: each step down it is cheaper, by far more than the scenery's step is.
        expect(cost[3] < cost[2] && cost[2] < cost[1]) << std::format("glow {:.2f}, baked {:.2f}, none {:.2f} ms", cost[1], cost[2], cost[3]);
        expect(cost[1] - cost[2] > 4 * (cost[4] - cost[3])) << std::format("the glow costs {:.2f} ms more than baked; scenery alive costs {:.2f} ms more than still", cost[1] - cost[2], cost[4] - cost[3]);
    };

    "a device shows no richer than it can; the baked frames are the glow's own form, the same bits each time; nothing emitting is refused"_test = [] {
        Shown modest(ca::Light::baked, false);
        expect(modest.picture.light() == ca::Light::baked && !modest.picture.scenery_alive());
        modest.picture.set_light(ca::Light::glow, "room in the frame");
        modest.picture.set_scenery(true, "room in the frame");
        expect(modest.picture.light() == ca::Light::baked && !modest.picture.scenery_alive());
        expect(field(modest.picture.describe(), "light_why").contains("this device shows baked at most")) << modest.picture.describe();
        expect(field(modest.picture.describe(), "scenery_why").contains("this device shows scenery still")) << modest.picture.describe();
        modest.picture.set_light(ca::Light::none, "frames over their budget");
        expect(modest.picture.light() == ca::Light::none);
        // A governor's change lands as it is given: the light is lowered before the scenery.
        Shown governed;
        const auto ladders = ca::WorldPicture::ladders();
        mlibs::pipeline::Governor governor({ladders[0], ladders[1]});
        std::vector<std::string> order;
        for (int frame = 0; frame < 200; ++frame)
            if (const auto change = governor.frame(40.0, 16.0)) {
                governed.picture.set(change->ladder, change->lowered, "frames over their budget");
                order.push_back(std::format("{}:{}", ladders[change->ladder].name, change->lowered));
            }
        expect(order == std::vector<std::string>{"world.light:1", "world.light:2", "world.scenery:1"}) << std::format("{}", order);
        expect(governed.picture.light() == ca::Light::none && !governed.picture.scenery_alive());
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
