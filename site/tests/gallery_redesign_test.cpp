// The component library as a focused workspace: one real specimen, a
// bounded directory, and details revealed only when they are requested.
import std;
import openmui;
import openmui.site;
import mlibs.testing;

using namespace omui;
using namespace mlibs::testing;

namespace {
constexpr std::string_view gallery = "main/body/view/components/page/gallery";

struct Run {
    Headless headless;
    explicit Run(float width = 1280, std::string_view lang = "en", bool autoplay = false)
        : headless(HeadlessOptions{.size = {width, 844}}) {
        auto app = site::app("components", lang);
        headless.runtime().set_theme(app.theme);
        app.setup(headless.runtime());
        for (auto& verb : app.verbs) (void)headless.runtime().verbs().add(verb.desc, verb.fn);
        headless.mount("main", app.view());
        headless.step();
        if (!autoplay) act(detail("button") + "/stage/actions/pause", "activate");
        headless.settle(seconds(0.9));
    }
    std::string name(std::string_view id) {
        const auto node = headless.tree().node(id);
        return node ? node->name : std::string();
    }
    bool has(std::string_view id) { return headless.tree().node(id).has_value(); }
    void act(std::string id, std::string_view action, Value args = {}) {
        Invocation call; call.args = std::move(args);
        check(headless.tree().act(id, action, call).exit_code() == std::optional<int>(0), id);
        // A toast's lifetime is part of its behaviour. Finish the short
        // transition without fast-forwarding through its five-second stay.
        headless.settle(seconds(0.9));
    }
    void open(std::string_view id, std::string_view lang = "en") {
        headless.runtime().set_route(site::canonical_route("components", lang, id));
        headless.settle(seconds(0.9));
    }
    std::string detail(std::string_view id) { return std::string(gallery) + "/detail/" + std::string(id); }
    std::uint64_t state_of(std::string_view id) {
        Value snapshot = headless.tree().snapshot({.root = std::string(id)}).to_value();
        if (const auto* source = snapshot.get("nodes"); source && source->list()) {
            Value::List nodes = *source->list();
            for (auto& node : nodes) if (const auto* source_states = node.get("state"); source_states && source_states->list()) {
                Value::List states = *source_states->list();
                std::erase_if(states, [](const Value& state) { return state.as_string() == "focused"; });
                node.set("state", Value(std::move(states)));
            }
            snapshot.set("nodes", Value(std::move(nodes)));
        }
        return site::tree_hash(snapshot);
    }
    std::string pixels_of(std::string_view id) {
        const auto bounds = headless.runtime().screen_bounds(headless.runtime().find(id));
        const auto& pixels = headless.pixels();
        const int left = std::clamp(static_cast<int>(std::floor(bounds.x)), 0, pixels.width());
        const int right = std::clamp(static_cast<int>(std::ceil(bounds.right())), left, pixels.width());
        const int top = std::clamp(static_cast<int>(std::floor(bounds.y)), 0, pixels.height());
        const int bottom = std::clamp(static_cast<int>(std::ceil(bounds.bottom())), top, pixels.height());
        check(right > left && bottom > top, id);
        Hash hash;
        for (int y = top; y < bottom; ++y) hash.bytes(std::as_bytes(std::span(pixels.row(y) + left, static_cast<std::size_t>(right - left))));
        return hash.hex();
    }
};

const std::filesystem::path& media() {
    static const auto directory = scratch_dir("gallery-redesign");
    return directory;
}
}  // namespace

