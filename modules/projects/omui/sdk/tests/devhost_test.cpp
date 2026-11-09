// The development host (W16, D-154): the application drawn in the host's
// window from its own process (here a thread with its own runtime), the
// person's press handled by it, a new build taking over with what the person
// set and fading in, a build's problems shown.
import std;
import omui;
import mlibs.testing;
import mcpplibs.mtest;
import mcpplibs.os;

using namespace omui;
using namespace mcpplibs::mtest;

namespace {

// An application under development: its own runtime presenting to the host,
// stepped beside it (a process of its own in omui dev; one thread here, as
// the signal graph is the thread's).
struct Application {
    Runtime                              rt;
    Signal<bool>                         on{false};
    std::optional<agent::Presenter>      presenter;
    bool                                 done = false;
    int                                  frame = 0;

    Application(std::filesystem::path socket, int version)
        : rt([] {
              RuntimeOptions options;
              options.fonts = &fonts();
              options.size = {320, 200};
              return options;
          }()) {
        rt.mount("main", column({.padding = 12, .gap = 8},
                                {text({.id = "version", .text = std::format("Build {}", version)}),
                                 toggle({.id = "dark", .checked = on, .label = "Dark", .on_changed = [this](bool now) { on.set(now); }})}));
        if (auto connected = mcpplibs::os::ipc::connection::connect(socket.parent_path(), mcpplibs::os::fs::utf8(socket.filename())))
            presenter.emplace(rt, std::move(*connected));
        else
            done = true;
    }
    void step() {
        if (done || !presenter) return;
        const FrameOutput output = rt.frame({seconds(++frame / 60.0), presenter->input()});
        if (!presenter->send(output)) done = true;  // the host let it go
    }
    Point where() { const Rect bounds = rt.screen_bounds(rt.find("main/dark")); return {bounds.x + 20, bounds.y + bounds.height / 2}; }

    static mlibs::text::FontCollection& fonts() {
        static mlibs::text::FontCollection collection = [] {
            mlibs::text::FontCollection made;
            (void)made.add_defaults();
            return made;
        }();
        return collection;
    }
};

}  // namespace

UTest cases{"sdk.devhost", [] {
    "drawn in the host's window; a press handled by it; a new build takes what was set and fades in; the old one ends; problems shown"_test = [] {
        register_base_components();
        const auto dir = mlibs::testing::scratch_dir("devhost");
        Headless host(HeadlessOptions{.level = HeadlessLevel::pixels, .size = {320, 200}});
        std::vector<std::unique_ptr<Application>> applications;
        DevHost dev(host.runtime(), {.directory = dir, .start = [&](const std::filesystem::path& socket) {
                                         applications.push_back(std::make_unique<Application>(socket, static_cast<int>(applications.size()) + 1));
                                         return true;
                                     }});
        const auto run_until = [&](auto done) {
            for (int step = 0; step < 2000 && !done(); ++step) {
                for (auto& application : applications) application->step();
                (void)host.step();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return done();
        };
        expect(dev.restart());
        expect(run_until([&] { const Value s = dev.status(); return s.get("shown") && s.get("frames")->as_int() > 0; })) << dev.status().json();
        // Something of it in the host's pixels.
        const auto& pixels = host.pixels();
        long drawn = 0;
        for (int y = 0; y < pixels.height(); ++y)
            for (int x = 0; x < pixels.width(); ++x)
                if (const auto p = pixels.at(x, y); p.r < 200 || p.g < 200 || p.b < 200) ++drawn;
        expect(drawn > 100) << "the application's frame in the host's window";

        // The person presses its switch, in the host's window.
        const Point at = applications.front()->where();
        host.click(at);
        expect(run_until([&] { return applications.front()->on.get(); })) << "handled by the application";

        // A new build: what the person set given to it, faded in, the old one ending.
        expect(dev.restart());
        expect(run_until([&] { const Value s = dev.status(); return s.get("shown") && s.get("shown")->as_int() == 2 && !s.get("fading")->as_bool(); }))
            << dev.status().json();
        expect(run_until([&] { return applications.back()->on.get(); })) << "the switch on in the new build too";
        expect(run_until([&] { return applications.front()->done; })) << "the old one let go";

        // A build that failed: its problems over the application.
        dev.show_problems(problems_in("src/main.cpp:12:7: error: expected ';' before '}' token\nnote: something\n"));
        (void)host.step();
        const auto card = host.tree().node("dev-problems/problem-0");
        expect(card && card->name.contains("src/main.cpp:12:7") && card->name.contains("expected ';'")) << (card ? card->name : std::string("none"));
        dev.show_problems({});
        (void)host.step();
        expect(!host.tree().node("dev-problems/problem-0"));
    };
}};
