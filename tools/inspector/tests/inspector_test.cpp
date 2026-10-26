// The Inspector, against a real application over its agent endpoint: the
// tree, a node's details, frame times, the pipeline, and the application's
// clock paused and stepped from the Inspector's buttons.
import std;
import openmui;
import openmui.inspector;
import openmui.host.headless;
import openmui.testing;

using namespace omui::testing;
using namespace omui;

namespace {

struct Target {
    Signal<bool> wide = signal(false);
    View view() {
        auto w = wide;
        return column({.padding = 12, .gap = 8},
                      {
                          text({.id = "title", .text = "Target", .style = "title"}),
                          button({.id = "go", .text = "Go", .on_activated = [w] { w.set(!w.peek()); }}),
                          box({.id = "bar", .width = Prop<Dim>(Source{std::nullopt, [w] { return Value(w.get() ? 300.0 : 100.0); }, {}}), .height = 12,
                               .background = "accent", .radius = 6, .animate = {{"width", Transition::tween(seconds(1), anim::ease::linear)}}}),
                      });
    }
};

// Steps both hosts until `done` or a few seconds of wall time pass.
template <class F>
bool pump(Headless& a, Headless& b, F done) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < until) {
        a.step();
        b.step();
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"inspector"};

    s.test("inspects a running application over its endpoint, and drives its clock", [] {
        register_base_components();
        const auto dir = scratch_dir("inspector-endpoints");
        HeadlessOptions to;
        to.level = HeadlessLevel::session;
        to.size = {400, 200};
        to.app = "org.openmui.test.target";
        to.endpoint_directory = dir;
        Headless target(to);
        if (!target.endpoint()) {
            // Wine lacks AF_UNIX on its drives; Windows 10 and later, Linux and macOS have local endpoints.
            std::println("    skipped: local endpoints are not supported here");
            return;
        }
        Target t;
        target.mount("main", t.view());
        target.settle();

        HeadlessOptions io;
        io.size = {1280, 800};
        io.app = "org.openmui.inspector";
        Headless ui(io);
        inspector::Inspector insp(ui.runtime(), {.app = "", .directory = dir, .poll = seconds(0.05)});
        ui.mount("main", insp.view());

        check(pump(target, ui, [&] { return insp.tree_size.peek() >= 3; }), std::format("the tree arrives ({} nodes; {})", insp.tree_size.peek(), insp.status.peek()));
        check(insp.app.peek() == "org.openmui.test.target", "it chose the only other application");
        check(insp.applications.peek() == std::vector<std::string>{"org.openmui.test.target"}, "and never itself");

        // A node's details.
        insp.select("main/go");
        check(pump(target, ui, [&] { return insp.details.peek().get("type") != nullptr; }),
              std::format("details: {}; selected {}; status {}", insp.details.peek().json().substr(0, 200), insp.selected.peek(), insp.status.peek()));
        check(insp.details.peek().get("type")->as_string() == "button" && insp.details.peek().get("source")->as_string().contains("inspector_test.cpp"),
              insp.details.peek().json().substr(0, 200));

        // Frames and the pipeline arrive on their own.
        check(pump(target, ui, [&] {
            const Value* h = insp.stats.peek().get("history");
            return h && h->list() && !h->list()->empty() && insp.pipeline.peek().get("stages");
        }));
        const Value* stages = insp.pipeline.peek().get("stages");
        check(stages && stages->list() && stages->list()->size() == 8);
        insp.tab.set("pipeline");
        ui.step();
        check(ui.tree().node("main/body/panels/panel") != std::nullopt);

        // Pause from the Inspector's button: the target's time stops.
        check(ui.click("main/clock/pause"));
        check(pump(target, ui, [&] { return target.runtime().time_state().paused; }), "paused from the Inspector");
        t.wide.set(true);  // a transition that cannot move while paused
        const seconds frozen = target.runtime().now();
        for (int i = 0; i < 5; ++i) target.step();
        check(target.runtime().now() == frozen && target.runtime().bounds(target.runtime().find("main/bar")).width == 100, "frozen");
        check(ui.click("main/clock/forward"));  // +1 frame
        check(pump(target, ui, [&] { return target.runtime().now() > frozen; }), "stepped by one frame");
        check(std::abs((target.runtime().now() - frozen).count() - 1.0 / 60.0) < 1e-9);
        // Overlays on the target from the Inspector's toggles.
        check(ui.click("main/clock/bounds"));
        check(pump(target, ui, [&] { return target.runtime().debug().bounds; }), "bounds overlay turned on remotely");
        // Back to live time.
        check(ui.click("main/clock/release"));
        check(pump(target, ui, [&] { return !target.runtime().time_state().controlled; }));
        check(ui.save_png(scratch_dir("inspector") / "inspector.png").has_value());
        check(ui.tree().violations().empty(), "the Inspector's own tree is clean");
    });

    return s.run(argc, argv);
}
