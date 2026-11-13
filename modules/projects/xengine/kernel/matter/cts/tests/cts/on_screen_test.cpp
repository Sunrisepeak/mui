// Matter on screen (D-168): scenes of the standard definition on the lattice, shaded by the standard
// appearance — a look that reads state and never a material — drawn by the renderer and read back as pixels.
// What is hot glows because it is hot, what burns is aflame whatever it is, what is wet is darker; a material
// no pack names looks like its phase and burns like the rest; another pack changes the picture and not a cell.
// The pictures are kept for a reviewer (the scratch directory each case prints).
import std;
import xengine.matter.json;
import xengine.cellular.matter;
import xengine.cellular.present.matter;
import mlibs.image;
import mlibs.testing;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
namespace matter = xengine::matter;
namespace ca = xengine::cellular;
namespace geom = mlibs::geom;
namespace raster = xrender::raster;

namespace {

constexpr int scale = 6;  // pixels a cell

// A world of matter on the lattice, open to air at 293 K beyond its edges, and its picture.
struct Scene {
    matter::Program program;
    ca::Program plan;
    ca::Shape shape;
    std::vector<std::uint32_t> cells, next, outside;
    std::uint64_t tick = 0;
    xrender::ImageStore images;
    ca::Shade shade;
    ca::FieldPresenter view;
    xrender::DisplayList list;

    Scene(matter::Program of, const matter::Look& look, ca::Shape size)
        : program(std::move(of)), plan(*ca::lower(program, {.seed = 7})), shape(size), cells(program.fresh(area(), *program.material("air"))), next(cells.size()),
          outside(program.fresh(1, *program.material("air"))), shade(ca::lower(look)),
          view(ca::FieldPresenter::make(images, size, static_cast<std::uint32_t>(program.channels().size()), shade, {16, 16}).value()) {}

    [[nodiscard]] std::size_t area() const { return static_cast<std::size_t>(shape.width) * shape.height; }
    [[nodiscard]] std::size_t at(std::uint32_t x, std::uint32_t y) const { return static_cast<std::size_t>(y) * shape.width + x; }
    void fill(std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1, std::string_view material) {
        for (std::uint32_t y = y0; y <= y1; ++y)
            for (std::uint32_t x = x0; x <= x1; ++x) program.place(cells, area(), at(x, y), *program.material(material));
    }
    [[nodiscard]] std::uint32_t& value(std::string_view channel, std::uint32_t x, std::uint32_t y) { return cells[*program.channel(channel) * area() + at(x, y)]; }
    [[nodiscard]] double kelvin(std::uint32_t x, std::uint32_t y) { return value("temperature", x, y) / 256.0; }
    [[nodiscard]] bool is(std::uint32_t x, std::uint32_t y, std::string_view name) const { return program.materials()[cells[at(x, y)]].name == name; }
    [[nodiscard]] std::size_t count(std::string_view name) const {
        return static_cast<std::size_t>(std::ranges::count(std::span(cells).first(area()), *program.material(name)));
    }
    void step(int count = 1) {
        for (int index = 0; index < count; ++index) {
            expect(fatal(plan.step(shape, cells, next, {.tile = {16, 16}, .boundary = ca::open_to(outside), .tick = tick++}).has_value()));
            cells.swap(next);
        }
    }
    [[nodiscard]] ca::Shaded shaded(std::uint32_t x, std::uint32_t y) const { return view.shaded(cells, x, y); }
    // The frame: a dark backdrop, the field a cell to `scale` pixels, and — asked for — its light as a glow.
    [[nodiscard]] raster::Surface frame(bool glow = true) {
        expect(view.present(cells).has_value());
        const float width = static_cast<float>(shape.width) * scale, height = static_cast<float>(shape.height) * scale;
        list.clear();
        list.fill_rect({0, 0, width, height}, geom::Color::hex(0x0d1117));
        view.draw(list, {0, 0, width, height});
        if (glow) view.draw_glow(list, {0, 0, width, height}, 2.0f * scale, 0.9f);
        raster::Surface out(static_cast<int>(shape.width) * scale, static_cast<int>(shape.height) * scale);
        xrender::CpuRenderer renderer;
        (void)renderer.render(list, out, {&images, nullptr});
        return out;
    }
    // The pixel at a cell's middle.
    [[nodiscard]] static raster::Rgba8 middle(const raster::Surface& of, std::uint32_t x, std::uint32_t y) {
        return of.at(static_cast<int>(x) * scale + scale / 2, static_cast<int>(y) * scale + scale / 2);
    }
};

[[nodiscard]] int brightness(raster::Rgba8 pixel) { return pixel.r + pixel.g + pixel.b; }
[[nodiscard]] int brightness(std::uint32_t pixel) { return static_cast<int>((pixel & 0xff) + ((pixel >> 8) & 0xff) + ((pixel >> 16) & 0xff)); }

std::filesystem::path keep(const raster::Surface& picture, std::string_view name) {
    static const std::filesystem::path directory = scratch_dir("matter-on-screen");  // made anew once a run
    const auto file = directory / std::format("{}.png", name);
    const mlibs::image::View view{reinterpret_cast<const std::uint8_t*>(picture.pixels().data()), picture.width(), picture.height(), picture.width() * 4};
    expect(mlibs::image::save_png(file, view).has_value()) << file.generic_string();
    return file;
}

matter::Program standard() { return *matter::standard(); }

}  // namespace

