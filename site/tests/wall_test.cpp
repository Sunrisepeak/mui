// The components page's wall (the site plan §6, D4): the 36 components on one
// canvas, a band a category, every one in the tree; a card clicked, the
// camera flies to it and it is live — usable where it stands, the same
// state as the library's; its Details open the detail; See all flies back
// out; at rest nothing is drawn. Sampled and saved.
import std;
import omui;
import omui.site;
import mlibs.testing;

using namespace omui;
using namespace mlibs::testing;

namespace {
constexpr std::string_view wall = "main/body/view/components/page/wall";
std::string path(std::string_view tail) { return std::string(wall) + "/" + std::string(tail); }

struct Page {
    Headless h{HeadlessOptions{.size = {1440, 900}, .scale = 1}};
    Page() {
        auto app = site::app("components", "en");
        h.runtime().set_theme(app.theme);
        app.setup(h.runtime());
        h.mount("main", app.view());
        h.settle(seconds(2));
    }
    std::int64_t act(std::string_view node, std::string_view action, Value args = {}) {
        Invocation call;
        call.args = std::move(args);
        return h.tree().act(node, action, call).exit_code().value_or(-1);
    }
    double zoom() { return h.runtime().get(h.runtime().find(path("canvas")), "zoom").as_double(); }
};
}  // namespace

int main(int argc, char** argv) {
    Suite suite("site.wall");

    suite.test("the 36 components on one wall, a band a category, every one in the tree", [] {
        Page page;
        const auto dir = scratch_dir("wall");
        (void)page.h.save_png(dir / "all.png");
        std::size_t cards = 0, bands = 0;
        for (const auto& node : page.h.tree().snapshot({.root = path("canvas")}).nodes) {
            if (node.role == Role::article) ++cards;
            if (node.role == Role::group) ++bands;
        }
        check(cards == site::component_catalog.size() && bands == site::component_groups.size(), std::format("{} cards, {} bands", cards, bands));
        check(page.zoom() < 0.6, std::format("fitted: zoom {}", page.zoom()));
        const FrameOutput out = page.h.step();
        check(!out.animating && out.demand.idle(), std::format("at rest: {} at {} Hz", out.demand.reason, out.demand.max_hz));
    });

    suite.test("a card clicked: the camera flies to it, and up close it is the live component, the library's own", [] {
        Page page;
        const auto dir = scratch_dir("wall-flight");
        const double far = page.zoom();
        // The canvas's own activation of a card (a click on it): it flies there.
        check(page.act(path("canvas"), "focus_node", Value{}.set("key", "toggle")) == 0);
        for (int frame = 0; frame < 6; ++frame) {
            page.h.advance(seconds(0.1));
            (void)page.h.save_png(dir / std::format("{:02}.png", frame));
        }
        page.h.settle(seconds(2));
        (void)page.h.save_png(dir / "near.png");
        check(page.zoom() > far * 2, std::format("closer: {} → {}", far, page.zoom()));
        std::optional<spec::SemanticNode> control;
        for (const auto& node : page.h.tree().snapshot({.root = path("canvas/toggle")}).nodes)
            if (node.role == Role::switch_) control = node;
        check(control.has_value(), "the toggle is live on the wall");
        if (!control) return;
        const bool before = control->states.has(State::checked);
        check(page.act(control->id, "toggle") == 0);
        page.h.settle();
        const auto after = page.h.tree().node(control->id);
        check(after && after->states.has(State::checked) != before, "it works where it stands");
        // Details: the library's detail of it opens.
        check(page.act(path("canvas/toggle/card/head/details"), "activate") == 0);
        page.h.settle(seconds(1));
        check(page.h.runtime().route().contains("toggle"), page.h.runtime().route());
        check(page.h.tree().node("main/body/view/components/page/gallery/detail/toggle").has_value(), "its detail shown");
        check(!page.h.tree().node(path("canvas")).has_value(), "the page is that component's: the wall aside");
        // Back: the wall again, where it was left (by the toggle, close up).
        check(page.act("main/body/view/components/page/gallery/detail/directory/all", "activate") == 0);
        page.h.settle(seconds(1));
        check(page.h.tree().node(path("canvas")).has_value(), "the wall back");
        check(page.zoom() > far * 2, std::format("where it was left: zoom {}", page.zoom()));
    });

    suite.test("See all flies back out; at rest again nothing is drawn", [] {
        Page page;
        const double far = page.zoom();
        check(page.act(path("canvas"), "focus_node", Value{}.set("key", "slider")) == 0);
        page.h.settle(seconds(2));
        check(page.zoom() > far * 2);
        check(page.act(path("tools/all"), "activate") == 0);
        page.h.settle(seconds(2));
        check(std::abs(page.zoom() - far) < far * 0.05, std::format("back out: {} (was {})", page.zoom(), far));
        const FrameOutput out = page.h.step();
        check(!out.animating && out.demand.idle(), std::format("at rest: {} at {} Hz", out.demand.reason, out.demand.max_hz));
    });

    return suite.run(argc, argv);
}
