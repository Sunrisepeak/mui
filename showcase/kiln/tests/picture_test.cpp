// Kiln drawn headless, by the ladders (D-178): Mui lights the barricade; where the picture has room the level's
// scenery — dry roots by the barricade, brush on the ledge — catches from that fire and burns, with a glow about
// it; lower, the scenery is as placed and the light is a glow, then baked flames, then the shade alone. The
// game is the same game at every step: its checksum, frame by frame.
import std;
import omui.showcase.kiln.picture;
import mlibs.image;
import mlibs.json;
import mlibs.hash;
import mlibs.cts;
import mcpplibs.mtest;

// mtest by name: its `cts` is not mlibs'.
using mcpplibs::mtest::UTest, mcpplibs::mtest::expect, mcpplibs::mtest::eq, mcpplibs::mtest::fatal, mcpplibs::mtest::operator""_test;
namespace ca = xengine::cellular;
namespace raster = xrender::raster;

namespace {
constexpr int scale = 6;
kiln::Input walking(float axis) {
    kiln::Input input;
    input.player.axes[0] = static_cast<std::int16_t>(axis * 32767);
    return input;
}
kiln::Input casting_at(const kiln::State& state, float x, float y) {
    kiln::Input input;
    input.player.pressed = 1u << kiln::cast_button;
    input.player.held = input.player.pressed;
    input.player.pointer_x = static_cast<std::int16_t>(x / static_cast<float>(state.world->shape().width) * 32767);
    input.player.pointer_y = static_cast<std::int16_t>(y / static_cast<float>(state.world->shape().height) * 32767);
    return input;
}
// The play: up to the barricade, back from it, fire at it, and wait.
kiln::Input play(const kiln::State& state, int frame) {
    if (frame < 120) return walking(1);
    if (frame < 160) return walking(-1);
    if (frame == 160) return casting_at(state, 26.5f, 26.0f);
    return {};
}
// Kiln's picture under the presentation contract (mlibs.cts:world): the game — the play above — is what is
// shown, its checksum the hash.
struct Game {
    using State = kiln::State;
    std::unique_ptr<State> make() const { return std::make_unique<State>(*kiln::start(kiln::level::first())); }
    void step(State& state) const { kiln::step(state, play(state, static_cast<int>(state.frame) + 100)); }  // the fire lit at its 60th frame
    std::uint64_t hash(const State& state) const { return kiln::checksum(state); }
};
struct Pictured {
    xrender::ImageStore images;
    kiln::Picture picture;
    Pictured() : picture(*kiln::Picture::make(images, kiln::level::first())) {}
};
struct KilnPicture {
    using Worlds = Game;
    using Shown = Pictured;
    Game game;
    Game& worlds() { return game; }
    std::unique_ptr<Shown> make() const { return std::make_unique<Shown>(); }
    auto ladders(const Shown&) const { return ca::WorldPicture::ladders(); }
    void set(Shown& shown, std::size_t ladder, std::uint8_t lowered, std::string why) const { shown.picture.world().set(ladder, lowered, std::move(why)); }
    void show(Shown& shown, const kiln::State& state) const { shown.picture.show(state); }
    std::string describe(const Shown& shown) const { return shown.picture.world().describe(); }
};
struct Shown {
    xrender::ImageStore images;
    kiln::Picture picture;
    xrender::DisplayList list;
    xrender::CpuRenderer renderer;
    Shown(ca::Light light, bool alive) : picture(*kiln::Picture::make(images, kiln::level::first())) {
        picture.world().set_light(light, "the test's"), picture.world().set_scenery(alive, "the test's");
    }
    raster::Surface frame(const kiln::State& state) {
        picture.show(state);
        list.clear();
        picture.draw(list, {0, 0, 128.0f * scale, 64.0f * scale});
        raster::Surface out(128 * scale, 64 * scale);
        (void)renderer.render(list, out, {&images, nullptr});
        return out;
    }
};
std::filesystem::path keep(const raster::Surface& picture, std::string_view name) {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "openmui-tests" / "kiln-picture";
    std::filesystem::create_directories(directory);
    const auto file = directory / std::format("{}.png", name);
    const mlibs::image::View view{reinterpret_cast<const std::uint8_t*>(picture.pixels().data()), picture.width(), picture.height(), picture.width() * 4};
    expect(mlibs::image::save_png(file, view).has_value()) << file.generic_string();
    return file;
}
std::uint64_t brightness(const raster::Surface& picture, int x0, int y0, int x1, int y1) {
    std::uint64_t sum = 0;
    for (int y = y0 * scale; y < y1 * scale; ++y)
        for (int x = x0 * scale; x < x1 * scale; ++x) {
            const raster::Rgba8 pixel = picture.at(x, y);
            sum += pixel.r + pixel.g + pixel.b;
        }
    return sum;
}
std::int64_t number(const std::string& described, std::string_view name) {
    const auto parsed = mlibs::json::Document::parse(described, "described");
    return parsed ? parsed->root()[name].integer().value_or(-1) : -1;
}
// Where the ladders stand in the four pictures kept.
struct Standing {
    std::string_view name;
    ca::Light light;
    bool alive;
};
constexpr std::array<Standing, 4> standings{{{"rich", ca::Light::glow, true}, {"lit", ca::Light::glow, false}, {"baked", ca::Light::baked, false}, {"plain", ca::Light::none, false}}};
}  // namespace

