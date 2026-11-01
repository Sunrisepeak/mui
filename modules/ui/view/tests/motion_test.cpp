// Universal motion in the runtime (D-93): a root's drawing changed moves
// from what was on screen to what it draws — a child that moved, a text that
// changed glyph by glyph, an order — without a line of animation in what
// drew it; not while held by the pointer, nor with motion off; reduced, a
// short tween; settled, exactly the drawing as it is; the same at the same
// time, seekable.
import std;
import openmui.view;
import openmui.raster;
import openmui.testing;

using namespace omui::testing;
using namespace omui;

namespace {

typeset::FontCollection& fonts() {
    static typeset::FontCollection f = [] {
        typeset::FontCollection c;
        if (auto r = c.add_defaults(); !r) std::println("fonts: {}", r.error());
        return c;
    }();
    return f;
}

RuntimeOptions options(Size size = {200, 100}) {
    RuntimeOptions o;
    o.fonts = &fonts();
    o.size = size;
    return o;
}

raster::Surface draw(Runtime& rt) {
    static typeset::GlyphCache glyphs(fonts());
    raster::Surface s(static_cast<int>(rt.size().width), static_cast<int>(rt.size().height));
    render::CpuRenderer r;
    render::CpuOptions o;
    o.background = Color::white();
    (void)r.render(rt.display_list(), s, {&rt.images(), &glyphs}, o);
    return s;
}

bool same(const raster::Surface& a, const raster::Surface& b) {
    if (a.width() != b.width() || a.height() != b.height()) return false;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x)
            if (!(a.at(x, y) == b.at(x, y))) return false;
    return true;
}

// The left edge of the first pixel of `colour` in row `y` (-1: none).
int left_of(const raster::Surface& s, int y, raster::Rgba8 colour) {
    for (int x = 0; x < s.width(); ++x)
        if (s.at(x, y) == colour) return x;
    return -1;
}

// A track with a thumb at `x`: a switch's drawing, made of boxes, no animation anywhere.
Element track(Signal<float> x, std::string_view motion = "morph motion.normal") {
    Element thumb = box({.id = "thumb", .width = 20, .height = 20, .background = "#e11d48"});
    thumb.prop("translate_x", Source(derive([x] { return Value(static_cast<double>(x())); })));
    Element e = stack({.id = "track", .width = 160, .height = 20, .align = "start", .background = "#e2e8f0"}, {std::move(thumb)});
    e.prop("motion", Source(Value(std::string(motion))));
    return e;
}

