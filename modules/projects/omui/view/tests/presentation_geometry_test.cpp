// The actual sampled border box is shared by pixels, hit testing and semantics.
// NodeFrame is metadata only; it cannot become another visible staggered piece.
import std;
import openmui.view;
import openmui.motion;
import xrender.raster;
import mlibs.testing;
import xrender.text;

using namespace omui;
using namespace mlibs::testing;
namespace mo = omui::motion;

namespace {
Theme theme() {
    auto value = Theme::light(); value.motion["motion.normal"] = Transition::tween(seconds(1), hanim::ease::linear); return value;
}
RuntimeOptions options(mlibs::text::FontCollection& fonts, Stages stages = Stages::paint) {
    RuntimeOptions value; value.theme = theme(); value.fonts = &fonts; value.size = {260, 180}; value.stages = stages; return value;
}
PointerEvent pointer(PointerKind kind, Point point) {
    PointerEvent event; event.kind = kind; event.position = point; event.button = MouseButton::left; event.buttons = kind == PointerKind::down ? 1u : 0u; return event;
}
bool near(Rect a, Rect b, float tolerance = 0.02f) {
    return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance && std::abs(a.width - b.width) <= tolerance && std::abs(a.height - b.height) <= tolerance;
}
bool pixels_equal(const xrender::raster::Surface& a, const xrender::raster::Surface& b) { return a.width() == b.width() && a.height() == b.height() && std::ranges::equal(a.pixels(), b.pixels()); }
struct Fixture {
    mlibs::text::FontCollection fonts;
    Runtime runtime{options(fonts)};
    xrender::text::GlyphCache glyphs{fonts};
    Signal<float> x = signal(10.0f);
    int presses = 0;
    Point local{-1, -1};
    explicit Fixture(bool painted = true, bool clip = false, float rotation = 0) {
        auto button = box({.id = "control", .width = 60, .height = 30, .background = painted ? "#e11d48" : "transparent",
            .on = {{"press", [this](const Value&) { ++presses; }}, {"pointer_down", [this](const Value& value) { local = {static_cast<float>(value.get("x")->as_double()), static_cast<float>(value.get("y")->as_double())}; }}}});
        button.prop("translate_x", Source(derive([value = x] { return Value(static_cast<double>(value.get())); })));
        button.prop("motion", Source(Value("morph motion.normal")));
        if (rotation != 0) button.prop("rotation", Source(Value(rotation)));
        auto region = stack({.id = "region", .width = 240, .height = 140, .padding = Edges(10), .radius = clip ? 24.0f : 0.0f, .clip = clip}, {std::move(button)});
        runtime.mount("main", stack({.width = 260, .height = 180}, {std::move(region)})); frame(0); frame(0.1);
    }
    NodeId node() const { return runtime.find("main/region/control"); }
    void frame(double time) { check(runtime.frame({seconds(time), {}}).failure.empty()); }
    void move(double sample = 1.4) { x.set(140); frame(1); frame(sample); }
    void click(Point point, double time) { check(runtime.frame({seconds(time), {pointer(PointerKind::move, point), pointer(PointerKind::down, point), pointer(PointerKind::up, point)}}).failure.empty()); }
    Rect ink() {
        xrender::raster::Surface surface(260, 180); xrender::CpuRenderer renderer; xrender::CpuOptions opts; opts.object_ids = true;
        const auto report = renderer.render(runtime.display_list(), surface, {&runtime.images(), &glyphs}, opts); check(report.missing_glyphs == 0);
        const auto owners = renderer.object_ids();
        int left = 260, top = 180, right = -1, bottom = -1;
        for (int y = 0; y < 180; ++y) for (int x = 0; x < 260; ++x)
            if (runtime.node_at_index(owners[static_cast<std::size_t>(y) * 260 + x]) == node()) { left = std::min(left, x); top = std::min(top, y); right = std::max(right, x); bottom = std::max(bottom, y); }
        return right < left ? Rect{} : Rect{static_cast<float>(left), static_cast<float>(top), static_cast<float>(right - left + 1), static_cast<float>(bottom - top + 1)};
    }
};
xrender::DisplayList drawing(bool frames, bool end) {
    xrender::DisplayList list; list.begin_node(1);
    if (frames) list.node_frame(1, {0, 0, 180, 120});
    for (std::uint32_t i = 0; i < 3; ++i) {
        list.begin_node(i + 2); list.push_transform(Affine::translate(end ? 60.0f : static_cast<float>(i) * 45, end ? static_cast<float>(i) * 30 : 10));
        if (frames) list.node_frame(i + 2, {0, 0, end ? 30.0f : 20.0f, 20});
        list.fill_round_rect({0, 0, end ? 30.0f : 20.0f, 20}, CornerRadii::all(4), xrender::Paint::solid(Color::hex(0xe11d48)));
        list.pop_transform(); list.end_node();
    }
    list.end_node(); return list;
}
}

