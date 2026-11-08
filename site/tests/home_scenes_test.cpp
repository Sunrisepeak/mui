// The home's scenes under its opening (the site plan §5, D3c): each a heading
// and one line, then the thing itself — the whole page switched to its
// structure and back; a line changing language, the letters both share kept
// where they are; a soft card let go, its time dragged back to a step drawn
// exactly as it was; the page saved as SVG, its shapes named by their nodes.
// Sampled over time and saved.
import std;
import omui;
import omui.site;
import mlibs.testing;

using namespace omui;
using namespace mlibs::testing;

namespace {
constexpr std::string_view root = "main/body/view/home/page/scenes";
std::string path(std::string_view tail) { return std::string(root) + "/" + std::string(tail); }

struct Home {
    Headless h{HeadlessOptions{.size = {1280, 3000}, .scale = 1}};
    explicit Home(std::string_view lang = "en") {
        auto app = site::app("home", lang);
        h.runtime().set_theme(app.theme);
        app.setup(h.runtime());
        h.mount("main", app.view());
        h.settle(seconds(0.9));
    }
    std::optional<spec::SemanticNode> node(std::string_view tail) { return h.tree().node(path(tail)); }
    bool visible(std::string_view id) {
        const auto n = h.tree().node(id);
        return n && !n->states.has(State::hidden);
    }
    std::int64_t act(std::string_view tail, std::string_view action = "activate", Value args = {}) {
        Invocation request;
        request.args = std::move(args);
        return h.tree().act(path(tail), action, request).exit_code().value_or(-1);
    }
    Value card() { return node("soft/shown/card")->value; }
    std::string card_text(std::string_view key) {
        const Value now = card();
        return std::string(now.get(key)->as_string());
    }
    double card_number(std::string_view key) {
        const Value now = card();
        const Value* v = now.get(key);
        return v->type() == Value::Type::integer ? static_cast<double>(v->as_int()) : v->as_double();
    }
    void run(double seconds_) {
        for (int i = 0; i < static_cast<int>(seconds_ * 60); ++i) h.step();
    }
};
}  // namespace

int main(int argc, char** argv) {
    Suite suite("site.home_scenes");

    suite.test("four scenes under the opening, each a heading and one line, then the thing itself", [] {
        Home home;
        for (const auto& [id, heading] : {std::pair{"views", "One tree, every view"}, std::pair{"glyphs", "Only what changed moves"},
                                          std::pair{"soft", "Drop it. Then pull time back."}, std::pair{"vectors", "Any moment, as vectors"}}) {
            const auto title = home.node(std::format("{}/heading", id));
            check(title && title->role == Role::heading && title->name == heading, std::format("{}: {}", id, title ? title->name : "none"));
            std::size_t words = 0;
            for (const auto& node : home.h.tree().snapshot({.root = path(id)}).nodes)
                if (node.role == Role::text || node.role == Role::heading)
                    if (!node.id.contains("shown")) ++words;
            check(words <= 2, std::format("{}: {} texts beside what it shows", id, words));
        }
        Home zh("zh");
        check(zh.node("glyphs/heading")->name == "只动变了的部分");
        (void)home.h.save_png(scratch_dir("home-scenes") / "scenes-en.png");
    });

    suite.test("① the whole page as its structure and back, the way back shown", [] {
        Home home;
        check(!home.visible("main/body/view/wireframe"), "pixels at first");
        check(home.act("views/lens", "set_value", Value{}.set("value", "tree")) == 0);
        home.h.settle(seconds(1));
        check(home.visible("main/body/view/wireframe"), "the page as its structure");
        check(home.visible("main/foot/lens_pixels"), "the foot's lenses shown: the way back");
        check(home.h.tree().act("main/foot/lens_pixels", "activate").exit_code() == std::optional<int>(0));
        home.h.settle(seconds(1));
        check(!home.visible("main/body/view/wireframe"), "pixels again");
    });

    suite.test("② the line in the other language: the letters both share stay put, the rest give way", [] {
        Home home;
        check_eq(home.node("glyphs/shown/words")->name, std::string("omui draws every glyph"));
        check(home.act("glyphs/shown/switch") == 0);
        const auto dir = scratch_dir("home-scenes-glyphs");
        std::size_t kept = 0;
        bool moved = false;
        for (int frame = 0; frame < 40; ++frame) {
            home.h.step();
            for (const auto& motion : home.h.runtime().motions())
                if (motion.path.ends_with("glyphs/shown/words")) moved = true, kept = std::max(kept, motion.glyphs_kept);
            if (frame % 6 == 0) (void)home.h.save_png(dir / std::format("{:02}.png", frame / 6));
        }
        check(moved, "it changed by a motion");
        check(kept >= 4, std::format("o, m, u, i kept: {}", kept));
        home.h.settle(seconds(2));
        check_eq(home.node("glyphs/shown/words")->name, std::string("omui 绘制每一个字形"));
        check_eq(home.node("glyphs/shown/switch")->name, std::string("English"));
    });

    suite.test("③ let go, the card lands; the time dragged back draws a step as it was; played on, the same end", [] {
        Home home;
        check_eq(home.card_text("state"), std::string("waiting"));
        check(home.act("soft/shown/controls/drop") == 0);
        std::map<std::int64_t, std::string> hashes;
        for (int frame = 0; frame < 240 && home.card_text("state") != "resting"; ++frame) {
            home.h.step();
            hashes[static_cast<std::int64_t>(home.card_number("step"))] = home.card_text("hash");
        }
        check_eq(home.card_text("state"), std::string("resting"));
        const std::string end = home.card_text("hash");
        const Value slider = home.node("soft/shown/controls/time")->value;
        check(std::abs(slider.get("value")->as_double() - home.card_number("at")) < 1e-6, "the slider follows its time");
        (void)home.h.save_png(scratch_dir("home-scenes-soft") / "rest.png");
        // The slider dragged back (its own action, as a person's drag ends).
        check(home.act("soft/shown/controls/time", "set_value", Value{}.set("value", 0.8)) == 0);
        home.h.step();
        home.h.step();
        check(home.card_text("state") == "held" && home.card_number("step") == 96, std::format("{} at step {}", home.card_text("state"), home.card_number("step")));
        if (hashes.contains(96)) check_eq(home.card_text("hash"), hashes[96]);
        check(home.act("soft/shown/controls/play") == 0);
        home.run(3);
        check_eq(home.card_text("hash"), end);
    });

    suite.test("④ the page saved as SVG: a file given to the person, its shapes named by their nodes", [] {
        Home home;
        check(home.act("vectors/shown/save") == 0);
        home.h.step();
        const auto& files = home.h.runtime().offered_files();
        check(files.size() == 1, std::format("{} files offered", files.size()));
        if (files.empty()) return;
        const auto& file = files.back();
        check(file.name == "omui-home.svg" && file.type == "image/svg+xml" && file.bytes.starts_with("<svg"), file.name);
        check(file.bytes.contains(R"(data-node="main/body/view/home/page/scenes/vectors/shown/save")"), "the button that saved it, named in it");
        check(file.bytes.contains(R"(data-node="main/body/view/home/page/landing/intro/title")"), "the home's title, named");
        const std::string said = home.node("vectors/shown/saved")->name;
        check(said.starts_with("Saved omui-home.svg: ") && said.contains("shapes, each named by the node that drew it"), said);
        std::ofstream(scratch_dir("home-scenes-svg") / "omui-home.svg", std::ios::binary) << file.bytes;
    });

    return suite.run(argc, argv);
}