// Whether the frame's list draws a layer with the named effect.
bool draws(Runtime& rt, std::string_view effect) {
    const auto ref = render::effects().find(effect);
    for (const auto& c : rt.display_list().commands())
        if (const auto* l = std::get_if<render::PushLayer>(&c); l && ref && l->effect.id == ref->id) return true;
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"view.motion"};
    const raster::Rgba8 rose{0xe1, 0x1d, 0x48, 0xff};

    s.test("a root whose drawing changed moves to it: the thumb travels, the runtime says so, and at rest it is the drawing as it is", [&] {
        auto x = signal(0.0f);
        Runtime rt(options());
        rt.mount("main", column({}, {track(x)}));
        rt.frame({seconds(0), {}});
        rt.frame({seconds(0.016), {}});
        x.set(120);
        rt.frame({seconds(0.1), {}});
        check_eq(rt.motions().size(), std::size_t{1});
        if (!rt.motions().empty()) check_eq(rt.motions()[0].path, std::string("main/track"));
        rt.frame({seconds(0.2), {}});
        const int mid = left_of(draw(rt), 10, rose);
        check(mid > 5 && mid < 115, std::format("between: {}", mid));
        const auto node = rt.find("main/track");
        check(rt.semantic_node(node).states.has(spec::State::animating), "animating while it moves");
        rt.frame({seconds(2), {}});
        check(rt.motions().empty(), "settled");
        check_eq(left_of(draw(rt), 10, rose), 120);
        check(!rt.semantic_node(node).states.has(spec::State::animating), "still once at rest");
        // The same drawing as a runtime that never moved.
        Runtime plain(options());
        auto at = signal(120.0f);
        plain.mount("main", column({}, {track(at)}));
        plain.frame({seconds(0), {}});
        check(same(draw(rt), draw(plain)), "at rest: the drawing as it is, exactly");
    });

    s.test("the same time draws the same frame: a morph is a pure function of it", [&] {
        auto run = [&](double at) {
            auto x = signal(0.0f);
            Runtime rt(options());
            rt.mount("main", column({}, {track(x)}));
            rt.frame({seconds(0), {}});
            rt.frame({seconds(0.016), {}});
            x.set(120);
            rt.frame({seconds(0.1), {}});
            rt.frame({seconds(at), {}});
            return rt.display_list().hash();
        };
        check_eq(run(0.25), run(0.25));
        check(run(0.25) != run(0.3), "and moves on");
    });

    s.test("not morphed: a node whose slot holds none, motion off; reduced: a short tween", [&] {
        {  // no motion in its slot
            auto x = signal(0.0f);
            Runtime rt(options());
            rt.mount("main", column({}, {track(x, "none")}));
            rt.frame({seconds(0), {}});
            rt.frame({seconds(0.016), {}});
            x.set(120);
            rt.frame({seconds(0.1), {}});
            check(rt.motions().empty(), "none: shown at once");
            check_eq(left_of(draw(rt), 10, rose), 120);
        }
        {  // off
            auto x = signal(0.0f);
            Runtime rt(options());
            rt.set_motion_level(MotionLevel::off);
            rt.mount("main", column({}, {track(x)}));
            rt.frame({seconds(0), {}});
            rt.frame({seconds(0.016), {}});
            x.set(120);
            rt.frame({seconds(0.1), {}});
            check(rt.motions().empty(), "off");
        }
        {  // reduced: done in an eighth of a second
            auto x = signal(0.0f);
            Runtime rt(options());
            rt.set_motion_level(MotionLevel::reduced);
            rt.mount("main", column({}, {track(x, "morph motion.spring.gentle")}));
            rt.frame({seconds(0), {}});
            rt.frame({seconds(0.016), {}});
            x.set(120);
            rt.frame({seconds(0.1), {}});
            check(!rt.motions().empty() && rt.motions()[0].end - rt.motions()[0].start < 0.13, "a short tween");
            rt.frame({seconds(0.3), {}});
            check(rt.motions().empty(), "over");
        }
    });

    s.test("a text that changed changes glyph by glyph; an order that changed travels (each item matched by its key)", [&] {
        {
            auto label = signal(std::string("Save"));
            Runtime rt(options());
            Element t = text({.id = "label", .text = label});
            t.prop("motion", Source(Value("morph")));
            rt.mount("main", column({}, {std::move(t)}));
            rt.frame({seconds(0), {}});
            rt.frame({seconds(0.016), {}});
            label.set("Saved");
            rt.frame({seconds(0.1), {}});
            check_eq(rt.motions().size(), std::size_t{1});
            if (!rt.motions().empty()) check_eq(rt.motions()[0].glyphs_kept, std::size_t{4});
        }
        {
            auto order = signal(std::vector<std::string>{"a", "b", "c"});
            Runtime rt(options());
            Element list = column({.id = "list", .gap = 4}, {dynamic([order] {
                                       std::vector<Element> rows;
                                       for (const std::string& k : order()) {
                                           Element r = box({.id = k, .width = 40, .height = 10, .background = "#334155"});
                                           r.key = k;
                                           rows.push_back(std::move(r));
                                       }
                                       return rows;
                                   })});
            list.prop("motion", Source(Value("morph motion.normal")));
            rt.mount("main", column({}, {std::move(list)}));
            rt.frame({seconds(0), {}});
            rt.frame({seconds(0.016), {}});
            order.set({"c", "a", "b"});
            rt.frame({seconds(0.1), {}});
            check_eq(rt.motions().size(), std::size_t{1});
            if (!rt.motions().empty()) {
                check_eq(rt.motions()[0].matched, std::size_t{3});
                check_eq(rt.motions()[0].from_only + rt.motions()[0].to_only, std::size_t{0});
            }
        }
    });

    s.test("the framework's defaults: a control's drawing moves (role switch), a plain box's does not", [&] {
        auto x = signal(0.0f);
        Runtime rt(options());
        Element sw = track(x);
        sw.prop("motion", Source(Value{}));
        sw.prop("role", Source(Value("switch")));
        Element plain = track(x);
        plain.prop("motion", Source(Value{}));
        plain.key = "plain";
        rt.mount("main", column({}, {std::move(sw), box({.id = "p"}, {std::move(plain)})}));
        rt.frame({seconds(0), {}});
        rt.frame({seconds(0.016), {}});
        x.set(100);
        rt.frame({seconds(0.1), {}});
        const auto m = rt.motions();
        check_eq(m.size(), std::size_t{1});
        if (!m.empty()) check_eq(m[0].path, std::string("main/track"));
    });

    s.test("enter and exit: an item inserted where the window is drawn comes in by its enter slot; removed, it stays until its exit slot's motion rests", [&] {
        auto items = signal(std::vector<std::string>{"a"});
        Runtime rt(options());
        rt.mount("main", column({.id = "list", .gap = 4}, {dynamic([items] {
                                    std::vector<Element> rows;
                                    for (const std::string& k : items()) {
                                        Element r = box({.id = k, .width = 40, .height = 10, .background = "#334155"});
                                        r.key = k;
                                        r.prop("motion", Source(Value(Value::Map{{"enter", Value("morph motion.normal")}, {"exit", Value("morph motion.normal")}})));
                                        rows.push_back(std::move(r));
                                    }
                                    return rows;
                                })}));
        rt.frame({seconds(0), {}});
        rt.frame({seconds(0.016), {}});
        check(rt.motions().empty(), "what was there from the start does not enter");
        items.set({"a", "b"});
        rt.frame({seconds(0.1), {}});
        check(rt.motions().size() == 1 && rt.motions()[0].path == "main/b" && rt.motions()[0].to_only == 1, "b enters");
        rt.frame({seconds(1), {}});
        check(rt.motions().empty(), "in");
        const NodeId a = rt.find("main/a");
        items.set({"b"});
        rt.frame({seconds(1.1), {}});
        rt.frame({seconds(1.12), {}});
        check(rt.valid(a) && rt.semantic_node(a).states.has(spec::State::removing), "a is leaving");
        check(rt.motions().size() == 1 && rt.motions()[0].from_only == 1, "a leaves by its motion");
        for (double t = 1.2; t < 2.5; t += 0.1) rt.frame({seconds(t), {}});
        check(!rt.valid(a), "gone once it rests");
    });

    s.test("one switch, three motions (use case 7): the first that fits, the person's choice among them, and below their tiers the one that does", [&] {
        auto x = signal(0.0f);
        Runtime rt(options());
        Element sw = track(x);
        sw.prop("motion", Source(Value(Value::List{Value("liquid"), Value("ink"), Value("simple")})));
        rt.mount("main", column({}, {std::move(sw)}));
        rt.frame({seconds(0), {}});
        rt.frame({seconds(0.016), {}});
        // Moved at `at`, looked at a moment later: which travel effect it goes through, and how long it takes.
        auto move = [&](double at, float to, std::string_view effect) {
            x.set(to);
            rt.frame({seconds(at), {}});
            rt.frame({seconds(at + 0.06), {}});
            const bool through = effect.empty() ? !draws(rt, "metaball") && !draws(rt, "wobble") : draws(rt, effect);
            const double length = rt.motions().empty() ? 0 : rt.motions()[0].end - rt.motions()[0].start;
            for (double t = at + 0.1; t < at + 2.9; t += 0.1) rt.frame({seconds(t), {}});
            check(rt.motions().empty(), "at rest");
            return std::pair{through, length};
        };
        const auto liquid = move(0.1, 100, "metaball");
        check(liquid.first, "the first that fits: liquid, its drops merging on the way");
        rt.set_motion_choice("ink");
        const auto ink = move(3.1, 0, "wobble");
        check(ink.first, "the person chose ink: its line wavering on the way");
        check(ink.second > 0.4 && ink.second < 0.6, std::format("ink is unhurried: {}", ink.second));
        rt.set_motion_choice("simple");
        const auto simple = move(6.1, 100, "");
        check(simple.first && simple.second < 0.2, std::format("simple: no effect, short: {}", simple.second));
        // An embedded board: neither drops nor ink run there — the one that fits, whatever was chosen.
        rt.set_motion_choice("ink");
        rt.set_device_tier(pipeline::Tier::t1, "an embedded board");
        const auto board = move(9.1, 0, "");
        check(board.first && board.second < 0.2, std::format("on T1, simple: {}", board.second));
        check(!draws(rt, "metaball") && !draws(rt, "wobble"), "at rest, no travel effect anywhere");
    });

    s.test("a change's slot: a state (focus shown, nothing written), an order, a replacement — each its own; one holding none moves by the update slot's", [&] {
        {
            Runtime rt(options());
            Element b = box({.id = "b", .width = 60, .height = 20, .background = "#334155"});
            b.prop("focusable", Source(Value(true)));
            b.prop("motion", Source(Value(Value::Map{{"update", Value("none")}, {"state", Value("simple")}})));
            rt.mount("main", column({.padding = 8}, {std::move(b)}));
            rt.frame({seconds(0), {}});
            rt.frame({seconds(0.016), {}});
            rt.focus_visibly(rt.find("main/b"));
            rt.frame({seconds(0.1), {}});
            check(rt.motions().size() == 1 && rt.motions()[0].end - rt.motions()[0].start < 0.2, "its ring comes in by the state slot's tween");
        }
        auto order = signal(std::vector<std::string>{"a", "b", "c"});
        auto list = [&](Value motion) {
            Element l = column({.id = "list", .gap = 4}, {dynamic([order] {
                                   std::vector<Element> rows;
                                   for (const std::string& k : order()) {
                                       Element r = box({.id = k, .width = 40, .height = 10, .background = "#334155"});
                                       r.key = k;
                                       rows.push_back(std::move(r));
                                   }
                                   return rows;
                               })});
            l.prop("motion", Source(std::move(motion)));
            return l;
        };
        {
            order.set({"a", "b", "c"});
            Runtime rt(options());
            rt.mount("main", column({}, {list(Value(Value::Map{{"update", Value("none")}, {"reorder", Value("morph motion.normal")}}))}));
            rt.frame({seconds(0), {}});
            rt.frame({seconds(0.016), {}});
            order.set({"c", "a", "b"});
            rt.frame({seconds(0.1), {}});
            check_eq(rt.motions().size(), std::size_t{1});
            for (double t = 0.2; t < 2; t += 0.1) rt.frame({seconds(t), {}});
            order.set({"x", "y"});
            rt.frame({seconds(2.1), {}});
            check(rt.motions().empty(), "a replacement is not an order: the update slot holds none");
        }
        {
            order.set({"a", "b", "c"});
            Runtime rt(options());
            rt.mount("main", column({}, {list(Value(Value::Map{{"update", Value("morph motion.normal")}}))}));
            rt.frame({seconds(0), {}});
            rt.frame({seconds(0.016), {}});
            order.set({"x", "y"});
            rt.frame({seconds(0.1), {}});
            check(rt.motions().size() == 1 && rt.motions()[0].from_only == 3 && rt.motions()[0].to_only == 2,
                  "a slot holding none of its own (replace) moves by the update slot's");
        }
    });

    s.test("a handle on a morph: paused, sought, reversed — its own time; kept at rest while held; released, on from where it is", [&] {
        auto x = signal(0.0f);
        Runtime rt(options());
        rt.mount("main", column({}, {track(x)}));
        rt.frame({seconds(0), {}});
        rt.frame({seconds(0.016), {}});
        const auto before = draw(rt);
        x.set(100);
        rt.frame({seconds(0.1), {}});
        const NodeId t = rt.find("main/track");
        check(rt.hold_motion(t), "a morph under way is held");
        rt.pause_motion(t);
        rt.frame({seconds(0.2), {}});
        const auto paused = draw(rt);
        rt.frame({seconds(0.5), {}});
        check(same(paused, draw(rt)), "paused: the same drawing as time goes on");
        rt.seek_motion(t, 0);
        rt.frame({seconds(0.6), {}});
        check(same(before, draw(rt)), "sought to its start: the first drawing");
        const auto length = rt.motion_time(t);
        check(length && length->first > 0.1, "its length");
        rt.seek_motion(t, length ? length->first : 1);
        rt.frame({seconds(0.7), {}});
        const auto rest = draw(rt);
        rt.frame({seconds(3), {}});
        check(rt.motion_time(t).has_value(), "at rest, held: kept");
        rt.set_motion_rate(t, -1);
        rt.play_motion(t);
        rt.frame({seconds(3.1), {}});
        const int back = left_of(draw(rt), 10, rose);
        check(back >= 0 && back < left_of(rest, 10, rose), std::format("backwards: the thumb going back ({} < {})", back, left_of(rest, 10, rose)));
        rt.release_motion(t);
        for (double u = 3.2; u < 6; u += 0.1) rt.frame({seconds(u), {}});
        check(!rt.motion_time(t), "released: on to rest, and done");
        check(same(rest, draw(rt)), "at rest: the drawing as it is");
    });

    return s.run(argc, argv);
}
