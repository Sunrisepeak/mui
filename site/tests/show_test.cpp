// The site's design played in its page as a show (AG3; the presentation design §4.3, §5): the page as it ships (no
// development mode), its replay played by the Live Design key's verb, stepped at 60 frames a second, every frame's
// picture against the one before outside the replay's own card — how much of the time the page is seen to change.
// Twice: as the replay played before it had a show (each step 1.5 s, shown at once), and as its show plays it (each
// step for its beat, what it changes — its nodes, its words, its places — seen to move over most of the beat, as
// design.show measured it). Both are printed; the show changes more of the time than the replay did and is still at
// most 30% of its frames (AG1 §5) — the replay's own overlay (its card, its course, its maker's note) keeping its own
// motion, so only the design's change is slowed.
// Then sampled in time (AGENTS §6): each beat's last frame is its step as gone to, the frames between are neither
// end, a step without a visible change lasts at most 0.3 s, the show ends on the page as it ships; paused, sent
// forward or back in the middle of a beat, the page lands exactly where it was taken; unseen, a beat holds what is
// left of it; with the person's less motion the show is its steps cut, on the same beats. And the show's film (AG4) is
// the page — its frame at t is the page's frame at t — with its sound: a score under it, a chime on each seen beat.
import std;
import mcpplibs.mtest;
import omui;
import omui.player;
import omui.entry.player;
import omui.lang.js;
import mlibs.agent;
import mlibs.hash;

using namespace mcpplibs::mtest;
using namespace omui;

