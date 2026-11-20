// Overlays open from what opened them (component quality B5, origin-aware): a menu below its button grows from
// the button's edge and goes back there; an overlay over the window's middle keeps its course's own anchor.
import std;
import omui.view;
import mcpplibs.mtest;

using namespace omui;
using namespace mcpplibs::mtest;

namespace {

// Below its trigger, over the window's middle, or over the middle as a modal with a scrim (`top` stands for that).
template <Placement::Side side>
struct Popup {
    static constexpr std::string_view name = side == Placement::Side::below ? "origin-popup-below"
                                             : side == Placement::Side::top ? "origin-popup-modal"
                                                                            : "origin-popup-center";
    static constexpr bool overlay = true;
    Placement placement() const {
        if (side == Placement::Side::top) return {.side = Placement::Side::center, .offset = 0, .modal = true, .scrim = true, .light_dismiss = false};
        return {.side = side, .offset = 0, .light_dismiss = false};
    }
    void paint(PaintContext& context) const { context.list.fill_rect({0, 0, context.size.width, context.size.height}, Color{0.1f, 0.2f, 0.9f, 1.0f}); }
};

RuntimeOptions options(mlibs::text::FontCollection& fonts) {
    RuntimeOptions out;
    out.fonts = &fonts;
    out.size = {300, 300};
    out.theme = Theme::light();
    out.theme.motion["motion.normal"] = Transition::tween(seconds(1), hanim::ease::linear);
    return out;
}

// The popup's drawn rectangle in window coordinates (its fill under every transform above it).
std::optional<Rect> drawn(const Runtime& runtime) {
    std::vector<Affine> transforms(1);
    std::vector<bool> inside;
    for (const auto& command : runtime.display_list().commands()) {
        if (const auto* push = std::get_if<xrender::PushTransform>(&command)) transforms.push_back(transforms.back() * push->transform);
        else if (std::holds_alternative<xrender::PopTransform>(command)) {
            if (transforms.size() > 1) transforms.pop_back();
        } else if (const auto* begin = std::get_if<xrender::BeginNode>(&command)) {
            const auto id = runtime.node_at_index(begin->node);
            inside.push_back(runtime.valid(id) && runtime.path(id).ends_with("/popup"));
        } else if (std::holds_alternative<xrender::EndNode>(command)) {
            if (!inside.empty()) inside.pop_back();
        } else if (const auto* fill = std::get_if<xrender::FillRect>(&command); fill && !inside.empty() && inside.back() && fill->paint.color.b > 0.8f) {
            const Point top_left = transforms.back().apply({fill->rect.x, fill->rect.y});
            const Point bottom_right = transforms.back().apply({fill->rect.right(), fill->rect.bottom()});
            return Rect{top_left.x, top_left.y, bottom_right.x - top_left.x, bottom_right.y - top_left.y};
        }
    }
    return std::nullopt;
}

// The scrim's alpha: the fill over the whole window.
std::optional<float> scrim_alpha(const Runtime& runtime) {
    for (const auto& command : runtime.display_list().commands())
        if (const auto* fill = std::get_if<xrender::FillRect>(&command); fill && fill->rect.width >= 300 && fill->rect.height >= 300 && fill->paint.color.a < 1)
            return fill->paint.color.a;
    return std::nullopt;
}

template <Placement::Side side>
Element tree(bool open) {
    std::vector<Element> children;
    if (open) {
        Element popup;
        popup.type = &component_type<Popup<side>>();
        popup.id = "popup";
        // Enters growing from half its size, over a second, linearly.
        popup.prop("width", Source(Value(120.0))).prop("height", Source(Value(80.0)));
        popup.prop("motion", Source(Value(Value::Map{{"enter", Value(Value::Map{{"use", Value("morph")}, {"curve", Value("motion.normal")}, {"appear", Value(Value::Map{{"presence", Value("settle_in")}, {"scale", Value(0.5)}})}})},
                                                     {"exit", Value(Value::Map{{"use", Value("morph")}, {"curve", Value("motion.normal")}, {"vanish", Value(Value::Map{{"presence", Value("settle_out")}, {"scale", Value(0.5)}})}})}})));
        children.push_back(std::move(popup));
    }
    auto trigger = stack({.id = "trigger", .width = 60, .height = 30, .background = "#e11d48"}, std::move(children));
    return column({.width = 300, .height = 300, .padding = 40}, {std::move(trigger)});
}

// The popup always there, shown or hidden by `display` (how a menu or a dialog opens and closes).
Element toggled(Signal<std::string> display) {
    Element popup;
    popup.type = &component_type<Popup<Placement::Side::below>>();
    popup.id = "popup";
    popup.prop("width", Source(Value(120.0))).prop("height", Source(Value(80.0)));
    popup.prop("display", Source(derive([display] { return Value(display.get()); })));
    const Value course = Value(Value::Map{{"use", Value("morph")}, {"curve", Value("motion.normal")},
                                          {"appear", Value(Value::Map{{"presence", Value("settle_in")}, {"scale", Value(0.5)}})},
                                          {"vanish", Value(Value::Map{{"presence", Value("settle_out")}, {"scale", Value(0.5)}})}});
    popup.prop("motion", Source(Value(Value::Map{{"update", course}, {"enter", course}, {"exit", course}})));
    std::vector<Element> children;
    children.push_back(std::move(popup));
    auto trigger = stack({.id = "trigger", .width = 60, .height = 30, .background = "#e11d48"}, std::move(children));
    return column({.width = 300, .height = 300, .padding = 40}, {std::move(trigger)});
}

struct Toggled {
    mlibs::text::FontCollection fonts;
    Runtime runtime{options(fonts)};
    Signal<std::string> display = signal(std::string("none"));
    Toggled() {
        runtime.mount("main", toggled(display));
        frame(0);
        frame(0.1);
        display.set("flex");
        frame(1);
    }
    void frame(double time) { expect(runtime.frame({seconds(time), {}}).failure.empty()); }
};

template <Placement::Side side>
struct Scene {
    mlibs::text::FontCollection fonts;
    Runtime runtime{options(fonts)};
    Scene() {
        runtime.mount("main", tree<side>(false));
        frame(0);
        frame(0.1);
        runtime.mount("main", tree<side>(true));
        frame(1);
    }
    void frame(double time) { expect(runtime.frame({seconds(time), {}}).failure.empty()); }
    void open(bool shown) { runtime.mount("main", tree<side>(shown)); }
};

}  // namespace