int main(int argc, char** argv) {
    Suite suite("site.gallery");
    suite.test("one live specimen, compact results and an empty search preserve the current task", [] {
        Run run;
        const auto root = std::string(gallery), button = run.detail("button");
        check(run.has(button + "/stage/specimen/variants/primary"));
        check_eq(run.name(root + "/overview/results/count"), std::string("8 / 36"));
        std::size_t specimens = 0;
        for (const auto& node : run.headless.tree().snapshot({.root = root}).nodes) {
            if (node.id.ends_with("/stage/specimen")) ++specimens;
            check(!node.id.contains("/preview/"), "the directory has no repeated live previews");
        }
        check_eq(specimens, std::size_t{1});
        check(!run.has(button + "/options") && !run.has(button + "/code") && !run.has(button + "/semantics"));
        check(!run.has(button + "/stage/specimen/variants/off"));
        const auto results = run.headless.runtime().bounds(run.headless.runtime().find(root + "/overview/results"));
        check(results.height <= 331, "the directory scrolls inside its own bounded region");
        run.act(button + "/stage/actions/change", "activate");
        check_eq(run.name(button + "/stage/specimen/variants/primary"), std::string("Saved 1"));
        run.act(root + "/overview/filter/search", "set_value", Value{}.set("value", "no-matching-component"));
        check(run.has(root + "/overview/results/empty"));
        check_eq(run.name(root + "/overview/results/count"), std::string("0 / 36"));
        check_eq(run.name(button + "/stage/specimen/variants/primary"), std::string("Saved 1"));
        check(run.headless.save_png(media() / "directory-empty-en.png").has_value());
    });

    suite.test("variants and local motion are disclosed without overriding other components' defaults", [] {
        Run run;
        const auto button = run.detail("button");
        const auto primary = button + "/stage/specimen/variants/primary";
        check(run.headless.runtime().get(run.headless.runtime().find(primary), "motion").is_null(), "default writes no motion override");
        run.act(button + "/stage/actions/details", "activate");
        run.act(button + "/stage/actions/variants", "activate");
        check(run.has(button + "/stage/specimen/variants/off"));
        check(run.headless.tree().node(button + "/stage/specimen/variants/off")->states.has(State::disabled));
        if (!run.has(button + "/options")) run.act(button + "/stage/actions/states", "activate");
        run.act(button + "/options/motion", "set_value", Value{}.set("value", "none"));
        check_eq(run.headless.runtime().get(run.headless.runtime().find(primary), "motion").as_string(), std::string_view("none"));
        check(run.name(button + "/stage/hint").contains("off"));
        run.open("toggle");
        const auto toggle = run.detail("toggle") + "/stage/specimen/variants/wifi";
        check(run.headless.runtime().get(run.headless.runtime().find(toggle), "motion").is_null(), "another component retains its typed default");
        run.open("button");
        check_eq(run.headless.runtime().get(run.headless.runtime().find(primary), "motion").as_string(), std::string_view("none"));
        if (!run.has(button + "/options")) run.act(button + "/stage/actions/states", "activate");
        run.act(button + "/options/motion", "set_value", Value{}.set("value", "default"));
        check(run.headless.runtime().get(run.headless.runtime().find(primary), "motion").is_null());
    });

    suite.test("change state updates the actual fixture and its expression, including passive components", [] {
        Run run;
        for (const auto& component : site::component_catalog) {
            if (component.id == "tooltip") continue;  // its real trigger is hover/focus, checked separately
            run.open(component.id);
            const auto detail = run.detail(component.id), specimen = detail + "/stage/specimen";
            if (!run.has(detail + "/code")) run.act(detail + "/panes/code", "activate");
            const auto expression = run.name(detail + "/code/scroll/source");
            run.act(specimen, "scroll_into_view");
            const auto before_tree = run.state_of(specimen);
            const auto before_pixels = run.pixels_of(specimen);
            run.act(detail + "/stage/actions/change", "activate");
            const auto after_tree = run.state_of(specimen);
            const auto after_pixels = run.pixels_of(specimen);
            check(before_tree != after_tree || before_pixels != after_pixels, std::string(component.id) + " changes actual state or drawing");
            check(run.name(detail + "/code/scroll/source") != expression, std::string(component.id) + " keeps its API expression in sync");
            // Dismiss modal specimens through the actual component before
            // inspecting their code; the model is still the same model.
            if (component.id == "dialog") run.act(specimen + "/variants/confirm", "dismiss");
            if (component.id == "drawer") run.act(specimen + "/variants/filters", "dismiss");
            if (!run.has(detail + "/code")) run.act(detail + "/panes/code", "activate");
            check(!run.name(detail + "/code/scroll/source").empty(), component.id);
            run.act(detail + "/stage/actions/reset", "activate");
            check_eq(run.state_of(specimen), before_tree);
        }
    });

    suite.test("copy feedback describes the current expression and vanishes after its input changes", [] {
        Run run;
        run.open("text_box");
        const auto detail = run.detail("text_box");
        run.act(detail + "/panes/code", "activate");
        run.act(detail + "/code/copy", "activate");
        check_eq(run.headless.runtime().clipboard_text(), run.name(detail + "/code/scroll/source"));
        check_eq(run.name(detail + "/code/copied"), std::string("Expression copied"));
        run.act(detail + "/stage/specimen/variants/name", "set_value", Value{}.set("value", "Lin"));
        check(run.name(detail + "/code/scroll/source").contains("Lin"));
        check(run.name(detail + "/code/copied").empty());
    });

    suite.test("a phone reveals its directory and selects one real specimen; all visible controls fit", [] {
        for (const float width : {360.0f, 390.0f, 768.0f, 1280.0f, 1440.0f}) for (const std::string_view lang : {"en", "zh"}) {
            Run run(width, lang);
            const auto root = std::string(gallery);
            auto inside = [&] {
                for (const auto& node : run.headless.tree().snapshot({.root = root}).nodes)
                    if (node.bounds && (node.role == Role::button || node.role == Role::text_box || node.role == Role::checkbox || node.role == Role::combo_box) &&
                        !node.states.has(State::hidden) && !node.states.has(State::offscreen))
                        check(node.bounds->x >= -0.5f && node.bounds->right() <= width + 0.5f, std::format("{} {} at {}", lang, node.id, width));
            };
            inside();
            if (width < 1100) {
                check(!run.has(root + "/overview/filter/search"), "the compact directory starts closed");
                run.act(root + "/overview/browse", "activate");
                check(run.has(root + "/overview/filter/search"));
                inside();
            }
            run.act(root + "/overview/filter/search", "set_value", Value{}.set("value", "text_box"));
            run.act(root + "/overview/results/r0/text_box/heading/open", "activate");
            const auto detail = run.detail("text_box");
            check(run.has(detail + "/stage/specimen/variants/name"));
            if (width < 1100) check(!run.has(root + "/overview/filter/search"), "choosing returns the room to the specimen");
            check(!run.has(detail + "/panes/code"), "preview stays outside the language reference");
            run.act(detail + "/stage/actions/details", "activate");
            run.act(detail + "/stage/actions/states", "activate"); inside();
            run.act(detail + "/stage/actions/variants", "activate"); inside();
            run.act(detail + "/panes/code", "activate"); inside();
            if (width == 390 && lang == "zh") check(run.headless.save_png(media() / "options-zh-phone.png").has_value());
        }
    });

    suite.test("autoplay yields to pointer and keyboard, pauses explicitly and stops for reduced motion", [] {
        Run run(1280, "en", true);
        const auto detail = run.detail("button"), primary = detail + "/stage/specimen/variants/primary";
        run.headless.advance(seconds(1.8));
        check_eq(run.name(primary), std::string("Saved 1"));
        const auto bounds = run.headless.runtime().screen_bounds(run.headless.runtime().find(primary));
        run.headless.move(bounds.center()); run.headless.step();
        run.headless.advance(seconds(2));
        check_eq(run.name(primary), std::string("Saved 1"));
        check(run.name(detail + "/stage/hint").contains("Interactive"));
        run.headless.move({2, 2});
        run.headless.runtime().focus(run.headless.runtime().find(primary)); run.headless.step();
        run.headless.advance(seconds(2));
        check_eq(run.name(primary), std::string("Saved 1"));
        run.headless.runtime().focus({}); run.headless.step();
        run.headless.advance(seconds(1.8));
        check_eq(run.name(primary), std::string("Save"));
        run.act(detail + "/stage/actions/pause", "activate");
        run.headless.advance(seconds(2));
        check_eq(run.name(primary), std::string("Save"));
        run.act(detail + "/stage/actions/pause", "activate");
        run.headless.runtime().set_motion_level(MotionLevel::reduced); run.headless.step();
        run.headless.advance(seconds(2));
        check_eq(run.name(primary), std::string("Save"));
        check(run.name(detail + "/stage/hint").contains("reduced-motion"));
        run.headless.runtime().set_motion_level(MotionLevel::full);
        run.headless.runtime().resize({1280, 400}, 1); run.headless.step();
        run.headless.advance(seconds(2));
        check_eq(run.name(primary), std::string("Save"));
        check(run.name(detail + "/stage/hint").contains("visible"));
        run.headless.runtime().resize({1280, 844}, 1); run.headless.step();
        run.act(detail + "/stage/actions/details", "activate");
        run.headless.advance(seconds(2));
        check(run.name(primary) == "Save", "the detail route never runs the automatic preview");
        run.act(detail + "/stage/actions/details", "activate");
        Invocation agent; agent.actor = Actor{"agent", "gallery-review", {}};
        check(run.headless.tree().act(primary, "activate", agent).exit_code() == std::optional<int>(0));
        run.headless.advance(seconds(2));
        check(run.name(primary) == "Saved 1", "an agent's real edit stays available for inspection");
        check(run.name(detail + "/stage/hint").contains("Interactive"));
    });

    suite.test("tooltip previews use the actual keyboard trigger without taking focus automatically", [] {
        Run run;
        run.act(std::string(gallery) + "/overview/filter/search", "set_value", Value{}.set("value", "tooltip"));
        run.act(std::string(gallery) + "/overview/results/r0/tooltip/heading/open", "activate");
        const auto detail = run.detail("tooltip"), trigger = detail + "/stage/specimen/variants/save";
        check(run.headless.runtime().focused() != run.headless.runtime().find(trigger));
        run.act(detail + "/stage/actions/interact", "activate");
        run.headless.advance(seconds(0.6));
        check(run.headless.runtime().focused() == run.headless.runtime().find(trigger));
        bool visible = false;
        for (const auto& node : run.headless.tree().snapshot({.root = detail}).nodes)
            if (node.role == Role::tooltip && !node.states.has(State::hidden)) visible = true;
        check(visible, "the real tooltip appears from keyboard focus");
    });

    suite.test("actual workspace review frames in English and Chinese", [] {
        for (const auto& [width, language, file] : {std::tuple{1280.0f, "en", "workspace-en-desktop.png"}, std::tuple{390.0f, "zh", "workspace-zh-phone.png"}}) {
            Run run(width, language, true);
            check(run.headless.save_png(media() / file).has_value());
            run.open("data_grid", language);
            check(run.headless.save_png(media() / (std::string("data-grid-") + language + ".png")).has_value());
        }
    });
    return suite.run(argc, argv);
}
