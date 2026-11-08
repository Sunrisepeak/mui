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

    return suite.run(argc, argv);
}