static UTest cases{"omui.view overlay origin", [] {
    "a popup below its trigger grows from the trigger's edge: its top stays, its height grows (B5)"_test = [] {
        Scene<Placement::Side::below> scene;
        scene.frame(3);
        const auto rest = drawn(scene.runtime);
        expect(rest.has_value()) << "the popup is drawn";
        if (!rest) return;
        Scene<Placement::Side::below> half;
        half.frame(1.5);
        const auto middle = drawn(half.runtime);
        expect(middle.has_value());
        if (!middle) return;
        expect(middle->height < rest->height - 10) << std::format("half-way it is smaller: {} of {}", middle->height, rest->height);
        expect(std::abs(middle->y - rest->y) < 0.75f) << std::format("its edge by the trigger stays: {} vs {} at rest", middle->y, rest->y);
    };

    "a popup over the window's middle keeps its course's anchor: it grows about its middle"_test = [] {
        Scene<Placement::Side::center> scene;
        scene.frame(3);
        const auto rest = drawn(scene.runtime);
        Scene<Placement::Side::center> half;
        half.frame(1.5);
        const auto middle = drawn(half.runtime);
        expect(rest && middle);
        if (!rest || !middle) return;
        expect(middle->y > rest->y + 5) << "its top moves in";
        expect(std::abs((middle->y + middle->height / 2) - (rest->y + rest->height / 2)) < 0.75f) << "about its middle";
    };

    "interrupted, it goes on from where it is: closed half-way open, reopened half-way closed (B5)"_test = [] {
        // Opened at 1 s; closed at 1.5 s, half-way in: the next frame is where it was, then it shrinks back.
        Toggled closing;
        closing.frame(1.5);
        const auto before = drawn(closing.runtime);
        closing.display.set("none");
        closing.frame(1.52);
        const auto after = drawn(closing.runtime);
        expect(before && after) << "drawn on both sides of the interruption";
        if (before && after) expect(std::abs(after->height - before->height) < 2.5f) << std::format("no jump: {} then {}", before->height, after->height);
        closing.frame(1.9);
        const auto later = drawn(closing.runtime);
        if (after && later) expect(later->height < after->height) << "and it goes on closing";
        // Open, closed at 3 s, opened again at 3.5 s half-way out: from where it is, growing again.
        Toggled reopening;
        reopening.frame(3);
        reopening.display.set("none");
        reopening.frame(3.01);
        reopening.frame(3.5);
        const auto out = drawn(reopening.runtime);
        reopening.display.set("flex");
        reopening.frame(3.52);
        const auto back = drawn(reopening.runtime);
        expect(out && back) << "drawn on both sides of the interruption";
        if (out && back) expect(std::abs(back->height - out->height) < 2.5f) << std::format("no jump: {} then {}", out->height, back->height);
        reopening.frame(3.9);
        const auto grown = drawn(reopening.runtime);
        if (back && grown) expect(grown->height > back->height) << "and it grows again";
    };

    "a modal's scrim darkens on the overlay's own timeline, and clears on it (B5)"_test = [] {
        Scene<Placement::Side::top> full;
        full.frame(3);
        const auto rest = scrim_alpha(full.runtime);
        expect(rest.has_value() && *rest > 0.2f) << "a scrim at rest";
        Scene<Placement::Side::top> half;
        half.frame(1.5);
        const auto middle = scrim_alpha(half.runtime);
        if (rest && middle) expect(std::abs(*middle - *rest * 0.5f) < 0.03f) << std::format("half-way in, half as dark: {} of {}", *middle, *rest);
        full.open(false);
        full.frame(3.01);  // removed: its exit starts here
        full.frame(3.51);
        const auto leaving = scrim_alpha(full.runtime);
        if (rest && leaving) expect(std::abs(*leaving - *rest * 0.5f) < 0.03f) << std::format("half-way out, half as dark: {} of {}", *leaving, *rest);
    };
}};