int main(int argc, char** argv) {
    Suite suite{"view.presentation_geometry"};
    suite.test("metadata changes neither pixels visible matching nor document/distance stagger cadence", [] {
        for (const auto order : {mo::Order::document, mo::Order::distance}) {
            mo::Resolver resolver{.keys = [](std::uint32_t id, std::string& key, std::string&) { key = std::to_string(id); }};
            auto a = drawing(false, false), b = drawing(false, true), ma = drawing(true, false), mb = drawing(true, true);
            auto plain_a = mo::Matter::extract(a, resolver), plain_b = mo::Matter::extract(b, resolver);
            auto meta_a = mo::Matter::extract(ma, resolver), meta_b = mo::Matter::extract(mb, resolver);
            check(plain_a.pieces().size() == meta_a.pieces().size() && plain_b.pieces().size() == meta_b.pieces().size() && plain_b.bounds() == meta_b.bounds(), "border metadata is outside visual pieces, bounds and tier counts");
            mo::Options config; config.curve.kind = mo::Curve::Kind::tween; config.curve.duration = 1; config.curve.ease = hanim::ease::linear;
            config.order = order; config.stagger = 0.07; config.stretch = 0.1f;
            mo::Morph plain(std::move(plain_a), std::move(plain_b), config, 0), meta(std::move(meta_a), std::move(meta_b), config, 0);
            check(plain.end() == meta.end() && plain.stats().matched == meta.stats().matched && plain.stats().from_only == meta.stats().from_only && plain.stats().to_only == meta.stats().to_only, "metadata contributes neither matching statistics nor independent stagger ranks");
            for (const double time : {0.0, 0.02, 0.07, 0.2, 0.49, 0.8, 1.2, 2.0}) {
                xrender::DisplayList p, m; plain.draw(time, p); meta.draw(time, m); check(p.balanced() && m.balanced());
                xrender::raster::Surface ps(180, 120), ms(180, 120); xrender::CpuRenderer renderer;
                (void)renderer.render(p, ps, {}, {}); (void)renderer.render(m, ms, {}, {});
                check(pixels_equal(ps, ms), "enabling NodeFrame metadata preserves every native pixel at the same sampled time");
            }
        }
    });
    suite.test("sampled opaque borders hit their native painted center while target screen_bounds stays unchanged", [] {
        Fixture f; f.move(); const auto id = f.node();
        const auto presented = f.runtime.drawn_bounds(id), target = f.runtime.screen_bounds(id), painted = f.ink();
        check(!near(presented, target) && near(presented, painted, 1.1f), "drawn_bounds follows the actually painted border, not target placement or glyph ink");
        check(f.runtime.hit_test(presented.center()) == id && f.runtime.hit_test(target.center()) != id, "real hit testing uses the same sampled border");
        const auto semantic = f.runtime.semantic_node(id); check(semantic.bounds.has_value());
        if (semantic.bounds) check(near({semantic.bounds->x, semantic.bounds->y, semantic.bounds->width, semantic.bounds->height}, presented), "semantic bounds describe currently visible coordinates");
        f.click(presented.center(), 1.4); check(f.presses == 1 && std::abs(f.local.x - 30) < 0.1f && std::abs(f.local.y - 15) < 0.1f, "real pointer activation and local coordinates agree with the sampled center");
        check(near(f.runtime.screen_bounds(id), target), "sampling or clicking does not rewrite the target geometry API");
    });
    suite.test("transparent controls carry a border independently of painted fills or glyphs", [] {
        Fixture f(false); f.move(1.3); const auto id = f.node();
        const auto actual = f.runtime.drawn_bounds(id); check(!actual.empty() && !near(actual, f.runtime.screen_bounds(id)));
        check(f.runtime.hit_test(actual.center()) == id, "a transparent control remains operable at its current border");
        f.click(actual.center(), 1.3); check(f.presses == 1, "transparent frame metadata supports a real click without inventing a fill");
    });
    suite.test("interruption resize and explicit hold seek share one sampled presentation inverse", [] {
        Fixture f; f.move(1.25); const auto id = f.node();
        f.x.set(40); f.runtime.resize({220, 180}, 1); f.runtime.set(id, "width", Value(90.0)); f.frame(1.3); f.frame(1.55);
        const auto actual = f.runtime.drawn_bounds(id); check(near(actual, f.ink(), 1.1f) && f.runtime.hit_test(actual.center()) == id);
        f.click(actual.center(), 1.55); check(f.presses == 1 && std::abs(f.local.x - 45) < 0.15f, "the event inverse normalizes a sampled changing width into the component's current local box");
        check(f.runtime.hold_motion(id)); f.runtime.pause_motion(id); f.runtime.seek_motion(id, 0.2); f.frame(1.6);
        const auto sought = f.runtime.drawn_bounds(id); check(near(sought, f.ink(), 1.1f) && f.runtime.hit_test(sought.center()) == id);
        f.runtime.release_motion(id); f.frame(4); f.frame(4.01);
        check(near(f.runtime.drawn_bounds(id), f.runtime.screen_bounds(id)) && !f.runtime.motion_time(id), "settling restores the target border without a stale presented box");
    });
    suite.test("real row reflow and independently moving nested roots retain their actual pointer geometry", [] {
        mlibs::text::FontCollection fonts; Runtime runtime(options(fonts)); xrender::text::GlyphCache glyphs(fonts);
        auto room = signal(230.0f), inside = signal(0.0f); int pressed = 0;
        auto control = box({.id = "control", .width = 70, .height = 30, .background = "#e11d48", .on = {{"press", [&](const Value&) { ++pressed; }}}});
        control.prop("translate_x", Source(derive([inside] { return Value(static_cast<double>(inside.get())); }))).prop("motion", Source(Value("morph motion.normal")));
        auto card = stack({.id = "card", .width = 100, .height = 40}, {std::move(control)});
        card.prop("motion", Source(Value("morph motion.normal")));
        auto flow = row({.id = "flow", .width = 230, .height = 140, .gap = 10, .wrap = "wrap"}, {
            box({.id = "first", .width = 100, .height = 40, .background = "#0891b2"}), std::move(card)});
        flow.prop("width", Source(derive([room] { return Value(static_cast<double>(room.get())); }))).prop("motion", Source(Value("morph motion.normal")));
        runtime.mount("main", stack({.width = 260, .height = 180}, {std::move(flow)})); runtime.frame({seconds(0), {}}); runtime.frame({seconds(0.1), {}});
        room.set(120); inside.set(10); runtime.frame({seconds(1), {}}); runtime.frame({seconds(1.35), {}});
        const auto id = runtime.find("main/flow/card/control");
        const auto actual = runtime.drawn_bounds(id), target = runtime.screen_bounds(id);
        check(!near(actual, target) && runtime.hit_test(actual.center()) == id, "a nested root hit follows sampled reflow instead of the new row/column allocation");
        xrender::raster::Surface pixels(260, 180); xrender::CpuRenderer renderer; xrender::CpuOptions opts; opts.object_ids = true;
        (void)renderer.render(runtime.display_list(), pixels, {&runtime.images(), &glyphs}, opts);
        const auto point = actual.center(); const auto owner = renderer.object_ids()[static_cast<std::size_t>(static_cast<int>(point.y)) * 260 + static_cast<int>(point.x)];
        check(runtime.node_at_index(owner) == id, "the sampled border's center really belongs to the painted nested control");
        runtime.frame({seconds(1.35), {pointer(PointerKind::down, point), pointer(PointerKind::up, point)}});
        check(pressed == 1, "the reflow sample accepts actual pointer input without a semantic act shortcut");
        runtime.frame({seconds(4), {}}); runtime.frame({seconds(4.01), {}});
        check(near(runtime.drawn_bounds(id), runtime.screen_bounds(id)), "nested reflow settles to its target allocation");
    });
    suite.test("rotation rejects AABB corners and inherited rounded clips constrain current pointer geometry", [] {
        Fixture rotated(true, false, 35); rotated.move(1.4); const auto box = rotated.runtime.drawn_bounds(rotated.node());
        check(rotated.runtime.hit_test(box.center()) == rotated.node());
        check(rotated.runtime.hit_test({box.x + 0.5f, box.y + 0.5f}) != rotated.node(), "rotation uses the actual inverse instead of accepting every point in its AABB");
        Fixture clipped(true, true); clipped.x.set(-25); clipped.frame(1); clipped.frame(2.2);
        const auto id = clipped.node(), region = clipped.runtime.find("main/region");
        const auto border = clipped.runtime.drawn_bounds(id), clip = clipped.runtime.drawn_bounds(region);
        const auto state = clipped.runtime.semantic_node(id); check(state.bounds.has_value());
        if (state.bounds) check(near({state.bounds->x, state.bounds->y, state.bounds->width, state.bounds->height}, border),
                               "semantic geometry preserves the current full border so a reader can reveal a clipped object");
        check(clipped.runtime.hit_test({border.x + 1, border.center().y}) != id, "the old target parent box cannot expose a clipped presented control");
        check(clipped.runtime.hit_test({clip.x + 0.2f, clip.y + 0.2f}) != id, "rounded ancestor corners are checked in their presented coordinate system");
    });
    suite.test("history metadata never overrides current geometry and unseen nodes cannot retain old hit boxes", [] {
        Fixture f, plain; f.move(1.25); plain.move(1.25);
        f.runtime.crossfade(seconds(0.8)); f.x.set(90); plain.x.set(90); f.frame(1.3); plain.frame(1.3); f.frame(1.6); plain.frame(1.6);
        check(near(f.runtime.drawn_bounds(f.node()), plain.runtime.drawn_bounds(plain.node())), "retained history is excluded from presentation collection");
        const auto old = f.runtime.drawn_bounds(f.node()); f.runtime.set(f.node(), "visible", Value(false)); f.frame(1.7);
        check(f.runtime.drawn_bounds(f.node()).empty() && f.runtime.hit_test(old.center()) != f.node(), "a node absent from the new current frame cannot be hit through its old border");
    });
    suite.test("layout-only pipelines retain target fallback without requiring a renderer or metadata pass", [] {
        mlibs::text::FontCollection fonts; Runtime runtime(options(fonts, Stages::layout));
        runtime.mount("main", box({.width = 60, .height = 30, .translate_x = 25})); runtime.frame({seconds(0), {}});
        const auto id = runtime.find("main"); check(near(runtime.drawn_bounds(id), runtime.screen_bounds(id)));
        check(runtime.hit_test(runtime.drawn_bounds(id).center()) == id && runtime.semantic_node(id).bounds.has_value());
    });
    return suite.run(argc, argv);
}
