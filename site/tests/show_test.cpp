// The site's design played in its page as a show (AG3; the presentation design §4.3, §5): the page as it ships (no
// development mode), its replay played by the Live Design key's verb, stepped at 60 frames a second, every frame's
// picture against the one before outside the replay's own card — how much of the time the page is seen to change.
// Twice: as the replay played before it had a show (each step 1.5 s, shown at once), and as its show plays it (each
// step for its beat, what it changes presented over most of the beat). Both numbers are printed; the show changes
// more of the time than the replay did. The design's bound (30% still, AG1 §5) is AG3's open acceptance.
import std;
import mcpplibs.mtest;
import omui;
import omui.player;
import omui.entry.player;
import omui.lang.js;
import mlibs.agent;

using namespace mcpplibs::mtest;
using namespace omui;

namespace {

std::filesystem::path here() { return std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path(); }

std::string file_text(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// How a run of frames went: how long, the share of its frames with nothing changed, its longest still stretch.
struct Seen {
    double seconds = 0, still_share = 0, longest_still = 0;
    std::size_t frames = 0;
};

constexpr double frame = 1.0 / 60;

}  // namespace

extern "C++" void omui_application_components();

static UTest cases{"site.show", [] {
    omui_application_components();

    "the design played in the page as a show: each step for its beat, presented over it — changing more of the time than before"_test = [] {
        auto manifest = read_manifest(here() / "mcpp.toml");
        expect(fatal(manifest.has_value())) << (manifest ? std::string() : manifest.error());
        const std::string replay_text = file_text(here() / "design" / "main.replay.xdef.json");
        const std::string show_text = file_text(here() / "design" / "main.show.timeline.xdef.json");
        expect(fatal(!replay_text.empty() && !show_text.empty())) << "the replay and its show (design.show writes it)";

        // One way of playing it, from the page as it ships: its frames stepped and compared, the card left out.
        const auto played = [&](bool as_show) -> Seen {
            Headless host({.level = HeadlessLevel::pixels, .size = manifest->size});
            player::Player page(host.runtime(), *manifest, {lang::js::typescript()});
            expect(fatal(page.start().has_value()));
            const auto settle = [&] {
                expect(host.wait([&] { return page.idle(); })) << "its scripts done";
                host.settle();
            };
            settle();
            generate::Generation generation(host.runtime(), {.replay = replay_text,
                                                             .replay_file = "main.replay.xdef.json",
                                                             .replay_course = "o",
                                                             .replay_show = as_show ? show_text : std::string(),
                                                             .replay_show_file = "main.show.timeline.xdef.json",
                                                             .show = [&](const std::map<std::string, std::string>& texts) -> std::expected<void, std::string> {
                                                                 if (auto shown = page.show(texts); !shown) return std::unexpected(shown.error().message);
                                                                 return {};
                                                             },
                                                             .restore = [&] { (void)page.discard_edits(); }});
            const auto invoke = [&](Value args) {
                Invocation call;
                call.actor = {"person", "visitor", {}};
                call.args = std::move(args);
                return host.runtime().verbs().invoke("generate.replay", call);
            };
            const auto steps = mlibs::agent::read_replay(replay_text, "main.replay.xdef.json");
            expect(fatal(steps.has_value()));
            double at = host.runtime().now().count();
            mrender::raster::Surface before = host.pixels();
            Seen seen;
            std::size_t still = 0;
            double still_run = 0;
            std::map<std::int64_t, std::pair<std::size_t, std::size_t>> by_step;  // a step's frames, and its still ones
            std::int64_t step_now = 0;
            const auto take = [&] {
                at += frame;
                (void)host.step({}, seconds(at));
                const mrender::raster::Surface& now = host.pixels();
                const NodeId card = host.runtime().find(std::format("{}/dock/replay", generate::Replayer::slot));
                const Rect box = host.runtime().valid(card) ? host.runtime().drawn_bounds(card) : Rect{};
                const float scale = host.runtime().scale();
                bool changed = false;
                for (const auto& region : mrender::raster::changed_regions(before, now)) {
                    const Rect seen_region{region.x / scale, region.y / scale, region.width / scale, region.height / scale};
                    const bool in_card = seen_region.x >= box.x - 1 && seen_region.y >= box.y - 1 && seen_region.x + seen_region.width <= box.x + box.width + 1 &&
                                         seen_region.y + seen_region.height <= box.y + box.height + 1;
                    if (!in_card) changed = true;
                }
                before = now;
                ++seen.frames;
                ++by_step[step_now].first;
                if (!changed) ++by_step[step_now].second;
                if (changed) {
                    still_run = 0;
                } else {
                    ++still;
                    still_run += frame;
                    seen.longest_still = std::max(seen.longest_still, still_run);
                }
            };
            if (as_show) {
                // The Live Design key: played from the first step, as its show says; until it ends.
                const Envelope started = invoke(Value{}.set("from", 1));
                expect(fatal(started.exit_code().value_or(-1) == 0)) << started.ndjson();
                while (seen.frames < 60 * 120) {
                    take();
                    const Envelope asked = invoke(Value(Value::Map{}));
                    const Value* state = asked.data_value();
                    if (const Value* step = state ? state->get("at") : nullptr; step && step->is_number()) step_now = step->as_int();
                    if (const Value* ended = state ? state->get("ended") : nullptr; ended && ended->as_bool(false)) break;
                }
            } else {
                // Before its show: each step gone to and held for its own seconds, shown at once.
                for (std::size_t step = 1; step <= steps->steps.size(); ++step) {
                    expect(invoke(Value{}.set("at", static_cast<std::int64_t>(step))).exit_code().value_or(-1) == 0);
                    step_now = static_cast<std::int64_t>(step);
                    for (double held = 0; held < steps->steps[step - 1].seconds; held += frame) take();
                }
            }
            for (const auto& [step, counts] : by_step)
                if (step > 0)
                    std::println("      step {}: {:.2f} s, {:.0f}% still", step, static_cast<double>(counts.first) * frame,
                                 100.0 * static_cast<double>(counts.second) / static_cast<double>(std::max<std::size_t>(1, counts.first)));
            seen.seconds = static_cast<double>(seen.frames) * frame;
            seen.still_share = static_cast<double>(still) / static_cast<double>(std::max<std::size_t>(1, seen.frames));
            (void)invoke(Value{}.set("stop", true));
            return seen;
        };

        const Seen unpaced = played(false), shown = played(true);
        std::println("    the site's replay before its show: {:.1f} s, still {:.0f}% of the frames, longest still {:.2f} s", unpaced.seconds,
                     unpaced.still_share * 100, unpaced.longest_still);
        std::println("    as its show plays it:            {:.1f} s, still {:.0f}% of the frames, longest still {:.2f} s", shown.seconds,
                     shown.still_share * 100, shown.longest_still);
        // Held: the show is seen to change more of the time than the replay was, and ends. The design's bound (30% still,
        // AG1 §5) is AG3's acceptance, open while the beats are read's (§4.5): printed above, not asserted here.
        expect(shown.still_share < unpaced.still_share) << "the show changes more of the time";
        expect(shown.seconds < 120) << "it ends";
    };
}};
