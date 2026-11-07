// Typed default motion is exercised through the actual components and the
// public runtime: semantic actions, pixels between states, accessibility
// preferences, input during focus feedback and the theme's cascade.
import std;
import omui.ui.base;
import omui.host.headless;
import mlibs.testing;

using namespace omui;
using namespace mlibs::testing;

namespace {

mlibs::text::FontCollection& fonts() {
    static mlibs::text::FontCollection f = [] {
        mlibs::text::FontCollection result;
        (void)result.add_defaults();
        return result;
    }();
    return f;
}

HeadlessOptions options() {
    HeadlessOptions result;
    result.size = {420, 280};
    result.fonts = &fonts();
    result.parallel_raster = false;
    return result;
}

const std::filesystem::path& pictures() {
    static const auto path = scratch_dir("ui-base-default-motion");
    return path;
}

std::uint64_t pixels(Headless& h, std::optional<Rect> region = {}) {
    const auto& surface = h.pixels();
    const Rect r = region.value_or(Rect{0, 0, static_cast<float>(surface.width()), static_cast<float>(surface.height())});
    const int left = std::clamp(static_cast<int>(std::floor(r.x)), 0, surface.width());
    const int top = std::clamp(static_cast<int>(std::floor(r.y)), 0, surface.height());
    const int right = std::clamp(static_cast<int>(std::ceil(r.right())), left, surface.width());
    const int bottom = std::clamp(static_cast<int>(std::ceil(r.bottom())), top, surface.height());
    Hash hash;
    for (int y = top; y < bottom; ++y)
        hash.bytes(std::as_bytes(std::span(surface.row(y) + left, static_cast<std::size_t>(right - left))));
    return hash.value();
}

void save(Headless& h, std::string_view name) {
    check(h.save_png(pictures() / std::format("{}.png", name)).has_value(), "reviewable real raster output");
}

void act(Headless& h, std::string_view path, std::string_view action, Value args = {}) {
    Invocation call;
    call.actor = {"agent", "default-motion-test", {}};
    call.args = std::move(args);
    check_eq(h.tree().act(path, action, call).exit_code().value_or(-1), std::int64_t{0});
    h.step();
}

std::optional<MotionState> moving(Headless& h, std::string_view path) {
    for (const auto& motion : h.runtime().motions())
        if (motion.path == path) return motion;
    return {};
}

struct Scene {
    Element element;
    std::function<void(Headless&)> change;
};

// These are production controls, with their own paint/parts and actions.
// There is no animated fixture geometry and no animation in these builders.
Scene scene(std::string_view type, std::string_view override_motion = {}) {
    Element element;
    std::function<void(Headless&)> change;
    if (type == "checkbox") {
        element = checkbox({.id = "subject", .label = "Ready to publish"});
        change = [](Headless& h) { act(h, "main/subject", "toggle"); };
    } else if (type == "toggle") {
        element = toggle({.id = "subject", .label = "Sync changes"});
        change = [](Headless& h) { act(h, "main/subject", "toggle"); };
    } else if (type == "segmented") {
        element = segmented({.id = "subject", .label = "Calendar", .labels = std::vector<std::string>{"Day", "Week", "Month"},
                             .options = std::vector<std::string>{"day", "week", "month"}, .value = "day"});
        change = [](Headless& h) { act(h, "main/subject/month", "activate"); };
    } else if (type == "progress") {
        auto value = signal(0.15);
        element = progress({.id = "subject", .label = "Upload", .value = value, .width = 300});
        change = [value](Headless& h) { value.set(0.85); h.step(); };
    } else if (type == "accordion") {
        element = accordion({.id = "subject", .title = "Release details", .width = 320},
                            {text({.id = "answer", .text = "All checks passed. Ready for review."})});
        change = [](Headless& h) { act(h, "main/subject", "expand"); };
    } else {
        auto open = signal(false);
        element = drawer({.id = "subject", .modal = false, .open = open, .size = 220.0, .title = "Filters"},
                         {checkbox({.id = "mine", .label = "Assigned to me"}), button({.id = "apply", .text = "Apply filters"})});
        change = [open](Headless& h) { open.set(true); h.step(); };
    }
    if (!override_motion.empty()) element.prop("motion", Source(Value(std::string(override_motion))));
    return {std::move(element), std::move(change)};
}

void mount(Headless& h, Scene& sample) {
    h.mount("main", column({.padding = 24}, {std::move(sample.element)}));
    h.settle();
}

MotionSpec tween(double duration) {
    return compose_motion({MotionPrimitive::match_drawing, MotionPrimitive::match_glyphs, MotionPrimitive::fade},
                          Transition::tween(seconds(duration)));
}

void expect_duration(Headless& h, std::string_view path, double expected) {
    const auto motion = moving(h, path);
    check(motion.has_value(), std::format("{} has a real active drawing motion", path));
    if (motion) check(std::abs((motion->end - motion->start) - expected) < 0.001,
                      std::format("{}: expected {} s, actual {} s", path, expected, motion->end - motion->start));
}

}  // namespace

