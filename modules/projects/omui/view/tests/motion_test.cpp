// Universal motion in the runtime (D-93): a root's drawing changed moves
// from what was on screen to what it draws — a child that moved, a text that
// changed glyph by glyph, an order — without a line of animation in what
// drew it; not while held by the pointer, nor with motion off; reduced, a
// short tween; settled, exactly the drawing as it is; the same at the same
// time, seekable.
import std;
import omui.view;
import xrender.raster;
import mcpplibs.mtest;
import xrender.text;

using namespace mcpplibs::mtest;
using namespace omui;

namespace {

mlibs::text::FontCollection& fonts() {
    static mlibs::text::FontCollection f = [] {
        mlibs::text::FontCollection c;
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

xrender::raster::Surface draw(Runtime& rt) {
    static xrender::text::GlyphCache glyphs(fonts());
    xrender::raster::Surface s(static_cast<int>(rt.size().width), static_cast<int>(rt.size().height));
    xrender::CpuRenderer r;
    xrender::CpuOptions o;
    o.background = Color::white();
    (void)r.render(rt.display_list(), s, {&rt.images(), &glyphs}, o);
    return s;
}

bool same(const xrender::raster::Surface& a, const xrender::raster::Surface& b) {
    if (a.width() != b.width() || a.height() != b.height()) return false;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x)
            if (!(a.at(x, y) == b.at(x, y))) return false;
    return true;
}

// The left edge of the first pixel of `colour` in row `y` (-1: none).
int left_of(const xrender::raster::Surface& s, int y, xrender::raster::Rgba8 colour) {
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
    const auto ref = xrender::effects().find(effect);
    for (const auto& c : rt.display_list().commands())
        if (const auto* l = std::get_if<xrender::PushLayer>(&c); l && ref && l->effect.id == ref->id) return true;
    return false;
}

}  // namespace

UTest cases{"view.motion", [] {
    const xrender::raster::Rgba8 rose{0xe1, 0x1d, 0x48, 0xff};

    "a root whose drawing changed moves to it: the thumb travels, the runtime says so, and at rest it is the drawing as it is"_test = [&] {
        auto x = signal(0.0f);
        Runtime rt(options());
        rt.mount("main", column({}, {track(x)}));
        rt.frame({seconds(0), {}});
        rt.frame({seconds(0.016), {}});
        x.set(120);
        rt.frame({seconds(0.1), {}});
        expect(eq(rt.motions().size(), std::size_t{1}));
        if (!rt.motions().empty()) expect(eq(rt.motions()[0].path, std::string("main/track")));
        rt.frame({seconds(0.2), {}});
        const int mid = left_of(draw(rt), 10, rose);
        expect(mid > 5 && mid < 115) << std::format("between: {}", mid);
        const auto node = rt.find("main/track");
        expect(rt.semantic_node(node).states.has(spec::State::animating)) << "animating while it moves";
        rt.frame({seconds(2), {}});
        expect(rt.motions().empty()) << "settled";
        expect(eq(left_of(draw(rt), 10, rose), 120));
        expect(!rt.semantic_node(node).states.has(spec::State::animating)) << "still once at rest";
        // The same drawing as a runtime that never moved.
        Runtime plain(options());
        auto at = signal(120.0f);
        plain.mount("main", column({}, {track(at)}));
        plain.frame({seconds(0), {}});
        expect(same(draw(rt), draw(plain))) << "at rest: the drawing as it is, exactly";
    };

    "the same time draws the same frame: a morph is a pure function of it"_test = [&] {
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
        expect(eq(run(0.25), run(0.25)));
        expect(run(0.25) != run(0.3)) << "and moves on";
    };

    "not morphed: a node whose slot holds none, motion off; reduced: a short tween"_test = [&] {
        {  // no motion in its slot
            auto x = signal(0.0f);
            Runtime rt(options());
            rt.mount("main", column({}, {track(x, "none")}));
            rt.frame({seconds(0), {}});
            rt.frame({seconds(0.016), {}});
            x.set(120);
            rt.frame({seconds(0.1), {}});
            expect(rt.motions().empty()) << "none: shown at once";
            expect(eq(left_of(draw(rt), 10, rose), 120));
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
            expect(rt.motions().empty()) << "off";
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
            expect(!rt.motions().empty() && rt.motions()[0].end - rt.motions()[0].start < 0.13) << "a short tween";
            rt.frame({seconds(0.3), {}});
            expect(rt.motions().empty()) << "over";
        }
    };

    "text.reshape: a replaced letter's real outline becomes the new letter's, holes and all, then is the glyph"_test = [&] {
        auto label = signal(std::string("cAt"));
        Runtime rt(options());
        Element t = text({.id = "label", .text = label, .size = 48.0f});
        t.prop("motion", Source(Value("text.reshape")));
        rt.mount("main", column({}, {std::move(t)}));
        rt.frame({seconds(0), {}});
        rt.frame({seconds(0.016), {}});
        label.set("cBt");
        rt.frame({seconds(0.1), {}});
        const auto most_contours = [&] {
            std::size_t most = 0;
            for (const auto& command : rt.display_list().commands())
                if (const auto* fill = std::get_if<xrender::FillPath>(&command))
                    most = std::max(most, static_cast<std::size_t>(std::ranges::count(fill->path.verbs, xrender::PathVerb::move)));
            return most;
        };
        rt.frame({seconds(0.25), {}});
        // A's outline and hole, B's outline and two holes: the hole pairs, the second grows.
        expect(most_contours() >= 3) << std::format("mid-way one reshaping outline of {} contours", most_contours());
        expect(!rt.motions().empty() && rt.motions()[0].glyphs_kept == 2) << "c and t kept";
        rt.frame({seconds(4), {}});
        expect(rt.motions().empty() && most_contours() == 0) << "settled: the glyphs as drawn";
    };

    "a text that changed changes glyph by glyph; an order that changed travels (each item matched by its key)"_test = [&] {
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
            expect(eq(rt.motions().size(), std::size_t{1}));
            if (!rt.motions().empty()) expect(eq(rt.motions()[0].glyphs_kept, std::size_t{4}));
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
            expect(eq(rt.motions().size(), std::size_t{1}));
            if (!rt.motions().empty()) {
                expect(eq(rt.motions()[0].matched, std::size_t{3}));
                expect(eq(rt.motions()[0].from_only + rt.motions()[0].to_only, std::size_t{0}));
            }
        }
    };

    "the framework's defaults: a control's drawing moves (role switch), a plain box's does not"_test = [&] {
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
        expect(eq(m.size(), std::size_t{1}));
        if (!m.empty()) expect(eq(m[0].path, std::string("main/track")));
    };

    "enter and exit: an item inserted where the window is drawn comes in by its enter slot; removed, it stays until its exit slot's motion rests"_test = [&] {
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
        expect(rt.motions().empty()) << "what was there from the start does not enter";
        items.set({"a", "b"});
        rt.frame({seconds(0.1), {}});
        expect(rt.motions().size() == 1 && rt.motions()[0].path == "main/b" && rt.motions()[0].to_only == 1) << "b enters";
        rt.frame({seconds(1), {}});
        expect(rt.motions().empty()) << "in";
        const NodeId a = rt.find("main/a");
        items.set({"b"});
        rt.frame({seconds(1.1), {}});
        rt.frame({seconds(1.12), {}});
        expect(rt.valid(a) && rt.semantic_node(a).states.has(spec::State::removing)) << "a is leaving";
        expect(rt.motions().size() == 1 && rt.motions()[0].from_only == 1) << "a leaves by its motion";
        expect(rt.hold_motion(a)); rt.pause_motion(a);
        rt.frame({seconds(1.14), {}});
        const auto paused_exit = rt.frame({seconds(1.16), {}});
        expect(paused_exit.demand.idle() && !paused_exit.animating) << "a paused exit stays seekable without requesting more frames";
        expect(rt.valid(a)) << "retained exit material is not destroyed while its handle is paused";
        rt.release_motion(a);
        for (double t = 1.2; t < 2.5; t += 0.1) rt.frame({seconds(t), {}});
        expect(!rt.valid(a)) << "gone once it rests";
    };

    "one switch, three motions (use case 7): the first that fits, the person's choice among them, and below their tiers the one that does"_test = [&] {
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
            expect(rt.motions().empty()) << "at rest";
            return std::pair{through, length};
        };
        const auto liquid = move(0.1, 100, "metaball");
        expect(liquid.first) << "the first that fits: liquid, its drops merging on the way";
        rt.set_motion_choice("ink");
        const auto ink = move(3.1, 0, "wobble");
        expect(ink.first) << "the person chose ink: its line wavering on the way";
        expect(ink.second > 0.4 && ink.second < 0.6) << std::format("ink is unhurried: {}", ink.second);
        rt.set_motion_choice("simple");
        const auto simple = move(6.1, 100, "");
        expect(simple.first && simple.second < 0.2) << std::format("simple: no effect, short: {}", simple.second);
        // An embedded board: neither drops nor ink run there — the one that fits, whatever was chosen.
        rt.set_motion_choice("ink");
        rt.set_device_tier(mlibs::pipeline::Tier::t1, "an embedded board");
        const auto board = move(9.1, 0, "");
        expect(board.first && board.second < 0.2) << std::format("on T1, simple: {}", board.second);
        expect(!draws(rt, "metaball") && !draws(rt, "wobble")) << "at rest, no travel effect anywhere";
    };

