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
}};
