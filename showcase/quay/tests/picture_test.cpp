// Quay's picture, the CPU reference: the harbour from where the watchkeeper starts — the lamps and their glow, the
// containers ahead, the beacon far off; then a game played by the guide, drawn as it goes: the wardens the picture
// draws are the wardens a person is shown (one tree, two views — the pixels' owners and the person's view agree
// frame by frame), a shot of the watchkeeper's shows its flash, and the beacon worked to the end burns brighter
// than the display.
import std;
import omui.showcase.quay.picture;
import omui.showcase.quay.headless;
import mlibs.hash;
import mlibs.image;
import mlibs.json;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace picture = quay::picture;
namespace math = mlibs::math;

namespace {

std::filesystem::path keep(const picture::Picture& drawn, std::string_view name) {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "openmui-tests" / "quay";
    std::filesystem::create_directories(directory);
    const auto file = directory / std::format("{}.png", name);
    const auto& targets = drawn.targets();
    const mlibs::image::View view{drawn.shown().data(), targets.width, targets.height, targets.width * 4, mlibs::image::Alpha::straight};
    expect(mlibs::image::save_png(file, view).has_value()) << file.generic_string();
    return file;
}
std::uint64_t bits(std::span<const std::uint8_t> shown) {
    mlibs::hash::Fnv1a hash;
    for (const std::uint8_t byte : shown) hash.le(byte, 1);
    return hash.value();
}
// Pixels drawn by instances with ids in [from, to).
std::size_t drawn_by(const picture::Picture& drawn, xrender::scene::Id from, xrender::scene::Id to) {
    std::size_t count = 0;
    for (std::size_t pixel = 0; pixel < drawn.targets().ids.size(); ++pixel) {
        const auto id = drawn.instance_at(pixel);
        count += id >= from && id < to;
    }
    return count;
}

}  // namespace

