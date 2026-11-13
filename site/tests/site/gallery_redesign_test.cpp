// The component library as a focused workspace: one real specimen, a
// bounded directory, and details revealed only when they are requested.
import std;
import omui;
import omui.site;
import mlibs.testing;
import mcpplibs.mtest;

using namespace omui;
using namespace mlibs::testing;
using namespace mcpplibs::mtest;

namespace {
constexpr std::string_view gallery = "main/body/view/components/page/gallery";

struct Run {
    Headless headless;
    explicit Run(float width = 1280, std::string_view lang = "en", bool autoplay = false)
        : headless(HeadlessOptions{.size = {width, 844}}) {
        // The library is a component's page now (the components themselves are the studio's): opened at the button.
        auto app = site::app(lang == "zh" ? "zh/components/button" : "components/button", lang);
        headless.runtime().set_theme(app.theme);
        app.setup(headless.runtime());
        for (auto& verb : app.verbs) (void)headless.runtime().verbs().add(verb.desc, verb.fn);
        headless.mount("main", app.view());
        headless.step();
        (void)autoplay;  // the automatic preview is the studio's now (live_wall_test)
        headless.settle(seconds(0.9));
    }
    std::string name(std::string_view id) {
        const auto node = headless.tree().node(id);
        return node ? node->name : std::string();
    }
    bool has(std::string_view id) { return headless.tree().node(id).has_value(); }
    void act(std::string id, std::string_view action, Value args = {}) {
        Invocation call; call.args = std::move(args);
        expect(headless.tree().act(id, action, call).exit_code() == std::optional<int>(0)) << id;
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
        expect(right > left && bottom > top) << id;
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

static UTest cases{"site.gallery", [] {
    "one live specimen, compact results and an empty search preserve the current task"_test = [] {
        Run run;
        const auto root = std::string(gallery), button = run.detail("button");
        expect(run.has(button + "/stage/specimen/variants/primary"));
        expect(eq(run.name(root + "/overview/results/count"), std::string("8 / 36")));
        std::size_t specimens = 0;
        for (const auto& node : run.headless.tree().snapshot({.root = root}).nodes) {
            if (node.id.ends_with("/stage/specimen")) ++specimens;
            expect(!node.id.contains("/preview/")) << "the directory has no repeated live previews";
        }
        expect(eq(specimens, std::size_t{1}));
        expect(!run.has(button + "/options") && !run.has(button + "/code") && !run.has(button + "/semantics"));
        expect(!run.has(button + "/stage/specimen/variants/off"));
        const auto results = run.headless.runtime().bounds(run.headless.runtime().find(root + "/overview/results"));
        expect(results.height <= 331) << "the directory scrolls inside its own bounded region";
        run.act(button + "/stage/actions/change", "activate");
        expect(eq(run.name(button + "/stage/specimen/variants/primary"), std::string("Saved 1")));
        run.act(root + "/overview/filter/search", "set_value", Value{}.set("value", "no-matching-component"));
        expect(run.has(root + "/overview/results/empty"));
        expect(eq(run.name(root + "/overview/results/count"), std::string("0 / 36")));
        expect(eq(run.name(button + "/stage/specimen/variants/primary"), std::string("Saved 1")));
        expect(run.headless.save_png(media() / "directory-empty-en.png").has_value());
    };

    "variants and local motion are disclosed without overriding other components' defaults"_test = [] {
        Run run;
        const auto button = run.detail("button");
        const auto primary = button + "/stage/specimen/variants/primary";
        expect(run.headless.runtime().get(run.headless.runtime().find(primary), "motion").is_null()) << "default writes no motion override";
        run.act(button + "/stage/actions/variants", "activate");
        expect(run.has(button + "/stage/specimen/variants/off"));
        expect(run.headless.tree().node(button + "/stage/specimen/variants/off")->states.has(State::disabled));
        if (!run.has(button + "/options")) run.act(button + "/stage/actions/states", "activate");
        run.act(button + "/options/motion", "set_value", Value{}.set("value", "none"));
        expect(eq(run.headless.runtime().get(run.headless.runtime().find(primary), "motion").as_string(), std::string_view("none")));
        expect(run.name(button + "/stage/hint").contains("off"));
        run.open("toggle");
        const auto toggle = run.detail("toggle") + "/stage/specimen/variants/wifi";
        expect(run.headless.runtime().get(run.headless.runtime().find(toggle), "motion").is_null()) << "another component retains its typed default";
        run.open("button");
        expect(eq(run.headless.runtime().get(run.headless.runtime().find(primary), "motion").as_string(), std::string_view("none")));
        if (!run.has(button + "/options")) run.act(button + "/stage/actions/states", "activate");
        run.act(button + "/options/motion", "set_value", Value{}.set("value", "default"));
        expect(run.headless.runtime().get(run.headless.runtime().find(primary), "motion").is_null());
    };

    "change state updates the actual fixture and its expression, including passive components"_test = [] {
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
            expect(before_tree != after_tree || before_pixels != after_pixels) << std::string(component.id) + " changes actual state or drawing";
            expect(run.name(detail + "/code/scroll/source") != expression) << std::string(component.id) + " keeps its API expression in sync";
            // Dismiss modal specimens through the actual component before
            // inspecting their code; the model is still the same model.
            if (component.id == "dialog") run.act(specimen + "/variants/confirm", "dismiss");
            if (component.id == "drawer") run.act(specimen + "/variants/filters", "dismiss");
            if (!run.has(detail + "/code")) run.act(detail + "/panes/code", "activate");
            expect(!run.name(detail + "/code/scroll/source").empty()) << component.id;
            run.act(detail + "/stage/actions/reset", "activate");
            expect(eq(run.state_of(specimen), before_tree));
        }
    };

    "copy feedback describes the current expression and vanishes after its input changes"_test = [] {
        Run run;
        run.open("text_box");
        const auto detail = run.detail("text_box");
        run.act(detail + "/panes/code", "activate");
        run.act(detail + "/code/copy", "activate");
        expect(eq(run.headless.runtime().clipboard_text(), run.name(detail + "/code/scroll/source")));
        expect(eq(run.name(detail + "/code/copied"), std::string("Expression copied")));
        run.act(detail + "/stage/specimen/variants/name", "set_value", Value{}.set("value", "Lin"));
        expect(run.name(detail + "/code/scroll/source").contains("Lin"));
        expect(run.name(detail + "/code/copied").empty());
    };

    "a phone reveals its directory and selects one real specimen; all visible controls fit"_test = [] {
        for (const float width : {360.0f, 390.0f, 768.0f, 1280.0f, 1440.0f}) for (const std::string_view lang : {"en", "zh"}) {
            Run run(width, lang);
            const auto root = std::string(gallery);
            auto inside = [&] {
                for (const auto& node : run.headless.tree().snapshot({.root = root}).nodes)
                    if (node.bounds && (node.role == Role::button || node.role == Role::text_box || node.role == Role::checkbox || node.role == Role::combo_box) &&
                        !node.states.has(State::hidden) && !node.states.has(State::offscreen))
                        expect(node.bounds->x >= -0.5f && node.bounds->right() <= width + 0.5f) << std::format("{} {} at {}", lang, node.id, width);
            };
            inside();
            if (width < 1100) {
                expect(!run.has(root + "/overview/filter/search")) << "the compact directory starts closed";
                run.act(root + "/overview/browse", "activate");
                expect(run.has(root + "/overview/filter/search"));
                inside();
            }
            run.act(root + "/overview/filter/search", "set_value", Value{}.set("value", "text_box"));
            run.act(root + "/overview/results/r0/text_box/heading/open", "activate");
            const auto detail = run.detail("text_box");
            expect(run.has(detail + "/stage/specimen/variants/name"));
            if (width < 1100) expect(!run.has(root + "/overview/filter/search")) << "choosing returns the room to the specimen";
            expect(eq(run.headless.runtime().route(), std::string(lang == "zh" ? "zh/components/text-box" : "components/text-box")));
            run.act(detail + "/stage/actions/states", "activate"); inside();
            run.act(detail + "/stage/actions/variants", "activate"); inside();
            run.act(detail + "/panes/code", "activate"); inside();
            if (width == 390 && lang == "zh") expect(run.headless.save_png(media() / "options-zh-phone.png").has_value());
        }
    };

    "a tooltip is shown through its actual keyboard trigger, never taking focus by itself"_test = [] {
        Run run;
        run.act(std::string(gallery) + "/overview/filter/search", "set_value", Value{}.set("value", "tooltip"));
        run.act(std::string(gallery) + "/overview/results/r0/tooltip/heading/open", "activate");
        const auto detail = run.detail("tooltip"), trigger = detail + "/stage/specimen/variants/save";
        expect(run.headless.runtime().focused() != run.headless.runtime().find(trigger));
        run.act(detail + "/stage/actions/change", "activate");
        run.headless.advance(seconds(0.6));
        expect(run.headless.runtime().focused() == run.headless.runtime().find(trigger));
        bool visible = false;
        for (const auto& node : run.headless.tree().snapshot({.root = detail}).nodes)
            if (node.role == Role::tooltip && !node.states.has(State::hidden)) visible = true;
        expect(visible) << "the real tooltip appears from keyboard focus";
    };

    "actual workspace review frames in English and Chinese"_test = [] {
        for (const auto& [width, language, file] : {std::tuple{1280.0f, "en", "workspace-en-desktop.png"}, std::tuple{390.0f, "zh", "workspace-zh-phone.png"}}) {
            Run run(width, language, true);
            expect(run.headless.save_png(media() / file).has_value());
            run.open("data_grid", language);
            expect(run.headless.save_png(media() / (std::string("data-grid-") + language + ".png")).has_value());
        }
    };
}};