static UTest cases{"matter.on-screen", [] {

    "a hearth: what burns is aflame and lights what is beside it, the air over it shows its heat, sand by the pool is darker for the water in it"_test = [] {
        const matter::Program program = standard();
        const auto look = matter::standard_look(program);
        expect(fatal(look.has_value()));
        Scene scene(program, *look, {56, 32});
        scene.fill(0, 28, 55, 31, "stone");
        scene.fill(2, 25, 6, 27, "sand");       // a dry heap, far from the water
        scene.fill(12, 22, 23, 27, "wood");     // the pile
        scene.fill(30, 24, 37, 27, "sand");     // a heap against the pool
        scene.fill(38, 25, 51, 27, "water");
        scene.fill(52, 22, 53, 27, "stone");    // the pool's far wall
        const raster::Surface before = scene.frame();
        expect(eq(scene.view.shaded(scene.cells, 20, 2).pixel, 0u)) << "air is clear: the backdrop shows";
        expect(Scene::middle(before, 20, 2).r == 0x0d && Scene::middle(before, 20, 2).b == 0x17);
        const std::uint32_t wood_before = scene.shaded(14, 24).pixel;
        expect(eq(scene.shaded(14, 24).light, 0u)) << "nothing glows before the fire";
        scene.value("temperature", 12, 27) = 900 * 256;  // a light at the pile's foot
        scene.value("temperature", 13, 27) = 900 * 256;
        scene.step(420);
        const raster::Surface lit = scene.frame(), unlit = scene.frame(false);
        std::println("    a hearth after 420 ticks: {}", keep(lit, "hearth").generic_string());

        // What burns: aflame — brighter than it was, and giving light — whatever it is made of.
        const std::size_t burning = *program.channel("burning") * scene.area();
        std::size_t alight = 0, hot_air = 0;
        for (std::uint32_t y = 0; y < scene.shape.height; ++y)
            for (std::uint32_t x = 0; x < scene.shape.width; ++x) {
                const ca::Shaded shaded = scene.shaded(x, y);
                if (scene.cells[burning + scene.at(x, y)]) {
                    ++alight;
                    expect(shaded.light != 0u && brightness(shaded.pixel) > brightness(wood_before)) << std::format("the burning cell {}, {}: {:08x}", x, y, shaded.pixel);
                } else if (scene.kelvin(x, y) <= (program.phase(scene.cells[scene.at(x, y)]) == matter::Phase::gas ? 420 : 800)) {
                    // Not burning, and no hotter than where air begins to show its heat or a thing to glow.
                    expect(eq(shaded.light, 0u)) << std::format("{}, {} is not burning and at {:.0f} K: it gives no light", x, y, scene.kelvin(x, y));
                }
                if (scene.is(x, y, "air") && shaded.pixel >> 24) ++hot_air;
            }
        expect(alight >= 6u) << std::format("{} cells alight", alight);
        expect(hot_air >= 4u) << std::format("{} cells of air show their heat", hot_air);
        // The glow: light added round the fire, none far from it.
        std::size_t brighter = 0, darker = 0;
        for (int y = 0; y < lit.height(); ++y)
            for (int x = 0; x < lit.width(); ++x) {
                if (brightness(lit.at(x, y)) > brightness(unlit.at(x, y))) ++brighter;
                if (brightness(lit.at(x, y)) < brightness(unlit.at(x, y))) ++darker;
            }
        expect(brighter > alight * scale * scale && eq(darker, std::size_t{0})) << std::format("{} pixels brighter for the glow, {} darker", brighter, darker);
        expect(Scene::middle(lit, 48, 4) == Scene::middle(unlit, 48, 4)) << "far from the fire nothing is lit";

        // Wet sand is darker: the heap against the pool has taken up water, the far heap is dry. By the mean of
        // each (a grain is lighter or darker by its place).
        const std::size_t sand = *program.material("sand");
        double wet_sum = 0, dry_sum = 0;
        std::size_t wet_cells = 0, dry_cells = 0;
        for (std::uint32_t y = 0; y < scene.shape.height; ++y)
            for (std::uint32_t x = 0; x < scene.shape.width; ++x) {
                if (scene.cells[scene.at(x, y)] != sand) continue;
                const bool wet = scene.value("wetness", x, y) >= 16;
                (wet ? wet_sum : dry_sum) += brightness(scene.shaded(x, y).pixel);
                ++(wet ? wet_cells : dry_cells);
            }
        expect(fatal(wet_cells >= 4u && dry_cells >= 4u)) << std::format("{} wet and {} dry cells of sand", wet_cells, dry_cells);
        expect(wet_sum / static_cast<double>(wet_cells) < dry_sum / static_cast<double>(dry_cells) - 40)
            << std::format("wet sand {:.0f}, dry sand {:.0f}", wet_sum / static_cast<double>(wet_cells), dry_sum / static_cast<double>(dry_cells));
        std::println("    {} cells alight, {} of air showing heat, {} pixels lit by the glow; sand: wet {:.0f}, dry {:.0f} (of 765)", alight, hot_air, brighter,
                     wet_sum / static_cast<double>(wet_cells), dry_sum / static_cast<double>(dry_cells));

        // The same ticks, the same picture.
        Scene again(program, *look, {56, 32});
        again.cells = scene.cells;
        expect(again.frame().hash() == lit.hash());
    };

    "lava glows of its own heat — no state names it — and where water has set it as stone and cooled it, it does not"_test = [] {
        const matter::Program program = standard();
        const auto look = matter::standard_look(program);
        expect(fatal(look.has_value()));
        expect(std::ranges::none_of(look->states(), [](const matter::StateLook& state) { return state.name.contains("lava"); }));
        Scene scene(program, *look, {40, 24});
        scene.fill(0, 22, 39, 23, "stone");
        scene.fill(0, 12, 1, 21, "stone");
        scene.fill(38, 12, 39, 21, "stone");
        scene.fill(2, 18, 37, 21, "lava");
        scene.fill(22, 14, 37, 17, "water");   // over the right half
        scene.fill(21, 14, 21, 17, "stone");   // held there by a wall
        const raster::Surface poured = scene.frame();
        std::println("    lava and water, poured: {}", keep(poured, "lava-0").generic_string());
        const ca::Shaded lava = scene.shaded(8, 20);
        expect(lava.light != 0u && brightness(lava.pixel) > brightness(look->base()[*program.material("lava")])) << std::format("{:08x}", lava.pixel);
        scene.step(40);
        std::println("    after 40 ticks: {}", keep(scene.frame(), "lava-40").generic_string());
        scene.step(460);
        const raster::Surface after = scene.frame();
        std::println("    after 500 ticks: {}", keep(after, "lava-500").generic_string());
        std::size_t set_and_dark = 0, molten_and_lit = 0, steam = scene.count("steam");
        for (std::uint32_t y = 12; y < 22; ++y)
            for (std::uint32_t x = 2; x < 38; ++x) {
                const ca::Shaded shaded = scene.shaded(x, y);
                if (scene.is(x, y, "stone") && scene.kelvin(x, y) < 800 && !(x == 21 && y < 18)) {
                    expect(eq(shaded.light, 0u)) << std::format("stone at {:.0f} K gives no light", scene.kelvin(x, y));
                    ++set_and_dark;
                }
                if (scene.is(x, y, "lava")) {
                    expect(shaded.light != 0u) << "what is still lava is above 1300 K: it glows";
                    ++molten_and_lit;
                }
            }
        expect(set_and_dark >= 8u) << std::format("{} cells set as stone and cooled", set_and_dark);
        std::println("    {} cells set and dark, {} still molten and glowing, {} of steam", set_and_dark, molten_and_lit, steam);
    };

    "a material no pack names looks like its phase, and burns with the same flame; another pack changes the picture and not a cell"_test = [] {
        using P = matter::Property;
        auto read = matter::read(matter::standard_snapshot, "standard.matter.omui.json");
        expect(fatal(read.has_value()));
        read->materials.push_back({"peat", {P{"phase", "granular"}, P{"density", 500.0}, P{"thermal", 1.0}, P{"ignition", 520.0}, P{"fuel", 300.0}, P{"heat", 70.0}, P{"burnt", "ash"}}, {}});
        const auto prepared = matter::Program::prepare(*read);
        expect(fatal(prepared.has_value()));
        const matter::Program& program = *prepared;
        const auto look = matter::standard_look(program);
        expect(fatal(look.has_value()));
        const matter::MaterialId peat = *program.material("peat");
        expect(!look->named(peat));
        Scene scene(program, *look, {32, 20});
        scene.fill(0, 18, 31, 19, "stone");
        scene.fill(6, 14, 25, 17, "peat");
        scene.step(10);  // it settles
        const std::uint32_t granular = look->base()[peat];
        expect(eq(granular, 0xff77a3b9u));
        // On screen it is its phase's colour, lighter or darker by its grain (41 of 255).
        std::size_t seen = 0;
        for (std::uint32_t y = 0; y < 18; ++y)
            for (std::uint32_t x = 0; x < 32; ++x) {
                if (scene.cells[scene.at(x, y)] != peat) continue;
                ++seen;
                const std::uint32_t pixel = scene.shaded(x, y).pixel;
                expect(std::abs(static_cast<int>(pixel & 0xff) - 0xb9) <= 41 && std::abs(static_cast<int>((pixel >> 8) & 0xff) - 0xa3) <= 41) << std::format("{:08x}", pixel);
            }
        expect(eq(seen, std::size_t{80}));
        const auto row_of = [&] {
            for (std::uint32_t y = 0; y < 18; ++y)
                if (scene.cells[scene.at(8, y)] == peat) return y;
            return 17u;
        };
        scene.value("temperature", 8, row_of()) = 900 * 256;
        scene.step(120);
        const std::size_t burning = *program.channel("burning") * scene.area();
        std::size_t alight = 0;
        for (std::size_t cell = 0; cell < scene.area(); ++cell)
            if (scene.cells[burning + cell] && scene.cells[cell] == peat) {
                ++alight;
                expect(ca::shade_of(scene.shade, scene.cells, scene.area(), cell).light != 0u) << "peat alight gives light: no one wrote how peat burns";
            }
        expect(alight >= 2u) << std::format("{} cells of peat alight", alight);
        const raster::Surface by_standard = scene.frame();
        std::println("    peat, a row of properties, alight ({} cells): {}", alight, keep(by_standard, "peat").generic_string());

        // Another pack: everything a shade of green on black, heat as white. The cells are not touched.
        const std::string night = R"({"appearance": {
            "phases": {"solid": {"color": "#0a3d1c"}, "soft": {"color": "#0d4d24"}, "granular": {"color": "#12692f", "grain": 0.05},
                       "liquid": {"color": "#18a04680"}, "gas": {"color": "#00000000"}},
            "states": [{"name": "heat", "by": "temperature", "from": 400, "to": 1100, "color": "#e8ffe8", "emits": true},
                       {"name": "aflame", "by": "burning", "to": 1, "color": "#ffffff", "emits": true}]}})";
        const auto other = matter::look_document(program, night, "night.appearance.omui.json");
        expect(fatal(other.has_value())) << (other ? "" : other.error().front().text());
        const std::vector<std::uint32_t> cells_before = scene.cells;
        expect(scene.view.reshade(ca::lower(*other)).has_value());
        scene.shade = ca::lower(*other);
        const raster::Surface by_night = scene.frame();
        std::println("    the same cells by another pack: {}", keep(by_night, "peat-night").generic_string());
        expect(scene.cells == cells_before) << "a look changes no cell";
        expect(by_night.hash() != by_standard.hash());
        std::size_t green = 0;
        for (std::uint32_t x = 0; x < 32; ++x) {
            const auto floor = Scene::middle(by_night, x, 19);
            if (floor.g > floor.r && floor.g > floor.b) ++green;
        }
        expect(green >= 28u) << std::format("{} of the floor's 32 cells are the night pack's green", green);
    };

    "gunpowder goes off as a flash: its pressure is light for the ticks it lasts, and the picture is dark again after"_test = [] {
        const matter::Program program = standard();
        const auto look = matter::standard_look(program);
        expect(fatal(look.has_value()));
        Scene scene(program, *look, {48, 20});
        scene.fill(0, 18, 47, 19, "stone");
        scene.fill(10, 17, 37, 17, "gunpowder");
        scene.value("temperature", 10, 17) = 700 * 256;
        const auto lit_cells = [&] {
            const raster::Surface* light = scene.images.get(scene.view.light());
            return static_cast<std::size_t>(std::ranges::count_if(light->pixels(), [](raster::Rgba8 pixel) { return pixel.a != 0; }));
        };
        std::size_t most = 0;
        int brightest_tick = 0;
        std::optional<raster::Surface> flash;
        for (int tick = 1; tick <= 400; ++tick) {
            scene.step();
            if (tick > 60 && most == 0) break;
            if (tick % 2) continue;
            raster::Surface picture = scene.frame();
            const std::size_t lit = lit_cells();
            if (lit > most) {
                most = lit;
                brightest_tick = tick;
                flash.emplace(std::move(picture));
            }
        }
        expect(fatal(flash.has_value() && most >= 30u)) << std::format("at most {} cells lit", most);
        std::println("    the flash at tick {} ({} cells lit): {}", brightest_tick, most, keep(*flash, "blast").generic_string());
        expect(eq(scene.count("gunpowder"), std::size_t{0})) << "all of it went";
        scene.step(600);
        (void)scene.frame();
        expect(lit_cells() < most / 4) << std::format("{} cells still lit 600 ticks after", lit_cells());
    };
}};
