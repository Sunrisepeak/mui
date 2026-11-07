import std;
import omui;
import omui.site;
import mlibs.testing;

using namespace omui;
using namespace mlibs::testing;

namespace {
constexpr std::string_view root = "main/body/view/agent/page/agent_feature";
std::string path(std::string_view tail) { return std::string(root) + "/" + std::string(tail); }
struct Page {
    Headless h{HeadlessOptions{.size = {1280, 800}}};
    Page() {
        auto app = site::app("agent/use");
        h.runtime().set_theme(app.theme); app.setup(h.runtime());
        h.mount("main", app.view()); (void)h.step();
    }
    bool reviewed() {
        const auto n = h.tree().node(path("specimen/reviewed"));
        return n && n->states.has(State::checked);
    }
};
}

int main(int argc, char** argv) {
    Suite suite("site.agent_guard");
    suite.test("input reaching the specimen cancels an already-due preview before timers", [] {
        Page page;
        page.h.advance(seconds(0.2));
        const auto order = page.h.runtime().frame_pipeline().order();
        check(!order.empty() && order.front() == "preview_input_guard");
        const auto due = page.h.runtime().next_timer(); check(due.has_value()); if (!due) return;
        while (page.h.now() + seconds(1.0 / 60) < *due) (void)page.h.step();
        check(!page.reviewed(), "the preview's first action has not run");
        const auto specimen = page.h.runtime().find(path("specimen"));
        const auto at = page.h.runtime().screen_bounds(specimen).center();
        (void)page.h.step({PointerEvent{.kind = PointerKind::move, .position = at}});
        page.h.advance(seconds(2));
        check(!page.reviewed(), "the due task did not run ahead of pointer entry");
        check(std::ranges::none_of(page.h.runtime().action_journal(), [](const auto& entry) {
            return entry.actor == "agent:agent-feature-simulated" && entry.path.ends_with("/reviewed");
        }));
    });
    suite.test("window blur cancels pending preview actions before the timer stage", [] {
        Page page;
        page.h.advance(seconds(0.2));
        const auto due = page.h.runtime().next_timer(); check(due.has_value()); if (!due) return;
        while (page.h.now() + seconds(1.0 / 60) < *due) (void)page.h.step();
        (void)page.h.step({FocusEvent{false}});
        page.h.advance(seconds(2));
        check(!page.reviewed(), "hidden task remains untouched");
        (void)page.h.step({FocusEvent{true}});
        check(page.h.runtime().valid(page.h.runtime().find(path("use/actions/play"))), "explicit task controls remain available");
    });
    suite.test("releasing Enter does not revoke an explicitly started keyboard task", [] {
        Page page;
        const auto stage = page.h.runtime().find(path("specimen"));
        page.h.move(page.h.runtime().screen_bounds(stage).center());
        page.h.move({2, 2});
        const auto play = page.h.runtime().find(path("use/actions/play"));
        page.h.runtime().focus_visibly(play); (void)page.h.step();
        (void)page.h.step({KeyEvent{.kind = KeyKind::down, .key = Key::enter}});
        (void)page.h.step({KeyEvent{.kind = KeyKind::up, .key = Key::enter}});
        page.h.advance(seconds(2.5));
        const auto receipt = page.h.tree().node(path("specimen/receipt"));
        check(receipt && receipt->name.contains("Recorded here"), "the key-up in a later frame preserves the requested task");
    });
    return suite.run(argc, argv);
}