int main(int argc, char** argv) {
    Suite suite{"ui.base.default-motion"};
    register_base_components();

    suite.test("37 typed defaults reference existing compositions of shared primitives", [] {
        const auto recipes = default_component_motion_recipes();
        const auto library = default_motion_library();
        const auto rules = default_motion_rules();
        check_eq(recipes.size(), std::size_t{37});
        std::set<std::string_view> names;
        for (const auto& recipe : recipes) {
            check(names.insert(recipe.component).second, "one entry per component type");
            for (const auto& [slot, name] : {std::pair{"update", recipe.update}, std::pair{"state", recipe.state},
                                           std::pair{"enter", recipe.enter}, std::pair{"exit", recipe.exit}}) {
                check(!name.empty(), std::format("{}.{} is explicit", recipe.component, slot));
                const auto rule = std::ranges::find_if(rules, [&](const MotionRule& r) { return r.target == recipe.component && r.slot == slot; });
                check(rule != rules.end(), "the directory is installed in the actual framework rules");
                if (name == "none") {
                    if (rule != rules.end()) check(rule->spec.kind == MotionSpec::Kind::none, "input content remains immediate");
                    continue;
                }
                const auto found = library.find(name);
                check(found != library.end(), std::format("{} exists", name));
                if (found == library.end()) continue;
                check(std::popcount(found->second.primitives) >= 2, std::format("{} composes reusable primitives", name));
                check((found->second.primitives & static_cast<std::uint16_t>(MotionPrimitive::match_drawing)) != 0,
                      "shared drawing matching underlies the recipe");
                if (rule != rules.end()) {
                    check(rule->spec.name == name && rule->spec.primitives == found->second.primitives &&
                          rule->spec.kind == found->second.kind &&
                          rule->spec.appear == found->second.appear && rule->spec.stretch == found->second.stretch,
                          "installed rule is the documented composition");
                    check(rule->layer == MotionRule::Layer::framework, "styles can override framework defaults");
                }
            }
        }
        check(names.contains("actor_log") && names.contains("text_box") && names.contains("code_editor"));
        check(std::ranges::none_of(rules, [](const MotionRule& r) { return r.target == "*"; }), "no blanket recipe for every drawing");
    });

    suite.test("six real control types paint distinct middle frames and reach their actual state drawings", [] {
        for (const std::string type : {"checkbox", "toggle", "segmented", "progress", "accordion", "drawer"}) {
            Headless animated(options()), immediate(options());
            immediate.runtime().set_motion_level(MotionLevel::off);
            auto sample = scene(type), reference = scene(type);
            mount(animated, sample); mount(immediate, reference);
            const auto before = pixels(animated);
            check_eq(before, pixels(immediate));
            save(animated, type + "-before");
            sample.change(animated); reference.change(immediate);
            const auto record = moving(animated, "main/subject");
            check(record.has_value(), type + ": the actual control owns its motion handle");
            if (record) {
                check(record->matched + record->to_only + record->from_only > 0, "the handle contains real paint pieces");
                if (type == "checkbox") check(record->to_only > 0, "the checkmark appears as a part");
                if (type == "toggle" || type == "segmented" || type == "progress")
                    check(record->matched > 0, "the existing thumb, lens or value fill is matched");
            }
            animated.advance(seconds(0.10));
            const auto middle = pixels(animated), final = pixels(immediate);
            check(middle != before && middle != final, type + ": painted motion is neither endpoint");
            save(animated, type + "-middle");
            animated.settle();
            check(pixels(animated) == final, type + ": exactly the unanimated production drawing at rest");
            check(animated.runtime().motions().empty(), "no autonomous component animation remains");
            save(animated, type + "-rest");
        }
    });

    suite.test("shape-specific primitives alter real checkmarks, thumbs and selection lenses", [] {
        for (const std::string type : {"checkbox", "toggle", "segmented"}) {
            Theme flat = Theme::light();
            const auto recipes = default_component_motion_recipes();
            const auto recipe = std::ranges::find_if(recipes, [&](const ComponentMotionRecipe& r) { return r.component == type; });
            check(recipe != recipes.end());
            if (recipe == recipes.end()) continue;
            MotionSpec without_shape = flat.motion_library.at(std::string(recipe->update));
            without_shape.appear.away.scale = hanim::Vec2{1, 1}; without_shape.stretch = 0;
            flat.motion_library["test.without-shape"] = without_shape;
            Headless typed(options()), plain(options());
            plain.runtime().set_theme(flat);
            auto sample = scene(type), reference = scene(type, "test.without-shape");
            mount(typed, sample); mount(plain, reference);
            check_eq(pixels(typed), pixels(plain));
            sample.change(typed); reference.change(plain);
            const seconds sample_time(type == "checkbox" ? 0.10 : 0.05);
            typed.advance(sample_time); plain.advance(sample_time);
            check(moving(typed, "main/subject").has_value() && moving(plain, "main/subject").has_value());
            check(pixels(typed) != pixels(plain), type + ": its local growth or stretch affects actual intermediate paint");
            save(typed, type + "-typed-shape"); save(plain, type + "-without-shape");
            typed.settle(); plain.settle();
            check(pixels(typed) == pixels(plain), "only the journey changed");
        }
    });

    suite.test("off is immediate and reduced is a short real tween for the production controls", [] {
        for (const std::string type : {"checkbox", "toggle", "segmented", "progress", "accordion", "drawer"}) {
            Headless full(options()), reduced(options()), off(options());
            reduced.runtime().set_motion_level(MotionLevel::reduced);
            off.runtime().set_motion_level(MotionLevel::off);
            auto a = scene(type), b = scene(type), c = scene(type);
            mount(full, a); mount(reduced, b); mount(off, c);
            const auto before = pixels(off);
            a.change(full); b.change(reduced); c.change(off);
            check(off.runtime().motions().empty(), type + ": off creates no motion");
            check(pixels(off) != before, "off still applies the new state immediately");
            expect_duration(reduced, "main/subject", 0.12);
            const auto standard = moving(full, "main/subject");
            if (standard) check(standard->end - standard->start > 0.12, "the full recipe retains its longer response");
            reduced.advance(seconds(0.05));
            check(pixels(reduced) != before && pixels(reduced) != pixels(off), type + ": reduced has real intermediate paint");
            reduced.advance(seconds(0.15)); full.settle();
            check(reduced.runtime().motions().empty(), "reduced motion has ended");
            check_eq(pixels(reduced), pixels(off));
            check_eq(pixels(full), pixels(off));
        }
    });

    suite.test("text input supersedes a running focus drawing on its first painted frame", [] {
        for (bool code : {false, true}) {
            Headless animated(options()), reference(options());
            reference.runtime().set_motion_level(MotionLevel::off);
            auto source = signal(std::string("old")), target = signal(std::string("old"));
            auto make = [code](Signal<std::string> value) {
                return code ? code_editor({.id = "subject", .lines = 5, .text = value, .width = 340})
                            : text_box({.id = "subject", .label = "Name", .text = value, .width = 340});
            };
            animated.mount("main", column({.padding = 24}, {make(source)}));
            reference.mount("main", column({.padding = 24}, {make(target)}));
            animated.step(); reference.step();
            const std::string path = code ? "main/subject/text" : "main/subject";
            animated.runtime().focus_visibly(animated.runtime().find(path));
            reference.runtime().focus_visibly(reference.runtime().find(path));
            animated.step(); reference.step();
            check(moving(animated, path).has_value(), "focus feedback really was in progress before typing");
            const Rect field = animated.runtime().screen_bounds(animated.runtime().find(path));
            // Interior pixels isolate text and its caret from any enclosing panel's focus feedback.
            const Rect interior{field.x + 8, field.y + 6, field.width - 16, field.height - 12};
            const auto old = pixels(animated, interior);
            animated.type("fresh"); reference.type("fresh");
            check(source.peek().find("fresh") != std::string::npos && source.peek() == target.peek(), "the real editor accepted the text");
            check(!moving(animated, path), "immediate content cancels the stale focus drawing");
            check(pixels(animated, interior) != old, "new glyphs paint immediately, not merely semantic text");
            check(pixels(animated, interior) == pixels(reference, interior), "the first input frame already equals the immediate editor");
            save(animated, code ? "code-input-first-frame" : "text-input-first-frame");
            // The same guarantee applies to an external two-way value write during another focus transition.
            animated.runtime().focus({}); reference.runtime().focus({});
            animated.step(); reference.step();
            source.set("replacement text"); target.set("replacement text");
            animated.step(); reference.step();
            check(!moving(animated, path), "external value writes also supersede stale feedback");
            check_eq(pixels(animated, interior), pixels(reference, interior));
        }
    });

    suite.test("scoped style layers and explicit overrides win on actual component drawings", [] {
        Headless baseline(options());
        auto default_sample = scene("checkbox");
        mount(baseline, default_sample); default_sample.change(baseline);
        const auto default_motion = moving(baseline, "main/subject");
        check(default_motion.has_value());
        const double default_duration = default_motion ? default_motion->end - default_motion->start : 0;
        // Deliberately append a lower-layer, more-specific rule last: layer
        // precedence must beat both insertion order and selector specificity.
        for (const auto layer : {MotionRule::Layer::style, MotionRule::Layer::style_targeted, MotionRule::Layer::application}) {
            Headless h(options());
            Theme scoped = Theme::light();
            if (layer != MotionRule::Layer::style)
                scoped.motion_rules.push_back({.target = "checkbox", .spec = tween(0.55), .layer = MotionRule::Layer::style});
            if (layer == MotionRule::Layer::application)
                scoped.motion_rules.push_back({.target = "checkbox", .spec = tween(0.6), .layer = MotionRule::Layer::style_targeted});
            scoped.motion_rules.push_back({.target = "*", .spec = tween(0.4), .layer = layer});
            scoped.motion_rules.push_back({.target = "checkbox", .spec = tween(0.9), .layer = MotionRule::Layer::framework});
            scoped.motion["test.explicit"] = Transition::tween(seconds(0.7));
            h.runtime().set_scope("preview", scoped);
            auto own = checkbox({.id = "explicit", .label = "Explicit"});
            own.prop("motion", Source(Value("morph test.explicit")));
            auto none = checkbox({.id = "none", .label = "Immediate"});
            none.prop("motion", Source(Value("none")));
            auto panel = column({.id = "scope", .gap = 16}, {
                checkbox({.id = "styled", .label = "Scoped"}), std::move(own), std::move(none)});
            panel.prop("theme_scope", Source(Value("preview")));
            h.mount("main", column({.padding = 24, .gap = 16}, {
                checkbox({.id = "outside", .label = "Outside"}), std::move(panel)}));
            h.settle();
            for (const std::string path : {"main/outside", "main/scope/styled", "main/scope/explicit", "main/scope/none"}) act(h, path, "toggle");
            expect_duration(h, "main/scope/styled", 0.4);
            expect_duration(h, "main/scope/explicit", 0.7);
            check(!moving(h, "main/scope/none"), "an explicit none suppresses a style and the framework");
            const auto outside = moving(h, "main/outside");
            check(outside.has_value(), "unscoped checkbox retains its own default");
            if (outside) check(std::abs((outside->end - outside->start) - default_duration) < 0.001,
                               "unscoped duration still equals the actual framework reference");
            const Rect rect = h.runtime().screen_bounds(h.runtime().find("main/scope/styled"));
            const auto middle = pixels(h, rect);
            h.settle();
            check(middle != pixels(h, rect), "the selected scoped recipe animates the actual checkbox paint");
        }
    });

    suite.test("press.squash: a pressed button is pressed in about the finger and rings out", [] {
        Headless h(options());
        auto sample = button({.id = "go", .text = "Go"});
        sample.prop("motion", Source(Value("press.squash")));
        h.mount("main", column({.padding = 24}, {std::move(sample)}));
        h.settle();
        const auto bounds = h.runtime().screen_bounds(h.runtime().find("main/go"));
        PointerEvent down;
        down.kind = PointerKind::down; down.button = MouseButton::left; down.buttons = 1;
        down.position = {bounds.x + bounds.width * .25f, bounds.center().y};
        PointerEvent over = down;
        over.kind = PointerKind::move; over.buttons = 0;
        h.step({over});
        h.settle();  // hovered first: the press alone is the state change
        h.step({down});
        h.advance(seconds(0.45 / 6));  // the press's first crest
        float pressed = 1;
        for (const auto& command : h.runtime().display_list().commands())
            if (const auto* push = std::get_if<xrender::PushTransform>(&command); push && push->transform.b == 0 && push->transform.a < 1)
                pressed = std::min(pressed, push->transform.a);
        check(pressed < .955f, std::format("pressed in past the button's own press: {} ({} motions)", pressed, h.runtime().motions().size()));
        h.advance(seconds(1));
        check(h.runtime().motions().empty(), "then at rest, pressed");
    });

    return suite.run(argc, argv);
}