UTest cases{"kiln.picture", [] {
    // 140 frames: the barricade is lit at the 60th and burns for the rest.
    mlibs::cts::against({.subject = "kiln-picture"}, [] { mlibs::cts::world::declare_presentation(KilnPicture{}, "xengine.presentation@1", {.frames = 140, .at = 70}); });
    "Mui lights the barricade: wherever the ladders stand the game is the same game, and the picture what they say"_test = [] {
        // The game alone.
        std::vector<std::uint64_t> expected;
        {
            auto state = *kiln::start(kiln::level::first());
            for (int frame = 0; frame < 520; ++frame) kiln::step(state, play(state, frame)), expected.push_back(kiln::checksum(state));
        }
        std::array<raster::Surface, 4> pictures;
        std::array<std::string, 4> said;
        for (std::size_t index = 0; index < 4; ++index) {
            auto state = *kiln::start(kiln::level::first());
            Shown shown(standings[index].light, standings[index].alive);
            for (int frame = 0; frame < 520; ++frame) {
                kiln::step(state, play(state, frame));
                shown.picture.show(state);
                expect(fatal(eq(kiln::checksum(state), expected[static_cast<std::size_t>(frame)]))) << std::format("shown {}: frame {}", standings[index].name, frame);
            }
            pictures[index] = shown.frame(state);
            said[index] = shown.picture.world().describe();
            std::println("kept {}  {}", keep(pictures[index], standings[index].name).generic_string(), said[index]);
        }
        // Rich: the scenery is not as it was placed — roots and brush alight or burnt. Below it: as placed.
        const std::int64_t placed = number(said[1], "differing");
        expect(placed > 40) << said[1];
        expect(number(said[0], "differing") != placed) << std::format("rich {} against placed {}", number(said[0], "differing"), placed);
        // Light about the barricade: more with a glow than without; the baked step has its flames.
        const auto about = [&](std::size_t index) { return brightness(pictures[index], 18, 16, 36, 30); };
        std::println("light about the barricade: rich {}, lit {}, baked {}, plain {}", about(0), about(1), about(2), about(3));
        expect(about(1) > about(3) && about(2) > about(3) && about(0) > about(3));
        expect(number(said[2], "flames") > 0) << said[2];
        // Mui is in every picture where she stands: her coat's dark blue, her eye.
        for (std::size_t index = 0; index < 4; ++index) {
            auto state = *kiln::start(kiln::level::first());
            Shown shown(standings[index].light, standings[index].alive);
            const auto picture = shown.frame(state);
            const int px = static_cast<int>((state.cat.x - 1.5f) * scale) + scale / 4, py = static_cast<int>((state.cat.y - 5.0f) * scale) + scale * 3 / 4;
            const raster::Rgba8 coat = picture.at(px, py);
            expect(coat.r == 0x2b && coat.g == 0x2d && coat.b == 0x42) << std::format("{}: {:02x}{:02x}{:02x} at her ear", standings[index].name, coat.r, coat.g, coat.b);
        }
    };
}};
