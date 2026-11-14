// The site's frames while it moves: every place shown, the language switched, the lenses. Each
// motion comes to rest — the page stops asking for frames — and the bar's
// frame meter then says so: idle (and `reading` gives the run it saw). With --timing each
// frame is also drawn at a 2× display on one thread (as a page without
// threads draws it) and its time split into the runtime's stages and the
// raster, so a slow frame says where it went; run it with --profile release
// for numbers (`mcpp test -p openmui.site --profile release -- --timing`).
import std;
import omui;
import omui.site;
import omui.ui.markdown;
import mlibs.testing;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
using namespace omui;

namespace {

using clock = std::chrono::steady_clock;

bool timing = false;  // --timing

struct Run {
    Headless h;
    explicit Run(float scale) : h{HeadlessOptions{.size = {1440, 900}, .scale = scale, .parallel_raster = false}} {
        AppSpec spec = site::app("home", "en");
        h.runtime().set_theme(spec.theme);
        spec.setup(h.runtime());
        for (auto& v : spec.verbs) (void)h.runtime().verbs().add(v.desc, v.fn);
        h.mount("main", spec.view());
        h.settle(seconds(0.9));
        pause_preview("home");
        h.settle(seconds(0.8));
        expect(h.click("main/foot/tools_toggle"));
        h.settle();
    }
    bool navigate(const site::Place& place) { return h.click(std::format("main/bar/nav/to_{}", place.id)); }
    // Automatic previews deliberately keep moving while they are visible.
    // Exercise their actual public pause controls before asserting idleness;
    // keep full motion so these tests still measure transitions and language FPS.
    void pause_preview(std::string_view place) {
        // The walls' own motion (they play a few cards by themselves), paused through their buttons.
        for (const std::string wall : {std::format("main/body/view/{}/page/wall/tools/pause", place), std::format("main/body/view/{}/page/recipes/tools/pause", place)})
            if (const auto button = h.tree().node(wall); button && button->name == "Pause motion") {
                expect(h.tree().act(wall, "activate").exit_code() == std::optional<int>(0)) << wall;
                h.step();
            }
        if (place == "explore" || place == "motion" || place == "components") return;  // the studio and the walls (paused above); a gallery opens one case at a time
        std::string control;
        if (place == "home") control = "main/body/view/home/page/landing/scene/controls/pause";
        else if (place == "components") control = "main/body/view/components/page/gallery/detail/button/stage/actions/pause";
        else if (place == "motion" || place == "explore") control = std::format("main/body/view/{}/page/motion_gallery/transport/buttons/play", place);
        else if (place == "styles") control = "main/body/view/styles/page/styles_feature/preview_controls/preview_pause";
        else if (place == "interface") control = "main/body/view/interface/page/interface_feature/preview_controls/preview_pause";
        if (control.empty()) return;
        const auto button = h.tree().node(control);
        expect(button.has_value()) << control;
        if (!button) return;
        if (button->name == "Pause" || button->name == "Pause auto" || button->name == "Pause preview") {
            expect(h.tree().act(control, "activate").exit_code() == std::optional<int>(0)) << control;
            h.step();
        }
        const auto paused = h.tree().node(control);
        expect(paused && (paused->name == "Play" || paused->name == "Play preview" || paused->name == "Resume preview")) << std::string(place) + ": preview explicitly paused";
    }
    struct Times {
        std::vector<double> frame, raster;
        bool                rested = false;
        std::string         wants;  // what asked for the last frame, when the page did not rest
    };
    // Frames until the page rests (at most `most`): each one's milliseconds.
    Times frames(int most = 240) {
        Times t;
        for (int i = 0; i < most; ++i) {
            const auto t0 = clock::now();
            const FrameOutput out = h.step();
            const auto t1 = clock::now();
            if (timing) (void)h.pixels();
            const auto t2 = clock::now();
            t.frame.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
            t.raster.push_back(std::chrono::duration<double, std::milli>(t2 - t1).count());
            if (!out.animating && out.demand.idle()) {
                t.rested = true;
                break;
            }
            t.wants = std::format("{} at {} Hz", out.demand.reason, out.demand.max_hz);
        }
        return t;
    }
    Value reading() {
        const Envelope e = h.runtime().act(h.runtime().find("main/bar/frames"), "reading");
        const Value* v = e.data_value();
        return v ? *v : Value{};
    }
};

void report(std::string_view what, const Run::Times& t) {
    if (!timing) return;
    const auto line = [](std::string_view kind, std::vector<double> ms) {
        if (ms.empty()) return std::string();
        std::ranges::sort(ms);
        const double sum = std::accumulate(ms.begin(), ms.end(), 0.0);
        return std::format("{} median {:6.1f} p90 {:6.1f} max {:6.1f} total {:7.1f}", kind, ms[ms.size() / 2], ms[ms.size() * 9 / 10], ms.back(), sum);
    };
    std::println("    {:<16} {:>3} frames  {}  |  {}", what, t.frame.size(), line("frame", t.frame), line("raster", t.raster));
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<char*> args(argv, argv + argc);
    timing = std::erase_if(args, [](const char* a) { return std::string_view(a) == "--timing"; }) > 0;

    "every place shown comes to rest, and the frame meter then says the page is idle"_test = [] {
        Run r(timing ? 2.0f : 1.0f);
        for (const site::Place& p : site::places()) {
            if (p.id == "home" || !p.navigation) continue;  // secondary exhibits are covered in brand_test
            expect(r.navigate(p)) << std::format("the nav's {}", p.id);
            r.h.step();
            r.pause_preview(p.id);
            const Run::Times t = r.frames(300);
            report(std::format("to {}", p.id), t);
            expect(t.rested) << std::format("to {}: at rest within 5 s (still: {})", p.id, t.wants);
            const Value v = r.reading();
            expect(v.get("idle") && v.get("idle")->as_bool()) << std::format("to {}: the meter idle at rest ({})", p.id, v.json());
            expect(v.get("renderer") && v.get("renderer")->as_string() == "cpu") << "the headless host's renderer";
            expect(v.get("frames") && v.get("frames")->as_int() > 0) << std::format("to {}: its frames counted", p.id);
            expect(v.get("cost_ms") && v.get("cost_ms")->is_null()) << "no cost where the host does not measure";
        }
    };

    "while the language switches the meter reads its rate: the frames with something new, at most 60 a second on the headless host's clock"_test = [] {
        Run r(1);
        expect(r.h.click("main/bar/lang"));
        for (int i = 0; i < 12; ++i) (void)r.h.step();
        const Value v = r.reading();
        expect(v.get("idle") && !v.get("idle")->as_bool()) << "moving: not idle";
        const double fps = v.get("fps") ? v.get("fps")->as_double(0) : 0;
        expect(fps > 40 && fps < 60.5) << std::format("fps {}", fps);
    };

    "the lenses: each switch comes to rest: what stays is a slow timer, and a frame displayed only when it runs"_test = [] {
        Run r(timing ? 2.0f : 1.0f);
        for (const std::string_view lens : {"tree", "agent", "test", "pixels"}) {
            expect(r.h.click(std::format("main/foot/lens_{}", lens))) << std::format("the lens {}", lens);
            const Run::Times t = r.frames(120);
            report(std::format("lens {}", lens), t);
            // Then a second of it: nothing but timers asking, and those slow (a
            // timer's demand is when it is next due — 60 Hz the frame before it —
            // so its rate is read from its due times); a frame displayed only when
            // one ran (the foot's strip of the page's time catching up).
            const std::uint64_t shown = r.h.runtime().display_list().hash();
            std::set<double> due;
            int displayed = 0;
            std::string other;
            for (int k = 0; k < 60; ++k) {
                const FrameOutput out = r.h.step();
                if (out.repainted && r.h.runtime().display_list().hash() != shown) ++displayed;
                if (!out.demand.idle() && out.demand.reason != "timer") other = out.demand.reason;
                if (out.next_timer) due.insert(out.next_timer->count());
            }
            expect(displayed <= std::max<int>(1, static_cast<int>(due.size()))) << std::format("lens {}: {} display frames at rest, {} timers", lens, displayed, due.size());
            expect(other.empty()) << std::format("lens {}: frames asked for by {}", lens, other);
            expect(due.size() <= 10) << std::format("lens {}: {} timers in a second", lens, due.size());
        }
    };

    return mcpplibs::mtest::run(static_cast<int>(args.size()), args.data());
}
