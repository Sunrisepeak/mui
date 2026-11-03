// The opening is an actual object, shared by pixels, its semantic tree and
// a visibly simulated agent. The small and large mark share the vector original.
import std;
import openmui;
import openmui.site;
import openmui.testing;

using namespace omui;
using namespace omui::testing;

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
        check(h.tree().act(path(tail), action, request).exit_code() == std::optional<int>(0), tail);
        h.settle(seconds(0.8));
    }
    double x() { return h.runtime().get(h.runtime().find(path("scene/visual/mark/letter_m")), "translate_x").as_double(); }
};
}  // namespace

int main(int argc, char** argv) {
    Suite suite("site.home_landing");
    suite.test("a short opening has one heading, one real object and two clear destinations", [] {
        Home home;
        check(home.name("intro/title") == "One interface. Every perspective.");
        std::size_t headings = 0, images = 0;
        for (const auto& node : home.h.tree().snapshot({.root = std::string(root)}).nodes) {
            if (node.role == Role::heading) ++headings;
            if (node.role == Role::image) ++images;
            check(!node.id.contains("tasks") && !node.id.contains("json") && !node.id.contains("workbench"));
        }
        check_eq(headings, std::size_t{1}); check_eq(images, std::size_t{4});
        check_eq(site::brand_geometry().size(), std::size_t{5});
        check(home.name("intro/actions/explore") == "Explore components");
        check(home.name("intro/actions/creative") == "Creative demos");
        check(home.h.save_png(media() / "home-en-desktop.png").has_value());
    });

    suite.test("the small and large animated marks use the exact original and settle", [] {
        for (const float size : {32.0f, 248.0f}) {
            Headless still(HeadlessOptions{.size = {300, 300}}), moving(HeadlessOptions{.size = {300, 300}});
            auto unfolded = signal(false);
            still.mount("sheet", column({}, {site::brand_mark("mark", size)}));
            moving.mount("sheet", column({}, {site::animated_brand_mark("mark", size, unfolded)}));
            still.settle(); moving.settle();
            check_eq(site::frame_hash(still.pixels()), site::frame_hash(moving.pixels()));
            const auto before = site::frame_hash(moving.pixels());
            unfolded.set(true); moving.step(); moving.advance(seconds(0.1));
            const auto during = site::frame_hash(moving.pixels());
            moving.settle(); const auto after = site::frame_hash(moving.pixels());
            check(before != after && during != before && during != after, "runtime motion produces actual intermediate drawings");
            check(moving.runtime().frame_demand().idle(), "the mark has no permanent decorative timer");
            if (size == 32) check(std::abs(moving.runtime().get(moving.runtime().find("sheet/mark/letter_m"), "translate_x").as_double()) < 2);
        }
    });

    suite.test("structure reads actual named nodes; the simulated agent operates the same button", [] {
        Home home;
        home.act("scene/perspective", "set_value", Value{}.set("value", "structure"));
        std::string names;
        for (const auto& node : home.h.tree().snapshot({.root = path("scene/visual")}).nodes) if (node.role == Role::image) {
            if (!names.empty()) names += " · "; names += node.name;
        }
        check(home.name("scene/caption").starts_with(names) && !names.empty());
        home.act("scene/controls/assemble");
        check(home.name("scene/caption").ends_with("Apart"));
        const auto position = home.x();
        home.act("scene/perspective", "set_value", Value{}.set("value", "agent"));
        check(home.name("scene/caption").contains("simulated"));
        home.act("scene/controls/delegate");
        check(home.x() != position, "the agent changed the actual mark model");
        check(home.name("scene/status").contains("simulated agent acted"));
        bool recorded = false;
        for (const auto& entry : home.h.runtime().action_journal())
            if (entry.actor == "agent:home-scripted-demo" && entry.path == path("scene/controls/assemble") && entry.exit_code == 0) recorded = true;
        check(recorded, "a checked runtime action, with the agent's identity, is in the actual journal");
    });

    suite.test("the single preview yields to pointer and keyboard and can be paused", [] {
        Home home;
        check(home.x() == 0, "the brief first-visit assembly has completed");
        const auto area = home.h.runtime().screen_bounds(home.h.runtime().find(path("scene/visual")));
        home.h.move(area.center()); home.h.advance(seconds(4));
        check(home.x() == 0 && home.name("scene/status").contains("yours"));
        home.h.move({2, 2});
        home.h.runtime().focus(home.h.runtime().find(path("scene/controls/assemble"))); home.h.advance(seconds(4));
        check(home.x() == 0, "keyboard entry also pauses the automatic demonstration");
        home.h.runtime().focus({}); home.h.advance(seconds(3.5));
        check(home.x() != 0, "leaving resumes the one visible preview");
        home.act("scene/controls/pause"); const auto stopped = home.x();
        home.h.advance(seconds(4)); check(home.x() == stopped);
    });

    suite.test("reduced motion and an offscreen or departed page stop the preview", [] {
        Home reduced(1280, "en", true);
        check(reduced.x() == 0 && reduced.name("scene/status").contains("Reduced motion"));
        reduced.h.advance(seconds(4)); check(reduced.x() == 0);
        Home home;
        home.h.runtime().resize({1280, 220}, 1); home.h.step(); home.h.advance(seconds(4));
        check(home.x() == 0);
        home.h.runtime().resize({1280, 844}, 1); home.h.step();
        home.act("intro/actions/creative"); home.h.advance(seconds(4));
        check(!home.h.tree().node(std::string(root)).has_value());
        check(home.h.runtime().route() == "demos");
    });

    suite.test("the same canonical heading and usable controls fit five widths in both languages", [] {
        for (const float width : {360.0f, 390.0f, 768.0f, 1280.0f, 1440.0f}) for (const std::string_view lang : {"en", "zh"}) {
            Home home(width, lang);
            check(home.name("intro/title") == (lang == "zh" ? "一个界面，不止一种可能。" : "One interface. Every perspective."));
            for (const auto& node : home.h.tree().snapshot({.root = std::string(root)}).nodes)
                if (node.bounds && !node.states.has(State::hidden) && !node.states.has(State::offscreen))
                    check(node.bounds->x >= -0.5f && node.bounds->right() <= width + 0.5f, std::format("{} {} at {}", lang, node.id, width));
            if (width == 390) check(home.h.save_png(media() / (std::string("home-") + std::string(lang) + "-phone.png")).has_value());
            if (width == 1280 && lang == "zh") check(home.h.save_png(media() / "home-zh-desktop.png").has_value());
        }
    });
    return suite.run(argc, argv);
}
