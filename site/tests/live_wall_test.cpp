// Everything shown at once, alive (the site plan §7): the compositions and
// the component wall on bounded canvases — all of it at first, live up
// close and readable far off, a few moving at random, the one under the
// pointer moving, a double-click flying to it; the wheel past the wall's
// extent the page's. And Mui playing with the pointer.
import std;
import omui;
import omui.site;
import mlibs.testing;
import mlibs.hash;

using namespace omui;
using namespace mlibs::testing;

namespace {

struct Site {
    Headless h;
    explicit Site(std::string_view route, float width = 1280, float height = 900) : h(HeadlessOptions{.size = {width, height}, .scale = 1, .parallel_raster = false}) {
        auto app = site::app(route, "en");
        h.runtime().set_theme(app.theme);
        app.setup(h.runtime());
        for (auto& verb : app.verbs) (void)h.runtime().verbs().add(verb.desc, verb.fn);
        h.mount("main", app.view());
        h.settle(seconds(2));
    }
    std::uint64_t hash(Rect region) {
        const auto png = h.png(region);
        if (!png) return 0;
        mlibs::hash::Fnv1a fnv;
        fnv.bytes(std::as_bytes(std::span(*png)));
        return fnv.value();
    }
    double number(std::string_view id, std::string_view prop) { return h.runtime().get(h.runtime().find(id), prop).as_double(); }
    void double_click(Point at) {
        PointerEvent down{.kind = PointerKind::down, .position = at, .button = MouseButton::left, .buttons = 1, .clicks = 2};
        PointerEvent up{.kind = PointerKind::up, .position = at, .button = MouseButton::left, .clicks = 2};
        (void)h.step({down, up});
    }
};

const std::filesystem::path& media() {
    static const auto dir = scratch_dir("live-wall");
    return dir;
}

}  // namespace