UTest cases{"quay.picture", [] {
    "the harbour from where the watchkeeper starts: the containers ahead, the lamps brighter than the display and glowing, the beacon far off"_test = [] {
        const quay::State state = quay::start(quay::level::harbour());
        auto drawn = picture::Picture::make(quay::level::harbour());
        expect(fatal(drawn.has_value())) << (drawn ? "" : drawn.error());
        drawn->draw(state);
        const auto& targets = drawn->targets();
        const std::size_t boxes = drawn_by(*drawn, picture::box_ids, picture::crate_ids), lamps = drawn_by(*drawn, picture::lamp_ids, picture::beacon_ids),
                          beacon = drawn_by(*drawn, picture::beacon_ids, picture::gun_id), gun = drawn_by(*drawn, picture::gun_id, picture::gun_id + 1);
        const auto file = keep(*drawn, "quay-start");
        // The README's picture (omui-devtools media copies readme-quay/ into media/).
        const auto readme = std::filesystem::temp_directory_path() / "openmui-tests" / "readme-quay";
        std::filesystem::create_directories(readme);
        std::filesystem::copy_file(file, readme / "harbour.png", std::filesystem::copy_options::overwrite_existing);
        std::println("from the start, {} × {}: {} pixels of the quay's boxes, {} of lamps, {} of the beacon, {} of the gun; {} brighter than the display; bits {:016x}; {}", targets.width,
                     targets.height, boxes, lamps, beacon, gun, drawn->bright(), bits(drawn->shown()), file.generic_string());
        expect(gt(boxes, std::size_t{20000})) << "the quay and the containers fill the view";
        expect(gt(lamps, std::size_t{20})) << "lamps on their posts";
        expect(gt(beacon, std::size_t{0})) << "the beacon on its mast, 77 m off, above the containers";
        expect(gt(gun, std::size_t{200})) << "the watchkeeper's gun, low and right";
        expect(gt(drawn->bright(), 10u)) << "the globes are brighter than white";
        // The same state drawn again: the same bits.
        const std::uint64_t first = bits(drawn->shown());
        drawn->draw(state);
        expect(eq(bits(drawn->shown()), first));
    };

    "the second night: a searchlight on its mast lights the quay where its beam falls, and goes dark when shot out; a fuse box's panel is red until it is on"_test = [] {
        quay::Level night = quay::level::second_night();
        night.start = {6, 0, 2}, night.facing = 1.5707964f;  // looking down the quay toward the stacked container's mast
        quay::State state = quay::start(night);
        for (int frame = 0; frame < 90; ++frame) quay::step(state, {});
        auto drawn = picture::Picture::make(night);
        expect(fatal(drawn.has_value())) << (drawn ? "" : drawn.error());
        drawn->draw(state);
        const std::size_t lamp = drawn_by(*drawn, picture::lookout_ids, picture::fuse_ids), panels = drawn_by(*drawn, picture::fuse_ids, picture::fuse_ids + 8);
        const auto light_of = [](std::span<const std::uint8_t> shown) {
            std::uint64_t sum = 0;
            for (std::size_t at = 0; at + 3 < shown.size(); at += 4) sum += shown[at] + shown[at + 1] + shown[at + 2];
            return sum;
        };
        const std::uint64_t with_beam = light_of(drawn->shown());
        const auto file = keep(*drawn, "quay-searchlight");
        // The same frame with the lamp shot out.
        quay::State dark = state;
        for (quay::Searchlight& light : dark.lookouts) light.health = 0;
        drawn->draw(dark);
        const std::uint64_t without = light_of(drawn->shown());
        std::println("the second night: {} pixels of a searchlight and its mast, {} of fuse panels; the picture's light {} with the beams, {} with them out ({:+.1f}%); {}", lamp, panels, with_beam,
                     without, 100.0 * (static_cast<double>(with_beam) - static_cast<double>(without)) / static_cast<double>(without), file.generic_string());
        expect(gt(lamp, std::size_t{20})) << "the mast and its lamp over the stacked container";
        expect(gt(static_cast<double>(with_beam), static_cast<double>(without) * 1.01)) << "the beam lights the quay where it falls";
        // Before the fuse box at the quay's far corner: its panel red, then — switched on — green.
        quay::Level corner = night;
        corner.start = {23, 0, 11.5f};
        quay::State before = quay::start(corner);
        auto near = picture::Picture::make(corner);
        expect(fatal(near.has_value()));
        const auto panel_colour = [&](const quay::State& seen) {
            near->draw(seen);
            std::array<double, 3> sum{};
            std::size_t count = 0;
            for (std::size_t pixel = 0; pixel < near->targets().ids.size(); ++pixel) {
                const auto id = near->instance_at(pixel);
                if (id < picture::fuse_ids || id >= picture::fuse_ids + 8 || id % 2 == 0) continue;
                for (std::size_t part = 0; part < 3; ++part) sum[part] += near->shown()[pixel * 4 + part];
                ++count;
            }
            return std::tuple(count, count ? sum[0] / static_cast<double>(count) : 0.0, count ? sum[1] / static_cast<double>(count) : 0.0);
        };
        const auto [off_pixels, off_red, off_green] = panel_colour(before);
        quay::State on = before;
        for (std::uint32_t& worked : on.fused) worked = quay::fuse_steps;
        const auto [on_pixels, on_red, on_green] = panel_colour(on);
        keep(*near, "quay-fuse-on");
        std::println("    a fuse box's panel: {} pixels, red {:.0f} green {:.0f} while off; {} pixels, red {:.0f} green {:.0f} on", off_pixels, off_red, off_green, on_pixels, on_red, on_green);
        expect(gt(off_pixels, std::size_t{50}) && gt(off_red, off_green)) << "red while off";
        expect(gt(on_green, on_red)) << "green once on";
    };

    "one tree, two views: in a game played to the end, the wardens drawn are the wardens a person is shown; a shot shows its flash; the lit beacon glows"_test = [] {
        quay::State state = quay::start(quay::level::harbour());
        auto drawn = picture::Picture::make(quay::level::harbour());
        expect(fatal(drawn.has_value()));
        quay::Guide guide;
        std::size_t compared = 0, agreed = 0, flashes = 0, flash_pixels = 0, most_warden = 0;
        std::uint64_t best_frame = 0;
        std::optional<quay::State> nearest_warden;  // the frame a warden is drawn largest, kept to draw again full size
        std::string disagreed;
        const picture::Frame small{.width = 240, .height = 135, .shadows = false, .glow = false};
        while (state.outcome == quay::Outcome::playing && state.frame < 60 * 120) {
            quay::step(state, guide(state));
            const bool fired = std::ranges::any_of(state.shots, [](const quay::Shot& shot) { return shot.by == 0; });
            if (state.frame % 30 != 0 && !(fired && flashes < 4)) continue;
            drawn->draw(state, small);
            // The wardens with pixels of their own, and those a person is shown.
            std::array<std::size_t, 8> pixels{};
            for (std::size_t pixel = 0; pixel < drawn->targets().ids.size(); ++pixel)
                if (const std::uint32_t warden = drawn->warden_at(pixel); warden > 0 && warden < pixels.size()) ++pixels[warden];
            const auto view = mlibs::json::Document::parse(quay::observe(state, false), "view");
            const std::size_t shown = view ? view->root()["wardens"].items().size() : 99;
            std::size_t any = 0, plain = 0;  // drawn at all; drawn clearly (a dozen pixels) within the night's reach
            for (std::size_t warden = 1; warden <= state.wardens.size(); ++warden) {
                const quay::Warden& one = state.wardens[warden - 1];
                if (one.health <= 0) continue;
                const float distance = math::length(one.at + quay::Vec3{0, 1.2f, 0} - quay::eye(state));
                any += pixels[warden] > 0, plain += pixels[warden] >= 12 && distance < quay::seen_within;
                if (pixels[warden] > most_warden) most_warden = pixels[warden], best_frame = state.frame, nearest_warden = state;
            }
            ++compared;
            if (plain <= shown && shown <= any) ++agreed;
            else disagreed += std::format(" frame {}: drawn {} (clearly {}), shown {};", state.frame, any, plain, shown);
            if (fired) ++flashes, flash_pixels += drawn_by(*drawn, picture::flash_id, picture::flash_id + 1);
        }
        std::println("{} frames compared, {} agreed;{} {} shots drawn, {} flash pixels; the most of a warden {} pixels at frame {}; {} at frame {}", compared, agreed, disagreed, flashes, flash_pixels,
                     most_warden, best_frame, quay::outcome_names[static_cast<std::size_t>(state.outcome)], state.frame);
        expect(state.outcome == quay::Outcome::won) << state.why;
        expect(gt(compared, std::size_t{40}));
        expect(eq(agreed, compared)) << disagreed;
        expect(gt(flashes, std::size_t{0}) && gt(flash_pixels, flashes * 20)) << "each shot's flash at the muzzle";
        expect(gt(most_warden, std::size_t{100})) << "a warden near enough to see its shape";
        if (nearest_warden) {
            drawn->draw(*nearest_warden);
            std::println("a warden near, full size: {}", keep(*drawn, "quay-warden").generic_string());
        }
        // The end, full size, the view turned up to the lamp on its mast (only the picture's view: the game is over):
        // the beacon lit, burning brighter than the display.
        const quay::Vec3 up = state.beacon + quay::Vec3{0, quay::beacon_height, 0} - quay::eye(state);
        state.keeper.yaw = std::atan2(up.x, -up.z), state.keeper.pitch = std::atan2(up.y, std::sqrt(up.x * up.x + up.z * up.z)) - 0.25f;
        drawn->draw(state);
        const std::size_t glass = drawn_by(*drawn, picture::beacon_ids + 1, picture::beacon_ids + 2);
        const auto file = keep(*drawn, "quay-lit");
        std::println("the beacon lit: {} pixels of its glass, {} brighter than the display; {}", glass, drawn->bright(), file.generic_string());
        expect(gt(glass, std::size_t{100})) << "it burns above the watchkeeper";
        expect(gt(drawn->bright(), 100u)) << "lit, brighter than white";
    };

    "a shot leaves its mark where it met the harbour; rain falls through the lamps' light and splashes on the quay, a frame later lower"_test = [] {
        quay::State state = quay::start(quay::level::harbour());
        auto drawn = picture::Picture::make(quay::level::harbour());
        expect(fatal(drawn.has_value()));
        quay::Input fire;
        fire.player.held = 1u << quay::fire_button;
        quay::step(state, fire);
        expect(fatal(eq(state.marks.size(), std::size_t{1}))) << "the shot met what stays";
        const quay::Mark mark = state.marks.front();
        // Walked up to it (only the picture's view), looking at it from a metre and a half.
        quay::State near = state;
        near.shots.clear();  // its flash gone
        const quay::Vec3 back = mark.at + mark.normal * 1.5f;
        near.keeper.at = {back.x, near.keeper.at.y, back.z};
        const quay::Vec3 way = mark.at - quay::eye(near);
        near.keeper.yaw = std::atan2(way.x, -way.z), near.keeper.pitch = std::atan2(way.y, std::sqrt(way.x * way.x + way.z * way.z));
        drawn->draw(near);
        const std::size_t scorched = drawn_by(*drawn, picture::mark_ids, picture::rain_id);
        const auto close = keep(*drawn, "quay-mark");
        // The rain from the start, and a frame later.
        for (int frame = 0; frame < 5; ++frame) quay::step(state, {});
        drawn->draw(state);
        const xrender::particle::Drawn rained = drawn->rained();
        const std::uint64_t before = bits(drawn->shown());
        const auto file = keep(*drawn, "quay-rain");
        quay::step(state, {});
        drawn->draw(state);
        std::println("a mark at ({:.1f}, {:.1f}, {:.1f}) facing ({:.1f}, {:.1f}, {:.1f}): {} pixels of it from 1.5 m ({}); the rain: {} drops and splashes looked at, {} off the frame, {} pixels reached; {}",
                     mark.at.x, mark.at.y, mark.at.z, mark.normal.x, mark.normal.y, mark.normal.z, scorched, close.generic_string(), rained.particles, rained.outside, rained.pixels,
                     file.generic_string());
        // From 1.5 m, 100° across 480 pixels: 134 pixels a metre — the smallest scorch (0.07 m across its half) 275.
        expect(ge(scorched, std::size_t{250})) << "the scorch and its hole, close";
        expect(eq(rained.particles, picture::drops + picture::splashes));
        expect(gt(rained.pixels, std::uint64_t{5000})) << "rain across the view";
        expect(bits(drawn->shown()) != before) << "a frame later the drops have fallen";
    };
}};
