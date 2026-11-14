// The components page's wall (the site plan §6, D4): the 36 components on one
// canvas, a band a category, every one in the tree; a card clicked, the
// camera flies to it and it is live — usable where it stands, the same
// state as the library's; its Details open the detail; See all flies back
// out; at rest nothing is drawn. Sampled and saved.
import std;
import omui;
import omui.site;
import mlibs.testing;
import mcpplibs.mtest;

using namespace omui;
using namespace mlibs::testing;
using namespace mcpplibs::mtest;

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
        h.step();
        // What the wall plays by itself, paused: these look at it at rest (live_wall_test watches it play).
        (void)act(path("tools/pause"), "activate");
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

static UTest cases{"site.wall", [] {

    "the 36 components on one wall, a band a category, every one in the tree"_test = [] {
        Page page;
        const auto dir = scratch_dir("wall");
        (void)page.h.save_png(dir / "all.png");
        std::size_t cards = 0, bands = 0;
        const auto depth = std::ranges::count(path("canvas"), '/') + 1;
        for (const auto& node : page.h.tree().snapshot({.root = path("canvas")}).nodes) {
            if (std::ranges::count(node.id, '/') != depth) continue;  // the canvas's items (not what a live card holds)
            const std::string key = node.id.substr(node.id.rfind('/') + 1);
            if (node.role == Role::article && site::find_component(key)) ++cards;
            if (node.role == Role::group) ++bands;
        }
        expect(cards == site::component_catalog.size() && bands == site::component_groups.size()) << std::format("{} cards, {} bands", cards, bands);
        expect(page.zoom() < 0.6) << std::format("fitted: zoom {}", page.zoom());
        const FrameOutput out = page.h.step();
        expect(!out.animating && out.demand.idle()) << std::format("at rest: {} at {} Hz", out.demand.reason, out.demand.max_hz);
    };

    "a card clicked: the camera flies to it, and up close it is the live component, the library's own"_test = [] {
        Page page;
        const auto dir = scratch_dir("wall-flight");
        const double far = page.zoom();
        // The canvas's own activation of a card (a click on it): it flies there.
        expect(page.act(path("canvas"), "focus_node", Value{}.set("key", "toggle")) == 0);
        for (int frame = 0; frame < 6; ++frame) {
            page.h.advance(seconds(0.1));
            (void)page.h.save_png(dir / std::format("{:02}.png", frame));
        }
        page.h.settle(seconds(2));
        (void)page.h.save_png(dir / "near.png");
        expect(page.zoom() > far * 2) << std::format("closer: {} → {}", far, page.zoom());
        std::optional<spec::SemanticNode> control;
        for (const auto& node : page.h.tree().snapshot({.root = path("canvas/toggle")}).nodes)
            if (node.role == Role::switch_) control = node;
        expect(control.has_value()) << "the toggle is live on the wall";
        if (!control) return;
        const bool before = control->states.has(State::checked);
        expect(page.act(control->id, "toggle") == 0);
        page.h.settle();
        const auto after = page.h.tree().node(control->id);
        expect(after && after->states.has(State::checked) != before) << "it works where it stands";
        // Details: the library's detail of it opens.
        expect(page.act(path("canvas/toggle/card/head/details"), "activate") == 0);
        page.h.settle(seconds(1));
        expect(page.h.runtime().route().contains("toggle")) << page.h.runtime().route();
        expect(page.h.tree().node("main/body/view/components/page/gallery/detail/toggle").has_value()) << "its detail shown";
        expect(!page.h.tree().node(path("canvas")).has_value()) << "the page is that component's: the wall aside";
        // Back: the wall again, where it was left (by the toggle, close up).
        expect(page.act("main/body/view/components/page/gallery/detail/directory/all", "activate") == 0);
        page.h.settle(seconds(1));
        expect(page.h.tree().node(path("canvas")).has_value()) << "the wall back";
        expect(page.zoom() > far * 2) << std::format("where it was left: zoom {}", page.zoom());
    };

    "back out to the region; at rest again nothing is drawn"_test = [] {
        Page page;
        const double far = page.zoom();
        expect(page.act(path("canvas"), "focus_node", Value{}.set("key", "slider")) == 0);
        page.h.settle(seconds(2));
        expect(page.zoom() > far * 2);
        // Back to the components' region (its double-click, or its chip): all of them in view again.
        expect(page.act(path("canvas"), "fit", Value{}.set("target", "region-components")) == 0);
        // Back out, the cards go live again (each its component appearing): a moment longer.
        page.h.settle(seconds(3));
        expect(std::abs(page.zoom() - far) < far * 0.05) << std::format("back out: {} (was {})", page.zoom(), far);
        const FrameOutput out = page.h.step();
        expect(!out.animating && out.demand.idle()) << std::format("at rest: {} at {} Hz", out.demand.reason, out.demand.max_hz);
    };

    "a search dims the rest and flies to what it found; nothing found says so; cleared, the whole wall again"_test = [] {
        Page page;
        const double far = page.zoom();
        expect(site::wall_matches("picker") == std::vector<std::string>{"date_picker", "time_picker", "color_picker"}) << "the pickers, in the wall's order";
        expect(site::wall_matches("选择").size() >= 3) << "either language";
        expect(page.act(path("tools/search"), "set_value", Value{}.set("value", "picker")) == 0);
        page.h.settle(seconds(2));
        expect(eq(page.h.tree().node(path("hint"))->name, std::string("3 found.")));
        expect(page.zoom() > far * 1.5) << std::format("closer, on what it found: {} → {}", far, page.zoom());
        (void)page.h.save_png(scratch_dir("wall-search") / "pickers.png");
        expect(page.act(path("tools/search"), "set_value", Value{}.set("value", "no-such-part")) == 0);
        page.h.settle(seconds(1));
        expect(eq(page.h.tree().node(path("hint"))->name, std::string("Nothing matched. Try another word.")));
        expect(page.act(path("tools/search"), "set_value", Value{}.set("value", "")) == 0);
        page.h.settle(seconds(2));
        expect(std::abs(page.zoom() - far) < far * 0.05) << std::format("the whole wall: {} (was {})", page.zoom(), far);
    };

    "a style pack reskins the whole wall, drawn and live cards alike"_test = [] {
        Page page;
        const auto plain = site::frame_hash(page.h.pixels());
        expect(page.act(path("tools/style"), "set_value", Value{}.set("value", "neon")) == 0);
        page.h.settle(seconds(2));
        expect(site::frame_hash(page.h.pixels()) != plain) << "the wall in another style";
        (void)page.h.save_png(scratch_dir("wall-style") / "neon.png");
        expect(page.act(path("tools/style"), "set_value", Value{}.set("value", "plain")) == 0);
        page.h.settle(seconds(2));
        expect(site::frame_hash(page.h.pixels()) == plain) << "plain again: as it was";
    };

    "the keyboard: Tab from card to card in reading order, the camera following, its ring drawn; Enter flies in"_test = [] {
        Page page;
        const double far = page.zoom();
        page.h.runtime().focus(page.h.runtime().find(path("canvas")));
        const auto focused_card = [&] {
            for (const auto& node : page.h.tree().snapshot({.root = path("canvas")}).nodes)
                if (node.states.has(State::focused) && node.role == Role::article) return node.id.substr(node.id.rfind('/') + 1);
            return std::string();
        };
        page.h.key(Key::tab);
        page.h.settle(seconds(2));
        expect(eq(focused_card(), std::string("toggle")));
        page.h.key(Key::tab);
        page.h.settle(seconds(2));
        expect(eq(focused_card(), std::string("checkbox")));
        expect(page.zoom() > far * 2) << std::format("the camera on it: zoom {}", page.zoom());
        (void)page.h.save_png(scratch_dir("wall-keyboard") / "checkbox.png");
        page.h.key(Key::enter);
        page.h.settle(seconds(2));
        expect(eq(focused_card(), std::string("checkbox")));
    };

    "on a phone: a band a category, one above another, each a row of cards swiped sideways; a card tapped opens its detail"_test = [] {
        Headless h{HeadlessOptions{.size = {390, 844}, .scale = 1}};
        auto app = site::app("components", "en");
        h.runtime().set_theme(app.theme);
        app.setup(h.runtime());
        h.mount("main", app.view());
        h.settle(seconds(2));
        std::size_t cards = 0;
        for (const auto& group : site::component_groups) {
            const std::string band = std::format("{}/{}", wall, group.id);
            expect(h.tree().node(band + "/label").has_value()) << band;
            for (const auto& node : h.tree().snapshot({.root = band + "/cards"}).nodes)
                if (node.role == Role::button && site::find_component(node.id.substr(node.id.rfind('/') + 1))) ++cards;
        }
        expect(cards == site::component_catalog.size()) << std::format("{} cards", cards);
        expect(!h.tree().node(path("canvas")).has_value()) << "no canvas on a phone: the page scrolls as a page";
        (void)h.save_png(scratch_dir("wall-phone") / "phone.png");
        Invocation call;
        expect(h.tree().act(std::string(wall) + "/input/cards/row/slider", "activate", call).exit_code() == std::optional<int>(0));
        h.settle(seconds(1));
        expect(h.runtime().route().contains("slider")) << h.runtime().route();
    };
}};
