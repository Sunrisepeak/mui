// The motion page's signature works (the site plan §7.1, E1): each a hanim
// recipe on a stage whose time is yours — made, its length said (the
// slider's end); played to its end, where it holds; any moment sought drawn
// the same each time, forwards or back. Sampled over time and saved.
import std;
import omui;
import omui.site;
import mlibs.testing;
import mlibs.hash;

using namespace omui;
using namespace mlibs::testing;

namespace {
constexpr std::string_view root = "main/body/view/explore/page/works/grid";
std::string path(std::string_view tail) { return std::string(root) + "/" + std::string(tail); }

struct Page {
    Headless h{HeadlessOptions{.size = {1280, 2400}, .scale = 1}};
    Page() {
        auto app = site::app("explore", "en");
        h.runtime().set_theme(app.theme);
        app.setup(h.runtime());
        h.mount("main", app.view());
        h.settle(seconds(2));
    }
    Value value(std::string_view tail) { return h.tree().node(path(tail))->value; }
    double number(std::string_view tail, std::string_view key) {
        const Value v = value(tail);
        const Value* n = v.get(key);
        return n->type() == Value::Type::integer ? static_cast<double>(n->as_int()) : n->as_double();
    }
    std::int64_t act(std::string_view tail, std::string_view action, Value args = {}) {
        Invocation call;
        call.args = std::move(args);
        return h.tree().act(path(tail), action, call).exit_code().value_or(-1);
    }
    std::uint64_t stage_pixels(std::string_view tail) {
        const Rect box = h.runtime().screen_bounds(h.runtime().find(path(tail)));
        const auto& px = h.pixels();
        mlibs::hash::Fnv1a hash;
        for (int y = static_cast<int>(box.y); y < static_cast<int>(box.bottom()); ++y)
            hash.bytes(std::as_bytes(std::span(px.row(y) + static_cast<int>(box.x), static_cast<std::size_t>(box.width))));
        return hash.value();
    }
};
}  // namespace

int main(int argc, char** argv) {
    Suite suite("site.works");

    for (const auto& [id, work] : {std::pair{"mark", "mark-writing"}, std::pair{"gather", "mui-gather"}}) {
        suite.test(std::format("{}: made with its length; played to its end, it holds; any moment the same each time; the strip saved", work), [id, work] {
            Page page;
            const std::string stage = std::format("{}/stage", id);
            check_eq(std::string(page.value(stage).get("work")->as_string()), std::string(work));
            const double length = page.number(stage, "length");
            check(length > 0.5, std::format("its length: {}", length));
            const Value slider = page.h.tree().node(path(std::format("{}/controls/time", id)))->value;
            check(std::abs(slider.get("max")->as_double() - length) < 1e-6, "the slider ends where it does");
            check(page.act(std::format("{}/controls/play", id), "activate") == 0);
            const auto dir = scratch_dir(std::format("works-{}", id));
            for (int frame = 0; frame < 8; ++frame) {
                page.h.advance(seconds(length / 8));
                (void)page.h.save_png(dir / std::format("{:02}.png", frame), page.h.runtime().screen_bounds(page.h.runtime().find(path(stage))));
            }
            page.h.settle(seconds(1));
            check(!page.value(stage).get("playing")->as_bool() && std::abs(page.number(stage, "at") - length) < 1e-6, std::format("held at its end: {}", page.number(stage, "at")));
            // A moment sought twice, from either side: the same pixels.
            check(page.act(stage, "seek", Value{}.set("at", length * 0.4)) == 0);
            page.h.settle();
            const auto once = page.stage_pixels(stage);
            check(page.act(stage, "seek", Value{}.set("at", length * 0.9)) == 0);
            page.h.settle();
            const auto later = page.stage_pixels(stage);
            check(page.act(stage, "seek", Value{}.set("at", length * 0.4)) == 0);
            page.h.settle();
            check(page.stage_pixels(stage) == once && once != later, "the same moment, the same pixels; another moment, others");
            check(page.act(stage, "seek", Value{}.set("at", length * 2)) != 0, "not past its end");
        });
    }

    suite.test("a soft landing, rewound: dropped, it rests; its slider follows; dragged back, held", [] {
        Page page;
        check(page.act("soft/controls/drop", "activate") == 0);
        page.h.advance(seconds(4));
        check_eq(std::string(page.value("soft/stage").get("state")->as_string()), std::string("resting"));
        const Value slider = page.h.tree().node(path("soft/controls/time"))->value;
        check(std::abs(slider.get("value")->as_double() - 3.0) < 0.02, "the slider at its end");
        check(page.act("soft/controls/time", "set_value", Value{}.set("value", 1.0)) == 0);
        page.h.step();
        page.h.step();
        check_eq(std::string(page.value("soft/stage").get("state")->as_string()), std::string("held"));
    });

    suite.test("only what changed moves: the line in the other language, the shared letters kept", [] {
        Page page;
        check(page.act("letters/controls/switch", "activate") == 0);
        std::size_t kept = 0;
        for (int frame = 0; frame < 30; ++frame) {
            page.h.step();
            for (const auto& motion : page.h.runtime().motions())
                if (motion.path.ends_with("letters/stage/words")) kept = std::max(kept, motion.glyphs_kept);
        }
        check(kept >= 4, std::format("o, m, u, i kept: {}", kept));
        page.h.settle(seconds(2));
        check_eq(page.h.tree().node(path("letters/stage/words"))->name, std::string("omui 绘制每一个字形"));
    });

    suite.test("a list becomes cards: each item travels to its place, through the places between", [] {
        Page page;
        // Where it is drawn (a travel is drawn; the layout says where it ends at once).
        const auto x_of = [&](std::string_view item) { return page.h.runtime().drawn_bounds(page.h.runtime().find(path(std::format("layout/stage/items/{}", item)))).x; };
        const float list_x = x_of("item2");
        check(page.act("layout/controls/as", "set_value", Value{}.set("value", "cards")) == 0);
        std::vector<float> path_x;
        const auto dir = scratch_dir("works-layout");
        for (int frame = 0; frame < 24; ++frame) {
            page.h.step();
            path_x.push_back(x_of("item2"));
            if (frame % 4 == 0) (void)page.h.save_png(dir / std::format("{:02}.png", frame / 4), page.h.runtime().screen_bounds(page.h.runtime().find(path("layout/stage"))));
        }
        page.h.settle(seconds(2));
        const float card_x = x_of("item2");
        check(card_x > list_x + 100, std::format("the third item moved right: {} → {}", list_x, card_x));
        const auto between = std::ranges::count_if(path_x, [&](float x) { return x > list_x + 5 && x < card_x - 5; });
        check(between >= 3, std::format("{} frames between its places (travelled, not cut)", between));
    });

    return suite.run(argc, argv);
}
