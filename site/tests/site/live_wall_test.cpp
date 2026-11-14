// Everything shown at once, alive (the site plan §7): the compositions and
// the component wall on bounded canvases — all of it at first, live up
// close and readable far off, a few moving at random, the one under the
// pointer moving, a double-click flying to it; the wheel past the wall's
// extent the page's. And Mui playing with the pointer.
import std;
import omui;
import omui.site;
import mlibs.testing;
import mcpplibs.mtest;
import mlibs.hash;
import hanim.engine;

using namespace omui;
using namespace mlibs::testing;
using namespace mcpplibs::mtest;

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

static UTest cases{"site.live_wall", [] {

    "the studio: one canvas, its regions — the components, the works, the compositions, the recipes — each card live; the places arrive at theirs"_test = [] {
        Site s("motion");
        const std::string wall = "main/body/view/explore/page/wall", canvas = wall + "/canvas";
        const Rect view = s.h.runtime().screen_bounds(s.h.runtime().find(canvas));
        expect(view.width > 900 && view.height >= 480) << std::format("a large canvas: {}x{}", view.width, view.height);
        const auto item = [&](std::string_view key) { return s.h.tree().node(std::format("{}/{}", canvas, key)); };
        // Motion arrives at the works: each of them live (its stage is a node).
        expect(s.h.tree().node(canvas + "/work-mark/mark/stage").has_value()) << "the works live where the motion page opens";
        (void)s.h.save_png(media() / "studio-works.png");
        // The region chips fly to the others.
        for (const std::string_view region : {"compositions", "recipes", "components"}) {
            expect(s.h.tree().act(wall + "/tools/region", "set_value", Invocation{.args = Value{}.set("value", std::string(region))}).exit_code() == std::optional<int>(0));
            s.h.settle(seconds(2));
            (void)s.h.save_png(media() / std::format("studio-{}.png", region));
        }
        expect(s.h.tree().node(canvas + "/toggle/card/stage").has_value()) << "the components live at theirs";
        // All twenty compositions and thirty-seven recipes are items of it, wherever the camera is.
        std::size_t compositions = 0, recipes = 0;
        for (const auto& info : site::creative_motions()) compositions += item("composition-" + std::string(info.id)).has_value() ? 1 : 0;
        for (const auto& info : hanim::anim::recipes()) recipes += item("recipe-" + std::string(info.name)).has_value() ? 1 : 0;
        expect(compositions == site::creative_motions().size() && recipes == hanim::anim::recipes().size()) << std::format("{} compositions, {} recipes", compositions, recipes);
        // See all: everything at once.
        expect(s.h.tree().act(wall + "/tools/all", "activate").exit_code() == std::optional<int>(0));
        s.h.settle(seconds(2));
        (void)s.h.save_png(media() / "studio-all.png");
    };

    "the compositions alive at random; the pointer's card moving; a double-click flying to it"_test = [] {
        Site s("motion");
        const std::string wall = "main/body/view/explore/page/wall", canvas = wall + "/canvas";
        expect(s.h.tree().act(wall + "/tools/region", "set_value", Invocation{.args = Value{}.set("value", "compositions")}).exit_code() == std::optional<int>(0));
        s.h.settle(seconds(2));
        const Rect view = s.h.runtime().screen_bounds(s.h.runtime().find(canvas));
        std::set<std::uint64_t> pictures;
        for (int round = 0; round < 4; ++round) {
            s.h.advance(seconds(1.2));
            pictures.insert(s.hash(view));
        }
        expect(pictures.size() >= 2) << std::format("a few moving at random: {} different pictures", pictures.size());
        const auto card = s.h.tree().node(canvas + "/composition-palette-studio");
        expect(card.has_value() && card->bounds.has_value()) << "a card's bounds";
        if (!card || !card->bounds) return;
        const Rect box{card->bounds->x, card->bounds->y, card->bounds->width, card->bounds->height};
        // The pointer on it: it moves at once.
        expect(s.h.tree().act(wall + "/tools/pause", "activate").exit_code() == std::optional<int>(0)) << "the rest paused";
        s.h.settle(seconds(1));
        const auto before = s.hash(box);
        s.h.move(box.center());
        s.h.advance(seconds(0.6));
        expect(s.hash(box) != before) << "the card under the pointer moves";
        // Double-clicked: the camera flies to it, close up.
        s.double_click(box.center());
        s.h.advance(seconds(1.5));
        const auto close = s.h.tree().node(canvas + "/composition-palette-studio");
        expect(close && close->bounds && close->bounds->width > box.width * 1.5) << std::format("zoomed in: {} → {} px across", box.width, close && close->bounds ? close->bounds->width : 0);
        (void)s.h.save_png(media() / "focused.png", view);
    };

    "bounded: the wheel never takes the canvas out of sight; past all of it, the page scrolls; no closer than 2.5×"_test = [] {
        Site s("motion");
        const std::string wall = "main/body/view/explore/page/wall", canvas = wall + "/canvas", page = "main/body/view/explore";
        const Rect view = s.h.runtime().screen_bounds(s.h.runtime().find(canvas));
        const auto across = [&] { return s.h.tree().node(canvas + "/work-mark")->bounds->width; };
        // All of it in view: no farther out than that, and the wheel scrolls the page instead.
        expect(s.h.tree().act(wall + "/tools/all", "activate").exit_code() == std::optional<int>(0));
        s.h.settle(seconds(2));
        const double far = across();
        const double scrolled = s.h.runtime().get(s.h.runtime().find(page), "y").as_double();
        s.h.scroll(view.center(), {0, 240}, false);  // a mouse wheel, outwards
        s.h.settle(seconds(0.5));
        expect(std::abs(across() - far) < 1e-3) << "no farther out than all of it";
        expect(s.h.runtime().get(s.h.runtime().find(page), "y").as_double() != scrolled) << "the page scrolled instead";
        // A trackpad's long swipe: still all of it in sight.
        s.h.scroll(view.center(), {-5000, 0}, true);
        s.h.settle(seconds(0.5));
        const auto first = s.h.tree().node(canvas + "/work-mark");
        expect(first.has_value() && first->bounds && first->bounds->right() > view.x && first->bounds->x < view.right()) << "the canvas still in sight";
        // Zoomed in as far as it goes: a card no more than 2.5 times its size.
        for (int turn = 0; turn < 40; ++turn) s.h.scroll(view.center(), {0, -240}, false);
        s.h.settle(seconds(0.5));
        expect(across() <= 560 * 2.5 + 1) << std::format("no closer than 2.5×: {} px across", across());
    };

    "the component wall: every component live at the zoom that shows them all; a few changing state"_test = [] {
        Site s("components");
        const std::string canvas = "main/body/view/components/page/wall/canvas";
        const Rect view = s.h.runtime().screen_bounds(s.h.runtime().find(canvas));
        std::size_t specimens = 0;
        for (const auto& node : s.h.tree().snapshot({.root = canvas}).nodes)
            if (node.id.ends_with("/card/stage")) ++specimens;
        expect(specimens >= 30) << std::format("{} live specimens at the overview", specimens);
        (void)s.h.save_png(media() / "components.png", view);
        std::set<std::uint64_t> pictures;
        for (int round = 0; round < 4; ++round) {
            s.h.advance(seconds(1));
            pictures.insert(s.hash(view));
        }
        expect(pictures.size() >= 2) << std::format("alive: {} different pictures", pictures.size());
    };

    "Mui and the pointer: near, it notices it and turns to it; resting on the page, it stalks it"_test = [] {
        Site s("home");
        s.h.settle(seconds(4));
        const auto mui = s.h.tree().node("main/body/view/mui");
        expect(mui.has_value() && mui->bounds.has_value()) << "Mui on the home";
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
        expect(state() == "noticing" || state() == "stalking" || state() == "walking") << state();
        // Resting there: it stalks it, coming closer.
        s.h.advance(seconds(3));
        const Point after = feet();
        expect(std::abs(after.x - near.x) < std::abs(at.x - near.x) - 40) << std::format("closer: {} → {} (pointer at {})", at.x, after.x, near.x);
        (void)s.h.save_png(media() / "mui-stalks.png");
    };
}};