namespace {

std::filesystem::path here() { return std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path(); }

std::string file_text(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

constexpr double frame = 1.0 / 60;

// The page as it ships with its replay, played with its show or without: its frames stepped and compared.
struct Stage {
    Headless                            host;
    player::Player                      page;
    std::optional<generate::Generation> generation;

    Stage(const player::Manifest& manifest, const std::string& replay, const std::string& show)
        : host({.level = HeadlessLevel::pixels, .size = manifest.size}), page(host.runtime(), manifest, {lang::js::typescript(), lang::js::javascript()}) {
        expect(fatal(page.start().has_value()));
        settle();
        generation.emplace(host.runtime(),
                           generate::GenerationOptions{.replay = replay,
                                                       .replay_file = "main.replay.xdef.json",
                                                       .replay_course = "o",
                                                       .replay_show = show,
                                                       .replay_show_file = "main.show.timeline.xdef.json",
                                                       .show = [this](const std::map<std::string, std::string>& texts) -> std::expected<void, std::string> {
                                                           if (auto shown = page.show(texts); !shown) return std::unexpected(shown.error().message);
                                                           return {};
                                                       },
                                                       .restore = [this] { (void)page.discard_edits(); }});
    }

    // Its scripts done, then frames until nothing moves (a step gone to, a pause: no timer of the replay's is due).
    void settle() {
        expect(host.wait([&] { return page.idle(); })) << "its scripts done";
        host.settle();
    }

    // The Live Design key's verb, as a visitor's agent asks it.
    Envelope invoke(Value args) {
        Invocation call;
        call.actor = {"person", "visitor", {}};
        call.args = std::move(args);
        return host.runtime().verbs().invoke("generate.replay", call);
    }
    // Where the replay is: the step shown (0: none), whether it ended, each step's time on screen.
    struct Where {
        std::int64_t        at = 0;
        bool                ended = false, held = false;
        std::vector<double> beats;
    };
    Where where() {
        const Envelope asked = invoke(Value(Value::Map{}));
        Where out;
        const Value* state = asked.data_value();
        if (!state) return out;
        if (const Value* step = state->get("at"); step && step->is_number()) out.at = step->as_int();
        if (const Value* ended = state->get("ended")) out.ended = ended->as_bool(false);
        if (const Value* held = state->get("held")) out.held = held->as_bool(false);
        if (const Value* steps = state->get("steps"); steps && steps->list())
            for (const Value& step : *steps->list())
                if (const Value* seconds = step.get("seconds"); seconds && seconds->is_number()) out.beats.push_back(seconds->as_double());
        return out;
    }

    // A picture of the page, and where the replay's card was in it: left out of what is compared (it says where the
    // replay is, and plays — not the page; its height follows its caption, and it goes when the replay ends).
    struct Shot {
        mrender::raster::Surface pixels;
        Rect                     card;
    };
    Shot shot() {
        const NodeId card = host.runtime().find(std::format("{}/dock/replay", generate::Replayer::slot));
        return {host.pixels(), host.runtime().valid(card) ? host.runtime().drawn_bounds(card) : Rect{}};
    }
    // One frame later.
    Shot next() {
        (void)host.step({}, seconds(host.now().count() + frame));
        return shot();
    }

    // The regions two pictures differ in, outside either's card (logical pixels), with how many pixels each.
    std::vector<std::pair<Rect, std::uint64_t>> differing(const Shot& before, const Shot& after) {
        const float scale = host.runtime().scale();
        const auto inside = [](const Rect& seen, const Rect& box) {
            return box.width > 0 && seen.x >= box.x - 1 && seen.y >= box.y - 1 && seen.x + seen.width <= box.x + box.width + 1 &&
                   seen.y + seen.height <= box.y + box.height + 1;
        };
        std::vector<std::pair<Rect, std::uint64_t>> out;
        for (const auto& region : mrender::raster::changed_regions(before.pixels, after.pixels)) {
            const Rect seen{region.x / scale, region.y / scale, region.width / scale, region.height / scale};
            if (!inside(seen, before.card) && !inside(seen, after.card)) out.emplace_back(seen, region.pixels);
        }
        return out;
    }
    // How many pixels differ outside the card.
    std::uint64_t apart(const Shot& before, const Shot& after) {
        std::uint64_t differ = 0;
        for (const auto& [region, pixels] : differing(before, after)) differ += pixels;
        return differ;
    }
    // Where they differ, for a message: how many pixels, in which box.
    std::string where_apart(const Shot& before, const Shot& after) {
        float left = 1e9f, top = 1e9f, right = -1, bottom = -1;
        std::uint64_t differ = 0;
        for (const auto& [region, pixels] : differing(before, after)) {
            left = std::min(left, region.x);
            top = std::min(top, region.y);
            right = std::max(right, region.x + region.width);
            bottom = std::max(bottom, region.y + region.height);
            differ += pixels;
        }
        return right < 0 ? std::string("nowhere") : std::format("{} px in ({:.0f}, {:.0f}) – ({:.0f}, {:.0f})", differ, left, top, right, bottom);
    }
};

// How a run of frames went: how long, the share of its frames with nothing changed, its longest still stretch (in which
// step, how far into it).
struct Seen {
    double       seconds = 0, still_share = 0, longest_still = 0, longest_into = 0;
    std::int64_t longest_step = 0;
    std::size_t  frames = 0;
};

// A picture's fingerprint (mlibs.hash: FNV-1a over its size and its bytes): two pictures alike to the byte.
std::uint64_t fingerprint(const mrender::raster::Surface& picture) {
    const auto pixels = picture.pixels();
    return mlibs::hash::Fnv1a{}
        .word(static_cast<std::uint64_t>(picture.width()) << 32 | static_cast<std::uint32_t>(picture.height()))
        .bytes(std::span(reinterpret_cast<const std::uint8_t*>(pixels.data()), pixels.size_bytes()))
        .value();
}

// The site as it ships: its manifest, its design's replay and the show made of it.
struct Site {
    std::optional<player::Manifest> manifest;
    std::string                     replay, show;
};

Site read_site() {
    Site site;
    auto manifest = read_manifest(here() / "mcpp.toml");
    expect(fatal(manifest.has_value())) << (manifest ? std::string() : manifest.error());
    site.manifest = std::move(*manifest);
    site.replay = file_text(here() / "design" / "main.replay.xdef.json");
    site.show = file_text(here() / "design" / "main.show.timeline.xdef.json");
    expect(fatal(!site.replay.empty() && !site.show.empty())) << "the replay and its show (design.show writes it)";
    // What the page carries draws a change of words as a morph (omui.view.morphs; in every world that draws pixels):
    // the test program measures with it too, else a change of words would show at once here and not there.
    if (!omui::morphs_installed()) expect(fatal(omui::install_morphs().has_value())) << "the morphs' provider";
    return site;
}

}  // namespace

extern "C++" void omui_application_components();

static UTest cases{"site.show", [] {
    omui_application_components();

    "the design played in the page as a show: each step for its beat, presented over it — still at most 30% of the time"_test = [] {
        const Site site = read_site();
        const auto steps = mlibs::agent::read_replay(site.replay, "main.replay.xdef.json");
        expect(fatal(steps.has_value()));

        // One way of playing it, from the page as it ships: its frames stepped and compared, the card left out.
        const auto played = [&](bool as_show) -> Seen {
            Stage stage(*site.manifest, site.replay, as_show ? site.show : std::string());
            Stage::Shot before = stage.shot();
            Seen seen;
            std::size_t still = 0;
            double still_run = 0;
            std::map<std::int64_t, std::pair<std::size_t, std::size_t>> by_step;  // a step's frames, and its still ones
            std::int64_t step_now = 0;
            double step_began = 0;  // when the step shown began (the time of its first frame)
            const auto take = [&] {
                Stage::Shot now = stage.next();
                const bool changed = stage.apart(before, now) > 0;
                before = std::move(now);
                ++seen.frames;
                ++by_step[step_now].first;
                if (!changed) ++by_step[step_now].second;
                if (changed) {
                    still_run = 0;
                } else {
                    ++still;
                    still_run += frame;
                    if (still_run > seen.longest_still) {
                        seen.longest_still = still_run;
                        seen.longest_step = step_now;
                        seen.longest_into = stage.host.now().count() - step_began - still_run + frame;
                    }
                }
            };
            if (as_show) {
                // The Live Design key: played from the first step, as its show says; until it ends.
                const Envelope started = stage.invoke(Value{}.set("from", 1));
                expect(fatal(started.exit_code().value_or(-1) == 0)) << started.ndjson();
                while (seen.frames < 60 * 120) {
                    take();
                    const Stage::Where where = stage.where();
                    if (where.at != step_now) {
                        step_now = where.at;
                        step_began = stage.host.now().count();
                    }
                    if (where.ended) break;
                }
            } else {
                // Before its show: each step gone to and held for its own seconds, shown at once.
                for (std::size_t step = 1; step <= steps->steps.size(); ++step) {
                    expect(stage.invoke(Value{}.set("at", static_cast<std::int64_t>(step))).exit_code().value_or(-1) == 0);
                    step_now = static_cast<std::int64_t>(step);
                    step_began = stage.host.now().count();
                    for (double held = 0; held < steps->steps[step - 1].seconds; held += frame) take();
                }
            }
            for (const auto& [step, counts] : by_step)
                if (step > 0)
                    std::println("      step {}: {:.2f} s, {:.0f}% still", step, static_cast<double>(counts.first) * frame,
                                 100.0 * static_cast<double>(counts.second) / static_cast<double>(std::max<std::size_t>(1, counts.first)));
            seen.seconds = static_cast<double>(seen.frames) * frame;
            seen.still_share = static_cast<double>(still) / static_cast<double>(std::max<std::size_t>(1, seen.frames));
            (void)stage.invoke(Value{}.set("stop", true));
            return seen;
        };

        const Seen unpaced = played(false), shown = played(true);
        for (const auto& [name, seen] : {std::pair{"the site's replay before its show:", unpaced}, std::pair{"as its show plays it:           ", shown}})
            std::println("    {} {:.1f} s, still {:.0f}% of the frames, longest still {:.2f} s (step {}, {:.2f} s in)", name, seen.seconds,
                         seen.still_share * 100, seen.longest_still, seen.longest_step, seen.longest_into);
        // Held: the show is seen to change more of the time than the replay was, within the design's bound (AG1 §5:
        // still at most 30% of its frames, from 86%), and ends.
        expect(shown.still_share < unpaced.still_share) << "the show changes more of the time";
        expect(shown.still_share <= 0.30) << std::format("still {:.0f}% of the show's frames (AG1 §5: at most 30%)", shown.still_share * 100);
        expect(shown.seconds < 120) << "it ends";
    };

    "the show sampled in time: each beat ends on its step, passes between, and lands where it is taken — paused, sent on or back"_test = [] {
        const Site site = read_site();
        Stage stage(*site.manifest, site.replay, site.show);
        using Shot = Stage::Shot;

        // Where each step is, gone to and settled (a page as it ships shows it at once); 0 the page as it ships.
        std::vector<Shot> step_of{stage.shot()};
        const std::vector<double> beats = stage.where().beats;
        expect(fatal(!beats.empty())) << "the show's beats";
        for (std::size_t step = 1; step <= beats.size(); ++step) {
            expect(fatal(stage.invoke(Value{}.set("at", static_cast<std::int64_t>(step))).exit_code().value_or(-1) == 0));
            stage.settle();
            step_of.push_back(stage.shot());
        }
        (void)stage.invoke(Value{}.set("stop", true));
        stage.settle();
        expect(eq(stage.apart(stage.shot(), step_of[0]), std::uint64_t{0})) << "stopped: the page as it ships";

        // A step with no visible change lasts at most 0.3 s (AG1 §5: its words are said with the next).
        for (std::size_t step = 1; step < step_of.size(); ++step) {
            const std::uint64_t changed = stage.apart(step_of[step - 1], step_of[step]);
            std::println("      step {}: {:.2f} s, {} pixels changed from the step before", step, beats[step - 1], changed);
            if (changed == 0)
                expect(beats[step - 1] <= 0.3 + 1e-9) << std::format("step {} changes nothing seen and lasts {:.2f} s", step, beats[step - 1]);
        }

        // Played from the first step: each beat's frames a quarter and half way in, and its last.
        expect(fatal(stage.invoke(Value{}.set("from", 1)).exit_code().value_or(-1) == 0));
        std::int64_t step_now = 1;
        double began = stage.host.now().count();
        Shot last = stage.shot();
        std::map<std::int64_t, std::vector<Shot>> between;  // a step's frames at ¼ and ½ of its beat
        std::size_t frames = 0;
        bool ended = false;
        while (!ended && ++frames < 60 * 120) {
            Shot now = stage.next();
            const Stage::Where where = stage.where();
            if (where.at != step_now) {
                // The beat that ended: its last frame is its step.
                if (step_now > 0)
                    expect(eq(stage.apart(last, step_of[static_cast<std::size_t>(step_now)]), std::uint64_t{0}))
                        << std::format("step {}'s last frame is the step: {}", step_now, stage.where_apart(last, step_of[static_cast<std::size_t>(step_now)]));
                step_now = where.at;
                began = stage.host.now().count() - frame;
            }
            ended = where.ended;
            if (step_now > 0) {
                const double beat = beats[static_cast<std::size_t>(step_now) - 1], into = stage.host.now().count() - began;
                std::vector<Shot>& kept = between[step_now];
                if ((kept.empty() && into >= beat / 4) || (kept.size() == 1 && into >= beat / 2)) kept.push_back(now);
            }
            last = std::move(now);
        }
        expect(ended) << "the show ends";
        stage.settle();
        expect(eq(stage.apart(stage.shot(), step_of[0]), std::uint64_t{0})) << "the show ends on the page as it ships";
        // Between a step and the one before, where it changed something seen: neither of them, a quarter and half way.
        for (const auto& [step, kept] : between) {
            const auto index = static_cast<std::size_t>(step);
            if (stage.apart(step_of[index - 1], step_of[index]) == 0) continue;
            for (std::size_t sample = 0; sample < kept.size(); ++sample) {
                const std::uint64_t from_before = stage.apart(kept[sample], step_of[index - 1]), from_step = stage.apart(kept[sample], step_of[index]);
                std::println("      step {} at {}: {} pixels from the step before, {} from the step", step, sample == 0 ? "1/4" : "1/2", from_before,
                             from_step);
                expect(from_before > 0 && from_step > 0) << std::format("step {} at {} of its beat is between", step, sample == 0 ? "a quarter" : "half");
            }
        }

        // Taken in the middle of a beat: paused, it ends on its step; sent on or back, it lands on the one it was sent to.
        const auto midway = [&](std::int64_t from, Value then, std::size_t lands, std::string_view what) {
            expect(fatal(stage.invoke(Value{}.set("from", from)).exit_code().value_or(-1) == 0));
            const double until = stage.host.now().count() + beats[static_cast<std::size_t>(from) - 1] * 0.4;
            while (stage.host.now().count() < until) (void)stage.next();
            expect(fatal(stage.invoke(std::move(then)).exit_code().value_or(-1) == 0));
            stage.settle();
            expect(eq(stage.apart(stage.shot(), step_of[lands]), std::uint64_t{0})) << std::format("{}: {}", what, stage.where_apart(stage.shot(), step_of[lands]));
        };
        midway(3, Value{}.set("pause", true), 3, "paused in step 3's beat: step 3");
        midway(4, Value{}.set("at", 6), 6, "sent on to step 6 in step 4's beat: step 6");
        midway(7, Value{}.set("at", 2), 2, "sent back to step 2 in step 7's beat: step 2");
        (void)stage.invoke(Value{}.set("stop", true));
        stage.settle();
        expect(eq(stage.apart(stage.shot(), step_of[0]), std::uint64_t{0})) << "stopped: the page as it ships";

        // Unseen in the middle of a beat (a hidden tab, a minimized window: the host says so): the step holds what is left
        // of its time, still playing, and goes on with it when the window is seen again.
        expect(fatal(stage.invoke(Value{}.set("from", 3)).exit_code().value_or(-1) == 0));
        const double hidden_at = stage.host.now().count() + beats[2] * 0.4;
        while (stage.host.now().count() < hidden_at) (void)stage.next();
        const double left = beats[2] - (stage.host.now().count() - (hidden_at - beats[2] * 0.4));
        omui::window().set_shown(false);
        for (int unseen = 0; unseen < 60 * 3; ++unseen) (void)stage.next();  // three seconds: longer than the beat
        const Stage::Where held = stage.where();
        expect(eq(held.at, std::int64_t{3}) && held.held) << "unseen, it holds at its step";
        omui::window().set_shown(true);
        const double seen_again = stage.host.now().count();
        std::int64_t now_at = 3;
        while (now_at == 3 && stage.host.now().count() < seen_again + beats[2] + 1) {
            (void)stage.next();
            now_at = stage.where().at;
        }
        const double went_on = stage.host.now().count() - seen_again;
        expect(eq(now_at, std::int64_t{4}) && std::abs(went_on - left) <= 2 * frame)
            << std::format("seen again, it went on after {:.3f} s with {:.3f} s left", went_on, left);
        (void)stage.invoke(Value{}.set("stop", true));
        stage.settle();

        // The person's less motion (the system's setting, which the hosts read): the show is its steps cut — every frame
        // the step shown or, for the frame it is shown in, the one before — on the same beats, to the same end.
        stage.host.runtime().set_reduce_motion(true);
        expect(fatal(stage.invoke(Value{}.set("from", 1)).exit_code().value_or(-1) == 0));
        std::size_t cut = 0, seen_frames = 0;
        std::int64_t at_step = 1;
        for (bool over = false; !over && seen_frames < 60 * 120; ++seen_frames) {
            const Shot now = stage.next();
            const Stage::Where where = stage.where();
            over = where.ended;
            if (over) break;
            const bool moved_on = where.at != at_step;
            at_step = where.at;
            const auto index = static_cast<std::size_t>(at_step);
            const bool is_step = stage.apart(now, step_of[index]) == 0, is_before = moved_on && stage.apart(now, step_of[index - 1]) == 0;
            if (!is_step && !is_before) {
                ++cut;
                if (cut == 1) std::println("      less motion, step {}: {}", at_step, stage.where_apart(now, step_of[index]));
            }
        }
        expect(eq(cut, std::size_t{0})) << std::format("{} frames between steps with less motion", cut);
        stage.settle();
        expect(eq(stage.apart(stage.shot(), step_of[0]), std::uint64_t{0})) << "with less motion too, the show ends on the page as it ships";
        stage.host.runtime().set_reduce_motion(false);
    };

    "the show's film is the page: its frame at t is the page's at t, to the byte, start to end (AG4; one evaluation)"_test = [] {
        const Site site = read_site();
        // The film at 30 a second (omui::film_show: what `omui-player --film-show` writes and video render encodes).
        std::vector<std::uint64_t> film;
        const auto filmed = omui::film_show(*site.manifest, 30, [&](std::size_t, Headless& host) { film.push_back(fingerprint(host.pixels())); });
        expect(fatal(filmed.has_value())) << (filmed ? std::string() : filmed.error());
        expect(eq(filmed->frames, film.size()) && film.size() >= static_cast<std::size_t>(filmed->seconds * 30)) << std::format("{} frames for {:.2f} s", film.size(), filmed->seconds);
        // The page as the visitor's key plays it, at the show's own 60 a second: every other frame is the film's time.
        Stage stage(*site.manifest, site.replay, site.show);
        // Its sound, as the show states it (design/main.direction.xdef.json): the score under it and the chimes' bank,
        // beside the show; a `step` chime at the start of each beat that changes what is seen — not the still last one.
        expect(filmed->score.filename() == "main.music.xdef.ts" && std::filesystem::exists(filmed->score)) << filmed->score.generic_string();
        expect(filmed->sounds.filename() == "main.sound.xdef.ts" && std::filesystem::exists(filmed->sounds)) << filmed->sounds.generic_string();
        const std::vector<double> beats = stage.where().beats;
        expect(fatal(!beats.empty() && filmed->cues.size() == beats.size() - 1)) << std::format("{} chimes for {} beats", filmed->cues.size(), beats.size());
        double start = 0;
        for (std::size_t beat = 0; beat < filmed->cues.size(); start += beats[beat], ++beat)
            expect(filmed->cues[beat].first == "step" && std::abs(filmed->cues[beat].second - start) < 1e-6)
                << std::format("beat {}'s chime at {:.4f} s, its start {:.4f} s", beat + 1, filmed->cues[beat].second, start);
        expect(fatal(stage.invoke(Value{}.set("from", 1)).exit_code().value_or(-1) == 0));
        std::size_t same = 0, first_apart = film.size();
        for (std::size_t drawn = 1, kept = 0; kept < film.size(); ++drawn) {
            (void)stage.host.step({}, seconds(stage.host.now().count() + frame));
            if (drawn % 2 != 0) continue;
            if (fingerprint(stage.host.pixels()) == film[kept]) ++same;
            else if (first_apart == film.size()) first_apart = kept;
            ++kept;
        }
        std::println("    the film: {} frames at 30 a second, {:.2f} s; {} of them the page's at the same time", film.size(), filmed->seconds, same);
        expect(eq(same, film.size())) << std::format("the film's frame {} ({:.3f} s) is not the page's", first_apart, static_cast<double>(first_apart + 1) / 30);
    };
}};
