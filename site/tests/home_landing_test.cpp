// The opening is an actual object, shared by pixels, its semantic tree and
// a visibly simulated agent. The small and large mark share the vector original.
import std;
import omui;
import omui.site;
import mlibs.testing;
import mcpplibs.mtest;

using namespace omui;
using namespace mlibs::testing;
using namespace mcpplibs::mtest;

namespace {
constexpr std::string_view root = "main/body/view/home/page/landing";
std::string path(std::string_view tail) { return std::string(root) + "/" + std::string(tail); }
const std::filesystem::path& media() { static const auto dir = scratch_dir("home-landing"); return dir; }

struct Home {
    Headless h;
    explicit Home(float width = 1280, std::string_view lang = "en", bool reduced = false)
        : h(HeadlessOptions{.size = {width, 844}, .reduce_motion = reduced}) {
        auto app = site::app("home", lang);
        h.runtime().set_theme(app.theme); app.setup(h.runtime());
        h.mount("main", app.view()); h.settle(seconds(0.9));
    }
    std::string name(std::string_view tail) { const auto node = h.tree().node(path(tail)); return node ? node->name : std::string(); }
    void act(std::string_view tail, std::string_view action = "activate", Value args = {}) {
        Invocation request; request.args = std::move(args);
        expect(h.tree().act(path(tail), action, request).exit_code() == std::optional<int>(0)) << tail;
        h.settle(seconds(0.8));
    }
    double x() { return h.runtime().get(h.runtime().find(path("scene/visual/mark/letter_m")), "translate_x").as_double(); }
};
}  // namespace

UTest cases{"site.home_landing", [] {
    "a short opening has one heading, one real object and two clear destinations"_test = [] {
        Home home;
        expect(home.name("intro/title") == "One interface. Every perspective.");
        std::size_t headings = 0, images = 0;
        for (const auto& node : home.h.tree().snapshot({.root = std::string(root)}).nodes) {
            if (node.role == Role::heading) ++headings;
            if (node.role == Role::image) ++images;
            expect(!node.id.contains("tasks") && !node.id.contains("json") && !node.id.contains("workbench"));
        }
        expect(eq(headings, std::size_t{1})); expect(eq(images, std::size_t{4}));
        expect(eq(site::brand_geometry().size(), std::size_t{5}));
        expect(home.name("intro/actions/explore") == "Explore components");
        expect(home.name("intro/actions/creative") == "See the motion");
        expect(home.h.save_png(media() / "home-en-desktop.png").has_value());
    };

    "the small and large animated marks use the exact original and settle"_test = [] {
        for (const float size : {32.0f, 248.0f}) {
            Headless still(HeadlessOptions{.size = {300, 300}}), moving(HeadlessOptions{.size = {300, 300}});
            auto unfolded = signal(false);
            still.mount("sheet", column({}, {site::brand_mark("mark", size)}));
            moving.mount("sheet", column({}, {site::animated_brand_mark("mark", size, unfolded)}));
            still.settle(); moving.settle();
            expect(eq(site::frame_hash(still.pixels()), site::frame_hash(moving.pixels())));
            const auto before = site::frame_hash(moving.pixels());
            unfolded.set(true); moving.step(); moving.advance(seconds(0.1));
            const auto during = site::frame_hash(moving.pixels());
            moving.settle(); const auto after = site::frame_hash(moving.pixels());
            expect(before != after && during != before && during != after) << "runtime motion produces actual intermediate drawings";
            expect(moving.runtime().frame_demand().idle()) << "the mark has no permanent decorative timer";
            if (size == 32) expect(std::abs(moving.runtime().get(moving.runtime().find("sheet/mark/letter_m"), "translate_x").as_double()) < 2);
        }
    };

    "structure reads actual named nodes; the simulated agent operates the same button"_test = [] {
        Home home;
        home.act("scene/perspective", "set_value", Value{}.set("value", "structure"));
        std::string names;
        for (const auto& node : home.h.tree().snapshot({.root = path("scene/visual")}).nodes) if (node.role == Role::image) {
            if (!names.empty()) names += " · "; names += node.name;
        }
        expect(home.name("scene/caption").starts_with(names) && !names.empty());
        home.act("scene/controls/assemble");
        expect(home.name("scene/caption").ends_with("Apart"));
        const auto position = home.x();
        home.act("scene/perspective", "set_value", Value{}.set("value", "agent"));
        expect(home.name("scene/caption").contains("simulated"));
        home.act("scene/controls/delegate");
        expect(home.x() != position) << "the agent changed the actual mark model";
        expect(home.name("scene/status").contains("simulated agent acted"));
        bool recorded = false;
        for (const auto& entry : home.h.runtime().action_journal())
            if (entry.actor == "agent:home-scripted-demo" && entry.path == path("scene/controls/assemble") && entry.exit_code == 0) recorded = true;
        expect(recorded) << "a checked runtime action, with the agent's identity, is in the actual journal";
    };

    "the single preview yields to pointer and keyboard and can be paused"_test = [] {
        Home home;
        expect(home.x() == 0) << "the brief first-visit assembly has completed";
        const auto area = home.h.runtime().screen_bounds(home.h.runtime().find(path("scene/visual")));
        home.h.move(area.center()); home.h.advance(seconds(4));
        expect(home.x() == 0 && home.name("scene/status").contains("yours"));
        home.h.move({2, 2});
        home.h.runtime().focus(home.h.runtime().find(path("scene/controls/assemble"))); home.h.advance(seconds(4));
        expect(home.x() == 0) << "keyboard entry also pauses the automatic demonstration";
        home.h.runtime().focus({}); home.h.advance(seconds(3.5));
        expect(home.x() != 0) << "leaving resumes the one visible preview";
        home.act("scene/controls/pause"); const auto stopped = home.x();
        home.h.advance(seconds(4)); expect(home.x() == stopped);
    };

    "reduced motion and an offscreen or departed page stop the preview"_test = [] {
        Home reduced(1280, "en", true);
        expect(reduced.x() == 0 && reduced.name("scene/status").contains("Reduced motion"));
        reduced.h.advance(seconds(4)); expect(reduced.x() == 0);
        Home home;
        home.h.runtime().resize({1280, 220}, 1); home.h.step(); home.h.advance(seconds(4));
        expect(home.x() == 0);
        home.h.runtime().resize({1280, 844}, 1); home.h.step();
        home.act("intro/actions/creative"); home.h.advance(seconds(4));
        expect(!home.h.tree().node(std::string(root)).has_value());
        expect(home.h.runtime().route() == "motion");
    };

    "the same canonical heading and usable controls fit five widths in both languages"_test = [] {
        for (const float width : {360.0f, 390.0f, 768.0f, 1280.0f, 1440.0f}) for (const std::string_view lang : {"en", "zh"}) {
            Home home(width, lang);
            expect(home.name("intro/title") == (lang == "zh" ? "一个界面，不止一种可能。" : "One interface. Every perspective."));
            for (const auto& node : home.h.tree().snapshot({.root = std::string(root)}).nodes)
                if (node.bounds && !node.states.has(State::hidden) && !node.states.has(State::offscreen))
                    expect(node.bounds->x >= -0.5f && node.bounds->right() <= width + 0.5f) << std::format("{} {} at {}", lang, node.id, width);
            if (width == 390) expect(home.h.save_png(media() / (std::string("home-") + std::string(lang) + "-phone.png")).has_value());
            if (width == 1280 && lang == "zh") expect(home.h.save_png(media() / "home-zh-desktop.png").has_value());
        }
    };
}};