    "trace.focus writes a focus ring round as it appears"_test = [&] {
        {
            Runtime rt(options());
            Element b = box({.id = "b", .width = 60, .height = 20, .background = "#334155"});
            b.prop("focusable", Source(Value(true)));
            b.prop("motion", Source(Value(Value::Map{{"update", Value("none")}, {"state", Value("trace.focus")}})));
            rt.mount("main", column({.padding = 8}, {std::move(b)}));
            rt.frame({seconds(0), {}});
            rt.frame({seconds(0.016), {}});
            rt.focus_visibly(rt.find("main/b"));
            rt.frame({seconds(0.02), {}});
            rt.frame({seconds(0.1), {}});
            float written = -1;
            for (const auto& command : rt.display_list().commands())
                if (const auto* stroke = std::get_if<xrender::StrokePath>(&command); stroke && stroke->style.trim_end < 1) written = stroke->style.trim_end;
            expect(written > 0 && written < 1) << std::format("the ring part drawn: to {} of its length", written);
            rt.frame({seconds(1), {}});
            expect(rt.motions().empty()) << "then the ring as it is";
        }
    };

    "a change's slot: a state (focus shown, nothing written), an order, a replacement — each its own; one holding none moves by the update slot's"_test = [&] {
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
            expect(rt.motions().size() == 1 && rt.motions()[0].end - rt.motions()[0].start < 0.2) << "its ring comes in by the state slot's tween";
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
            expect(eq(rt.motions().size(), std::size_t{1}));
            for (double t = 0.2; t < 2; t += 0.1) rt.frame({seconds(t), {}});
            order.set({"x", "y"});
            rt.frame({seconds(2.1), {}});
            expect(rt.motions().empty()) << "a replacement is not an order: the update slot holds none";
        }
        {
            order.set({"a", "b", "c"});
            Runtime rt(options());
            rt.mount("main", column({}, {list(Value(Value::Map{{"update", Value("morph motion.normal")}}))}));
            rt.frame({seconds(0), {}});
            rt.frame({seconds(0.016), {}});
            order.set({"x", "y"});
            rt.frame({seconds(0.1), {}});
            expect(rt.motions().size() == 1 && rt.motions()[0].from_only == 3 && rt.motions()[0].to_only == 2) << "a slot holding none of its own (replace) moves by the update slot's";
        }
    };

    "composition applies every reusable operation while a strategy preserves separate fallback candidates"_test = [] {
        const Theme theme = Theme::light();
        const auto composed = parse_motion(Value{}.set("compose", Value(Value::List{"match", "glyphs", "fade", "check-grow", "stretch", "stagger"})), theme);
        expect(composed.has_value());
        if (!composed) return;
        using P = MotionPrimitive;
        const auto contains = [&](P part) { return (composed->primitives & static_cast<std::uint16_t>(part)) != 0; };
        expect(contains(P::match_drawing) && contains(P::match_glyphs) && contains(P::fade) && contains(P::check_grow) && contains(P::stretch) && contains(P::stagger));
        expect(composed->glyphs && composed->local_presence && composed->appear.away.scale.x < 1 && composed->stretch > 0 && composed->stagger > 0);
        const auto candidates = parse_motions(Value(Value::List{"liquid", "simple"}), theme);
        expect(candidates && candidates->size() == 2 && candidates->front().travel && !candidates->back().travel) << "a fallback list must not silently compose incompatible strategies";
        expect(!parse_motion(Value{}.set("compose", Value(Value::List{"match", "unknown-operation"})), theme));
        expect(!parse_motion(Value{}.set("compose", "match"), theme));
        expect(!parse_motion(Value{}.set("origin", "unknown-origin"), theme));
        const auto local = parse_motion(Value{}.set("compose", Value(Value::List{"match", "grow"})).set("origin", "piece"), theme);
        expect(local && local->local_presence) << "the same grow operation can apply about each real piece";
    };

    "a handle on a morph: paused, sought, reversed — its own time; kept at rest while held; released, on from where it is"_test = [&] {
        auto x = signal(0.0f);
        Runtime rt(options());
        rt.mount("main", column({}, {track(x)}));
        rt.frame({seconds(0), {}});
        rt.frame({seconds(0.016), {}});
        const auto before = draw(rt);
        x.set(100);
        rt.frame({seconds(0.1), {}});
        const NodeId t = rt.find("main/track");
        expect(rt.hold_motion(t)) << "a morph under way is held";
        rt.pause_motion(t);
        rt.frame({seconds(0.2), {}});
        expect(!rt.semantic_node(t).states.has(spec::State::animating)) << "a paused handle is stationary to the semantic reader too";
        const auto paused = draw(rt);
        const auto idle = rt.frame({seconds(0.5), {}});
        expect(same(paused, draw(rt))) << "paused: the same drawing as time goes on";
        expect(rt.frame_demand().idle()) << "a paused retained morph does not request animation frames";
        expect(!idle.animating) << "paused drawing must not dirty the next frame unconditionally";
        rt.seek_motion(t, 0);
        rt.frame({seconds(0.6), {}});
        expect(same(before, draw(rt))) << "sought to its start: the first drawing";
        const auto length = rt.motion_time(t);
        expect(length && length->first > 0.1) << "its length";
        rt.seek_motion(t, length ? length->first : 1);
        rt.frame({seconds(0.7), {}});
        const auto rest = draw(rt);
        rt.frame({seconds(3), {}});
        expect(rt.motion_time(t).has_value()) << "at rest, held: kept";
        expect(rt.frame_demand().idle()) << "a held endpoint lets the display rest";
        rt.set_motion_rate(t, -1);
        rt.play_motion(t);
        rt.frame({seconds(3.1), {}});
        expect(!rt.frame_demand().idle()) << "reverse playback requests animation frames again";
        const int back = left_of(draw(rt), 10, rose);
        expect(back >= 0 && back < left_of(rest, 10, rose)) << std::format("backwards: the thumb going back ({} < {})", back, left_of(rest, 10, rose));
        rt.release_motion(t);
        for (double u = 3.2; u < 6; u += 0.1) rt.frame({seconds(u), {}});
        expect(!rt.motion_time(t)) << "released: on to rest, and done";
        expect(same(rest, draw(rt))) << "at rest: the drawing as it is";
    };

    "a design edit presented (Live Design, D-149): what it adds writes itself in over the application's own motion; the person's level still holds"_test = [] {
        const auto whole = [](bool design, MotionLevel level) {
            Runtime rt(options());
            rt.set_motion_level(level);
            Signal<bool> more{false};
            Element page = column({}, {box({.id = "a", .width = 40, .height = 20, .background = "#1e5ac8"}).prop("motion", Source(Value("none"))),
                                         when([more] { return more.get(); },
                                              []() -> Element { return box({.id = "b", .width = 80, .height = 30, .background = "#c83c28"}).prop("motion", Source(Value("none"))); })});
            page.prop("motion", Source(Value("none")));
            rt.mount("main", std::move(page));
            (void)rt.frame({seconds(0), {}});
            if (design) rt.present_design();
            more.set(true);
            (void)rt.frame({seconds(0.05), {}});
            // The middle of the new box, a moment after it came: drawn whole (red) or still being written in.
            const Point centre = rt.screen_bounds(rt.find("main/b")).center();
            const auto picture = draw(rt);
            return picture.at(static_cast<int>(centre.x), static_cast<int>(centre.y)) == xrender::raster::Rgba8{200, 60, 40, 255};
        };
        expect(whole(false, MotionLevel::full)) << "the application said none: it appears at once";
        expect(!whole(true, MotionLevel::full)) << "presented as a design edit, it is still being written in";
        expect(whole(true, MotionLevel::off)) << "the person's motion off holds: at once";
    };
}};
