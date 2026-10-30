// The site's frames while it moves: every place flown to, the lenses. Each
// motion comes to rest — the page stops asking for frames — and the bar's
// frame meter then says so: idle, with the run it saw. With --timing each
// frame is also drawn at a 2× display on one thread (as a page without
// threads draws it) and its time split into the runtime's stages and the
// raster, so a slow frame says where it went; run it with --profile release
// for numbers (`mcpp test -p openmui.site --profile release -- --timing`).
import std;
import openmui;
import openmui.site;
import openmui.ui.markdown;
import openmui.ui.canvas;
import openmui.showcase.fighting;
import openmui.testing;

using namespace omui::testing;
using namespace omui;

namespace {

using clock = std::chrono::steady_clock;

bool timing = false;  // --timing

struct Run {
    Headless h;
    explicit Run(float scale) : h{HeadlessOptions{.size = {1280, 800}, .scale = scale, .parallel_raster = false}} {
        AppSpec spec = site::app("home", "en");
        h.runtime().set_theme(spec.theme);
        spec.setup(h.runtime());
        for (auto& v : spec.verbs) (void)h.runtime().verbs().add(v.desc, v.fn);
        h.mount("main", spec.view());
        h.settle();
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
    Suite s{"site.frames"};

    s.test("every place flown to comes to rest, and the frame meter then says the page is idle", [] {
        Run r(timing ? 2.0f : 1.0f);
        for (const site::Place& p : site::places()) {
            if (p.id == "home" || p.id == "moves") continue;  // home: where the page opens; the move list's stage plays on (its own test)
            check(r.h.click(std::format("main/body/nav/to_{}", p.id)), std::format("the nav's {}", p.id));
            const Run::Times t = r.frames(300);
            report(std::format("to {}", p.id), t);
            check(t.rested, std::format("to {}: at rest within 5 s (still: {})", p.id, t.wants));
            const Value v = r.reading();
            check(v.get("idle") && v.get("idle")->as_bool(), std::format("to {}: the meter idle at rest", p.id));
            check(v.get("renderer") && v.get("renderer")->as_string() == "cpu", "the headless host's renderer");
            check(v.get("frames") && v.get("frames")->as_int() > 10, std::format("to {}: the flight's frames counted", p.id));
            check(v.get("cost_ms") && v.get("cost_ms")->is_null(), "no cost where the host does not measure");
        }
    });

    s.test("while a flight moves the meter reads its rate: the frames with something new, at most 60 a second on the headless host's clock", [] {
        Run r(1);
        check(r.h.click("main/body/nav/to_features"));
        for (int i = 0; i < 20; ++i) (void)r.h.step();
        const Value v = r.reading();
        check(v.get("idle") && !v.get("idle")->as_bool(), "moving: not idle");
        const double fps = v.get("fps") ? v.get("fps")->as_double(0) : 0;
        check(fps > 40 && fps < 60.5, std::format("fps {}", fps));
    });

    s.test("the lenses: each switch comes to rest (what stays is a slow timer, no frame a display frame)", [] {
        Run r(timing ? 2.0f : 1.0f);
        for (const std::string_view lens : {"tree", "agent", "test", "pixels"}) {
            check(r.h.click(std::format("main/bar/lens_{}", lens)), std::format("the lens {}", lens));
            const Run::Times t = r.frames(120);
            report(std::format("lens {}", lens), t);
            const FrameOutput out = r.h.step();
            check(out.demand.idle() || out.demand.max_hz <= 10, std::format("lens {}: {} at {} Hz", lens, out.demand.reason, out.demand.max_hz));
        }
    });

    return s.run(static_cast<int>(args.size()), args.data());
}