int main(int argc, char** argv) {
    Suite suite("site.live_wall");

    suite.test("the compositions all at once on a canvas: alive at random, the pointer's card moving, a double-click flying to it", [] {
        Site s("motion");
        const std::string wall = "main/body/view/explore/page/wall", canvas = wall + "/canvas";
        check(s.h.tree().act(wall, "scroll_into_view").exit_code() == std::optional<int>(0), "the wall on screen");
        s.h.settle(seconds(1));
        const Rect view = s.h.runtime().screen_bounds(s.h.runtime().find(canvas));
        check(view.width > 900 && view.height > 500, "a large canvas");
        // All twenty there, each live (its scene's own nodes), at the zoom that shows them all.
        std::size_t live = 0;
        for (const auto& info : site::creative_motions())
            live += s.h.tree().node(std::format("{}/{}", canvas, info.id)).has_value() ? 1 : 0;
        check(live == site::creative_motions().size(), std::format("{} of {} cards stated", live, site::creative_motions().size()));
        (void)s.h.save_png(media() / "compositions.png", view);
        // Alive on its own: the canvas does not stay the same picture.
        std::set<std::uint64_t> pictures;
        for (int round = 0; round < 4; ++round) {
            s.h.advance(seconds(1.2));
            pictures.insert(s.hash(view));
        }
        check(pictures.size() >= 2, std::format("a few moving at random: {} different pictures", pictures.size()));
        // The pointer on a card: that card moves at once.
        const auto card = s.h.tree().node(canvas + "/palette-studio");
        check(card.has_value() && card->bounds.has_value(), "a card's bounds");
        if (!card || !card->bounds) return;
        const Rect box{card->bounds->x, card->bounds->y, card->bounds->width, card->bounds->height};
        s.h.settle(seconds(0.1));
        const auto before = s.hash(box);
        s.h.move(box.center());
        s.h.advance(seconds(0.6));
        check(s.hash(box) != before, "the card under the pointer moves");
        // Double-clicked: the camera flies to it, close up.
        s.double_click(box.center());
        s.h.advance(seconds(1.5));
        const auto close = s.h.tree().node(canvas + "/palette-studio");
        check(close && close->bounds && close->bounds->width > box.width * 1.5, std::format("zoomed in: {} → {} px across", box.width, close && close->bounds ? close->bounds->width : 0));
        (void)s.h.save_png(media() / "focused.png", view);
    });

    suite.test("bounded: the wheel never takes the wall out of sight; past all of it, the page scrolls", [] {
        Site s("motion");
        const std::string wall = "main/body/view/explore/page/wall", canvas = wall + "/canvas", page = "main/body/view/explore";
        (void)s.h.tree().act(wall, "scroll_into_view");
        s.h.settle(seconds(1));
        const Rect view = s.h.runtime().screen_bounds(s.h.runtime().find(canvas));
        const auto zoom = [&] { return s.h.tree().node(canvas + "/" + std::string(site::creative_motions().front().id))->bounds->width; };
        const double before = zoom();
        const double scrolled = s.h.runtime().get(s.h.runtime().find(page), "y").as_double();
        s.h.scroll(view.center(), {0, 240}, false);  // a mouse wheel, outwards
        s.h.settle(seconds(0.5));
        check(std::abs(zoom() - before) < 1e-3, "no farther out than all of it");
        check(s.h.runtime().get(s.h.runtime().find(page), "y").as_double() != scrolled, "the page scrolled instead");
        // A trackpad's long swipe: the camera stops at the wall's edge.
        s.h.scroll(view.center(), {-5000, 0}, true);
        s.h.settle(seconds(0.5));
        const auto first = s.h.tree().node(canvas + "/" + std::string(site::creative_motions().front().id));
        check(first.has_value() && first->bounds && first->bounds->right() > view.x, "the wall still in sight");
    });

    suite.test("the component wall: every component live at the zoom that shows them all; a few changing state", [] {
        Site s("components");
        const std::string canvas = "main/body/view/components/page/wall/canvas";
        const Rect view = s.h.runtime().screen_bounds(s.h.runtime().find(canvas));
        std::size_t specimens = 0;
        for (const auto& node : s.h.tree().snapshot({.root = canvas}).nodes)
            if (node.id.ends_with("/card/stage")) ++specimens;
        check(specimens >= 30, std::format("{} live specimens at the overview", specimens));
        (void)s.h.save_png(media() / "components.png", view);
        std::set<std::uint64_t> pictures;
        for (int round = 0; round < 4; ++round) {
            s.h.advance(seconds(1));
            pictures.insert(s.hash(view));
        }
        check(pictures.size() >= 2, std::format("alive: {} different pictures", pictures.size()));
    });

    suite.test("Mui and the pointer: near, it notices it and turns to it; resting on the page, it stalks it", [] {
        Site s("home");
        s.h.settle(seconds(4));
        const auto mui = s.h.tree().node("main/body/view/mui");
        check(mui.has_value() && mui->bounds.has_value(), "Mui on the home");
        if (!mui || !mui->bounds) return;
        const auto feet = [&] {
            const Value now = s.h.tree().node("main/body/view/mui")->value;
            const auto& list = *now.get("feet")->list();
            const auto number = [](const Value& v) { return v.type() == Value::Type::integer ? static_cast<double>(v.as_int()) : v.as_double(); };
            return Point{static_cast<float>(mui->bounds->x + number(list[0])), static_cast<float>(mui->bounds->y + number(list[1]))};
        };
        const auto state = [&] { return std::string(s.h.tree().node("main/body/view/mui")->value.get("state")->as_string()); };
        const Point at = feet();
        // Near, on nothing that takes the pointer: it notices.
        const Point near{at.x - 150, at.y - 60};
        s.h.move(near);
        s.h.advance(seconds(0.2));
        check(state() == "noticing" || state() == "stalking" || state() == "walking", state());
        // Resting there: it stalks it, coming closer.
        s.h.advance(seconds(3));
        const Point after = feet();
        check(std::abs(after.x - near.x) < std::abs(at.x - near.x) - 40, std::format("closer: {} → {} (pointer at {})", at.x, after.x, near.x));
        (void)s.h.save_png(media() / "mui-stalks.png");
    });

    return suite.run(argc, argv);
}
