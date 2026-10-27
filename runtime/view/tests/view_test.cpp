// The view runtime: elements to nodes, structural ids, bindings, regions and
// keyed reconciliation, layout, paint, input and focus, actions under the
// rules of spec §3.3, transitions, scrolling, themes; and pixels pinned for
// every platform.
import std;
import openmui.view;
import openmui.raster;
import openmui.image;
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

InputEvent pointer(PointerKind kind, float x, float y, MouseButton b = MouseButton::left) {
    PointerEvent e;
    e.kind = kind;
    e.position = {x, y};
    e.button = b;
    return e;
}

InputEvent key(Key k, Modifiers mods = {}) {
    KeyEvent e;
    e.key = k;
    e.mods = mods;
    return e;
}

void click(Runtime& rt, float x, float y) {
    rt.frame({std::nullopt, {pointer(PointerKind::down, x, y), pointer(PointerKind::up, x, y)}});
}

raster::Surface draw(Runtime& rt, typeset::GlyphCache& glyphs) {
    raster::Surface s(static_cast<int>(rt.size().width * rt.scale()), static_cast<int>(rt.size().height * rt.scale()));
    render::CpuRenderer r;
    render::CpuOptions o;
    o.scale = rt.scale();
    o.background = Color::white();
    (void)r.render(rt.display_list(), s, {&rt.images(), &glyphs}, o);
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"view"};

    s.test("frame demand (D-70): idle asks for none; animations, input, timers and leases ask, and say why", [] {
        Runtime rt(options());
        rt.mount("main", column({}, {box({.id = "b", .width = 10, .height = 10, .opacity = 1.0f,
                                          .animate = {{"opacity", Transition::tween(seconds(1))}}})}));
        rt.frame();
        rt.frame();
        check(rt.frame_demand().idle(), std::format("idle: {}", rt.frame_demand().reason));
        // An animation: as fast as the display goes.
        rt.set(rt.find("main/b"), "opacity", Value(0.2));
        auto f = rt.frame();
        check(!f.demand.idle() && std::isinf(f.demand.max_hz) && f.demand.reason == "animation", f.demand.reason);
        rt.step_time(seconds(2));
        rt.frame();
        rt.frame();
        check(rt.frame_demand().idle(), "settled");
        // Input that moves something keeps it fast a moment after.
        f = rt.frame({.events = {WheelEvent{.position = {5, 5}, .delta = {0, 10}}}});
        check(f.demand.reason == "input" && std::isinf(f.demand.max_hz), f.demand.reason);
        // A lease: what it asks, while it lives.
        rt.step_time(seconds(0.31));
        rt.frame();
        check(rt.frame_demand().idle(), "300 ms after the input");
        {
            auto lease = rt.demand_frames({.min_hz = 24, .max_hz = 60, .preferred_hz = 50, .reason = "stream"});
            check(static_cast<bool>(lease));
            f = rt.frame();
            check(f.demand.reason == "stream" && f.demand.wanted() == 50 && f.demand.min_hz == 24, f.demand.reason);
        }
        rt.frame();
        check(rt.frame_demand().idle(), "released");
        // A timer, as often as it is due (here: twice a second).
        (void)rt.after(seconds(0.5), [] {});
        f = rt.frame();
        check(f.demand.reason == "timer" && std::abs(f.demand.wanted() - 2) < 1e-9, std::format("{} {}", f.demand.reason, f.demand.wanted()));
        // Merging and pacing are pure.
        const FrameDemand stream{.min_hz = 24, .max_hz = 60, .preferred_hz = 50, .reason = "stream"}, blink{0, 2, 2, "caret"};
        const FrameDemand both = merge(blink, stream);
        check(both.reason == "stream" && both.wanted() == 50 && both.min_hz == 24);
        check(merge(both, FrameDemand::fastest("camera")).reason == "camera");
        check(std::abs(frame_interval(stream, 120) - 1.0 / 50) < 1e-12, "its preferred rate");
        check(std::abs(frame_interval(FrameDemand::fastest("x"), 120) - 1.0 / 120) < 1e-12, "the display's fastest");
        check(std::abs(frame_interval(stream, 30) - 1.0 / 30) < 1e-12, "never faster than the display");
        check(frame_interval(FrameDemand{}, 120) == 0, "none");
        // A lease outliving its runtime releases nothing.
        FrameDemandLease kept;
        {
            Runtime other(options());
            kept = other.demand_frames(FrameDemand::fastest("gone"));
        }
        kept.release();
    });

    s.test("elements become nodes with structural and explicit ids (S-8)", [] {
        Runtime rt(options());
        rt.mount("main", column({}, {
                                        text({.text = "a"}),
                                        row({.id = "tools"}, {text({.text = "b"}), text({.text = "c"})}),
                                        text({.text = "d"}),
                                    }));
        rt.frame();
        check(rt.valid(rt.find("main")));
        check(rt.valid(rt.find("main/text:0")));
        check(rt.valid(rt.find("main/tools/text:1")));
        check(rt.valid(rt.find("main/text:1")), "a row with an explicit id does not shift the texts' indices");
        check_eq(rt.get(rt.find("main/text:1"), "text").as_string(), std::string_view("d"));
        check_eq(rt.path(rt.find("main/tools/text:0")), std::string("main/tools/text:0"));
        check_eq(rt.node_count(), std::size_t{7});
        auto sem = rt.semantic_node(rt.find("main/tools/text:1"));
        check_eq(sem.id, std::string("main/tools/text:1"));
        check(sem.name == std::string("c"), "a text is named by its content (S-6)");
        check(sem.role == Role::text);
        check(spec::check(sem).empty());
        auto main = rt.semantic_node(rt.find("main"));
        check_eq(main.children.size(), std::size_t{3});
        check_eq(main.children[1], std::string("main/tools"));
        check(!rt.valid(rt.find("main/nothing")));
    });

    s.test("layout: column, padding, gap, grow, and window bounds", [] {
        Runtime rt(options({200, 100}));
        rt.mount("main", column({.padding = 10, .gap = 5}, {
                                    box({.id = "a", .height = 20}),
                                    box({.id = "b", .grow = 1}),
                                    row({.id = "c", .height = 10}, {box({.id = "x", .width = 30}), spacer(), box({.id = "y", .width = 40})}),
                                }));
        rt.frame();
        auto b = [&](std::string_view p) { return rt.bounds(rt.find(p)); };
        check(b("main/a") == Rect{10, 10, 180, 20});
        check(b("main/b") == Rect{10, 35, 180, 40}, std::format("b: {} {} {} {}", b("main/b").x, b("main/b").y, b("main/b").width, b("main/b").height));
        check(b("main/c") == Rect{10, 80, 180, 10});
        // A row centres its children across (as SwiftUI's HStack and Flutter's Row do).
        check(b("main/c/x") == Rect{10, 85, 30, 0});
        check(b("main/c/y") == Rect{150, 85, 40, 0});
        rt.resize({300, 100}, 1);
        auto out = rt.frame();
        check(out.repainted);
        check(b("main/a").width == 280);
    });

    s.test("bound properties update their node alone", [] {
        Runtime rt(options());
        auto name = signal(std::string("Ada"));
        auto count = signal(1);
        rt.mount("main", column({}, {
                                        text({.id = "hello", .text = fmt("hello {}", name)}),
                                        text({.id = "n", .text = derive([count] { return count.get() * 2; })}),
                                        box({.id = "box", .width = Prop<Dim>(Source{std::nullopt, [count] { return Value(count.get() * 10); }, {}})}),
                                    }));
        rt.frame();
        NodeId hello = rt.find("main/hello");
        check_eq(rt.get(hello, "text").as_string(), std::string_view("hello Ada"));
        check_eq(rt.get(rt.find("main/n"), "text").as_string(), std::string_view("2"));
        check_eq(rt.bounds(rt.find("main/box")).width, 10.0f);
        name.set("Grace");
        count.set(3);
        auto out = rt.frame();
        check(rt.find("main/hello") == hello, "the node is the same node");
        check_eq(rt.get(hello, "text").as_string(), std::string_view("hello Grace"));
        check_eq(rt.bounds(rt.find("main/box")).width, 30.0f);
        check(out.stats.bindings >= 3);
        // Nothing changed: nothing is laid out or painted again.
        auto idle = rt.frame();
        check(!idle.repainted);
        check_eq(idle.stats.bindings, std::size_t{0});
    });

    s.test("regions: when switches its children, each reconciles by key", [] {
        Runtime rt(options());
        auto open = signal(false);
        auto items = signal(std::vector<std::string>{"a", "b", "c"});
        rt.mount("main", column({}, {
                                        when([open] { return open.get(); }, [] { return text({.id = "details", .text = "open"}); },
                                             [] { return text({.id = "summary", .text = "closed"}); }),
                                        column({.id = "list"}, {each(items, [](const std::string& s) { return s; },
                                                                    [](const std::string& s, std::size_t) {
                                                                        return text({.id = s, .text = s});
                                                                    })}),
                                    }));
        rt.frame();
        check(rt.valid(rt.find("main/summary")) && !rt.valid(rt.find("main/details")));
        NodeId b = rt.find("main/list/b");
        open.set(true);
        items.set({"c", "b", "x"});
        rt.frame();
        check(rt.valid(rt.find("main/details")) && !rt.valid(rt.find("main/summary")));
        check(rt.find("main/list/b") == b, "a keyed item keeps its node");
        check(!rt.valid(rt.find("main/list/a")));
        auto list = rt.children(rt.find("main/list"));
        check_eq(list.size(), std::size_t{3});
        check_eq(rt.path(list[0]), std::string("main/list/c"));
        check_eq(rt.path(list[2]), std::string("main/list/x"));
        check(rt.bounds(rt.find("main/list/c")).y < rt.bounds(b).y, "order follows the list");
    });

    s.test("pointer press activates; handlers, hover and pressed states", [] {
        Runtime rt(options());
        int pressed = 0, entered = 0;
        rt.mount("main", column({}, {
                                        box({.id = "button", .width = 50, .height = 20,
                                             .on = {{"press", [&](const Value&) { ++pressed; }},
                                                    {"pointer_enter", [&](const Value&) { ++entered; }}}}),
                                    }));
        rt.frame();
        rt.frame({std::nullopt, {pointer(PointerKind::move, 10, 10)}});
        check_eq(entered, 1);
        check(rt.state(rt.find("main/button")).hovered);
        rt.frame({std::nullopt, {pointer(PointerKind::down, 10, 10)}});
        check(rt.state(rt.find("main/button")).pressed);
        rt.frame({std::nullopt, {pointer(PointerKind::up, 10, 10)}});
        check_eq(pressed, 1);
        check(!rt.state(rt.find("main/button")).pressed);
        // Released outside: no activation.
        rt.frame({std::nullopt, {pointer(PointerKind::down, 10, 10), pointer(PointerKind::up, 150, 80)}});
        check_eq(pressed, 1);
        check(!rt.state(rt.find("main/button")).hovered);
        // Its semantics: activatable and focusable, by pointer, key or agent alike.
        auto sem = rt.semantic_node(rt.find("main/button"));
        check(sem.action("activate") != nullptr && sem.states.has(State::focusable));
    });

    s.test("focus: Tab order, keyboard activation, focus-visible ring", [] {
        Runtime rt(options());
        std::vector<std::string> log;
        auto button = [&](std::string id) {
            return box({.id = id, .width = 40, .height = 20, .on = {{"press", [&log, id](const Value&) { log.push_back(id); }}}});
        };
        rt.mount("main", row({}, {button("one"), box({.id = "plain", .width = 10}), button("two"),
                                  box({.id = "off", .width = 40, .disabled = true, .on = {{"press", [](const Value&) {}}}})}));
        rt.frame();
        rt.frame({std::nullopt, {key(Key::tab)}});
        check_eq(rt.path(rt.focused()), std::string("main/one"));
        check(rt.state(rt.focused()).focus_visible);
        rt.frame({std::nullopt, {key(Key::tab)}});
        check(rt.path(rt.focused()) == std::string("main/two"), "plain boxes and disabled nodes are skipped");
        rt.frame({std::nullopt, {key(Key::enter)}});
        check(log == std::vector<std::string>{"two"});
        rt.frame({std::nullopt, {key(Key::tab)}});
        check(rt.path(rt.focused()) == std::string("main/one"), "Tab wraps");
        rt.frame({std::nullopt, {key(Key::tab, Modifier::shift)}});
        check_eq(rt.path(rt.focused()), std::string("main/two"));
        // A pointer press focuses without the ring.
        click(rt, 5, 50);
        check_eq(rt.path(rt.focused()), std::string("main/one"));
        check(!rt.state(rt.focused()).focus_visible);
        // Clicking empty space blurs.
        click(rt, 190, 90);
        check(!rt.valid(rt.focused()));
    });

    s.test("actions: effects, answers, disabled and the journal (S-1, S-2)", [] {
        Runtime rt(options());
        int deleted = 0;
        check(rt.verbs()
                  .add({"doc.delete", "Delete the document", Effect::irreversible, "delete", {}},
                       [&](const Value&) -> VerbResult {
                           ++deleted;
                           return Value("gone");
                       })
                  .has_value());
        rt.mount("main", column({}, {
                                        box({.id = "delete", .width = 10, .height = 10}).prop("verb", Source{Value("doc.delete"), {}, {}}),
                                        box({.id = "off", .disabled = true, .on = {{"press", [](const Value&) {}}}}),
                                    }));
        rt.frame();
        NodeId del = rt.find("main/delete");
        auto acts = rt.actions(del);
        auto it = std::ranges::find(acts, std::string("activate"), &spec::ActionDesc::name);
        check(it != acts.end() && it->effect == Effect::irreversible && it->verb == "doc.delete" && it->answer == "delete",
              "activate takes the verb's effect and answer");
        Invocation agent;
        agent.actor = {"agent", "helper", {"person:alice"}};
        auto e = rt.act(del, "activate", agent);
        check_eq(e.exit_code().value_or(-1), spec::exit_code::needs_answer);
        check_eq(deleted, 0);
        agent.answer = "delete";
        e = rt.act(del, "activate", agent);
        check_eq(e.exit_code().value_or(-1), spec::exit_code::success);
        check_eq(deleted, 1);
        check(e.data_value() && e.data_value()->as_string() == "gone");
        // Pointer activation of an irreversible action asks too: no one deletes by accident.
        click(rt, 5, 5);
        check_eq(deleted, 1);
        auto off = rt.act(rt.find("main/off"), "activate");
        check_eq(off.exit_code().value_or(-1), spec::exit_code::failure);
        auto none = rt.act(del, "toggle");
        check_eq(none.exit_code().value_or(-1), spec::exit_code::failure);
        auto j = rt.action_journal();
        check(j.size() >= 4);
        check_eq(j[1].actor, std::string("agent:helper;via=person:alice"));
        check_eq(j[1].exit_code, 0);
    });

    s.test("colour transitions travel through OKLab and land on the exact colour (D-60)", [] {
        Runtime rt(options());
        auto warm = signal(false);
        rt.mount("main", column({}, {
                                        box({.id = "swatch", .width = 40, .height = 40,
                                             .background = Prop<Fill>(Source{std::nullopt, [warm] { return Value(warm.get() ? "#ffff00" : "#0000ff"); }, {}}),
                                             .animate = {{"background", Transition::tween(seconds(1), anim::ease::linear)}}}),
                                    }));
        auto fill = [&]() -> std::optional<Color> {
            for (const auto& c : rt.display_list().commands())
                if (const auto* r = std::get_if<render::FillRect>(&c)) return r->paint.color;
            return std::nullopt;
        };
        rt.frame({seconds(0), {}});
        check(fill() == std::optional<Color>(Color::hex(0x0000ff)), "at rest: exactly the colour set");
        warm.set(true);
        rt.frame({seconds(0), {}});
        rt.frame({seconds(0.5), {}});
        // Halfway in OKLab, not in sRGB (which would be the grey 0.5, 0.5, 0.5).
        const auto lb = anim::lanes<anim::Rgba>::to({0, 0, 1, 1});
        const auto ly = anim::lanes<anim::Rgba>::to({1, 1, 0, 1});
        const anim::Rgba mid = anim::lanes<anim::Rgba>::from({(lb[0] + ly[0]) / 2, (lb[1] + ly[1]) / 2, (lb[2] + ly[2]) / 2, 1});
        const auto half = fill();
        check(half && std::abs(half->r - mid.r) < 2e-3f && std::abs(half->g - mid.g) < 2e-3f && std::abs(half->b - mid.b) < 2e-3f,
              half ? std::format("{} {} {} (OKLab mid {} {} {})", half->r, half->g, half->b, mid.r, mid.g, mid.b) : "no fill");
        check(half && !(std::abs(half->r - 0.5f) < 0.02f && std::abs(half->g - 0.5f) < 0.02f && std::abs(half->b - 0.5f) < 0.02f),
              "not the sRGB midpoint");
        rt.frame({seconds(1.0), {}});
        check(fill() == std::optional<Color>(Color::hex(0xffff00)), "settled: exactly the colour set");
    });

    s.test("every visual property transitions: layout, text size and colour (D-60)", [] {
        Runtime rt(options());
        auto wide = signal(false);
        auto big = signal(false);
        auto linear = Transition::tween(seconds(1), anim::ease::linear);
        rt.mount("main", column({.align = "start"}, {
                                        box({.id = "bar", .width = Prop<Dim>(Source{std::nullopt, [wide] { return Value(wide.get() ? 200.0 : 100.0); }, {}}),
                                             .height = 20, .background = "accent", .animate = {{"width", linear}}}),
                                        text({.id = "title", .text = "Title",
                                              .color = Prop<Fill>(Source{std::nullopt, [big] { return Value(big.get() ? "#ff0000" : "#0000ff"); }, {}}),
                                              .size = Prop<float>(Source{std::nullopt, [big] { return Value(big.get() ? 32.0 : 16.0); }, {}}),
                                              .animate = {{"size", linear}, {"color", linear}}}),
                                    }));
        rt.frame({seconds(0), {}});
        const NodeId bar = rt.find("main/bar"), title = rt.find("main/title");
        check(rt.bounds(bar).width == 100);
        const float small_h = rt.bounds(title).height;
        wide.set(true);
        big.set(true);
        rt.frame({seconds(0), {}});
        check(rt.semantic_node(bar).states.has(State::animating), "a layout property in transition is animating");
        rt.frame({seconds(0.5), {}});
        check(std::abs(rt.bounds(bar).width - 150) < 0.5f, std::format("halfway: {}", rt.bounds(bar).width));
        const float mid_h = rt.bounds(title).height;
        check(mid_h > small_h && rt.get(title, "size").as_double() > 16 && rt.get(title, "size").as_double() < 32,
              std::format("the text grows: {} → {}", small_h, mid_h));
        const std::string mid_color(rt.get(title, "color").as_string());
        check(mid_color != "#ff0000" && mid_color != "#0000ff", "the colour is in between: " + mid_color);
        // Retarget halfway: it continues from where it is.
        wide.set(false);
        rt.frame({seconds(0.5), {}});
        rt.frame({seconds(1.0), {}});
        const float back = rt.bounds(bar).width;
        check(back > 100 && back < 150, std::format("on its way back from 150: {}", back));
        rt.frame({seconds(2.0), {}});
        check(rt.bounds(bar).width == 100, "lands exactly");
        check(rt.get(title, "size") == Value(32.0) && rt.get(title, "color") == Value("#ff0000"), "exactly what was set");
        check(rt.bounds(title).height > mid_h);
        check(!rt.semantic_node(bar).states.has(State::animating) && !rt.semantic_node(title).states.has(State::animating));
        // Reduced motion: values jump.
        rt.set_reduce_motion(true);
        wide.set(true);
        rt.frame({seconds(2.0), {}});
        rt.frame({seconds(2.0), {}});
        check(rt.bounds(bar).width == 200, std::format("reduced motion jumps: {}", rt.bounds(bar).width));
    });

    s.test("the frame is a pipeline of stage plugins: levels, custom stages, live replacement (D-66)", [] {
        // The levels are pipelines.
        auto names = [](const pipeline::Description& d) {
            std::vector<std::string> out;
            for (const auto& st : d.stages) out.push_back(st.name);
            return out;
        };
        check(names(runtime_pipeline(Stages::semantics)) == std::vector<std::string>{"timers", "input", "regions", "animations"});
        check(names(runtime_pipeline(Stages::paint)).size() == 8);

        // A custom stage between paint and the debug overlay: it updates the display list.
        struct Badge final : pipeline::Stage {
            std::vector<pipeline::Port> reads() const override { return {pipeline::port<render::DisplayList>("display")}; }
            std::vector<pipeline::Port> writes() const override { return {pipeline::port<render::DisplayList>("display")}; }
            std::expected<void, std::string> run(pipeline::Context& c) override {
                c.resources.get<render::DisplayList>("display")->fill_rect({0, 0, 4, 4}, Color::hex(0xff00ff));
                return {};
            }
        };
        RuntimeOptions o = options();
        pipeline::Description d = runtime_pipeline(Stages::paint);
        d.name = "app";
        d.stages.insert(d.stages.begin() + 7, {"badge", "app.badge", {"paint"}, {}});
        o.pipeline = d;
        o.plugins = [](pipeline::Registry& r) {
            r.add({.name = "app.badge", .kind = "stage", .description = "a corner badge"},
                  [](const pipeline::Config&) -> std::expected<std::unique_ptr<pipeline::Stage>, std::string> { return std::make_unique<Badge>(); });
        };
        Runtime rt(o);
        rt.mount("main", column({.padding = 8}, {text({.id = "t", .text = "hi"})}));
        auto out = rt.frame({seconds(0), {}});
        auto has_badge = [&] {
            for (const auto& c : rt.display_list().commands())
                if (const auto* r = std::get_if<render::FillRect>(&c); r && r->paint.color == Color::hex(0xff00ff)) return true;
            return false;
        };
        check(out.repainted && has_badge(), "the custom stage drew into the frame's list");
        const auto order = rt.frame_pipeline().order();
        check(std::ranges::find(order, "badge") - std::ranges::find(order, "paint") == 1, "it runs right after paint");
        check(rt.frame_pipeline().describe().contains("badge (app.badge)"));
        check(out.stats.timings.other >= 0 && out.stats.timings.total > 0);

        // Removed while running: the next frame has no badge.
        check(rt.frame_pipeline().remove("badge", rt.stage_plugins()).has_value());
        rt.set_debug({});  // something to repaint
        rt.frame({seconds(0.1), {}});
        check(!has_badge(), "removed between frames");
        // A replacement that does not build leaves the pipeline as it was.
        check(!rt.frame_pipeline().replace("layout", "app.nonesuch", {}, rt.stage_plugins()).has_value());
        check(rt.frame_pipeline().order().size() == 8);
    });

    s.test("a custom pipeline that does not build falls back to its level, and says why", [] {
        std::vector<std::string> said;
        set_log_sink([](void* user, const LogRecord& r) { static_cast<std::vector<std::string>*>(user)->emplace_back(r.message); }, &said);
        RuntimeOptions o = options();
        o.pipeline = pipeline::Description{"broken", {{"paint", "openmui.view.paint", {}, {}}}};  // paint without bounds
        Runtime rt(o);
        set_log_sink(nullptr, nullptr);
        check(rt.frame_pipeline().description().name == "runtime.h2");
        check(std::ranges::any_of(said, [](const std::string& m) { return m.contains("`broken` does not build") && m.contains("reads `bounds`"); }),
              said.empty() ? "nothing logged" : said.front());
        rt.mount("main", text({.id = "t", .text = "still works"}));
        check(rt.frame({seconds(0), {}}).repainted);
    });

    s.test("time control: pause, step, rate, and seeking back into finished animations (F4)", [] {
        Runtime rt(options());
        auto on = signal(false);
        auto linear = Transition::tween(seconds(1), anim::ease::linear);
        int fired = 0;
        rt.mount("main", column({.align = "start"}, {
                                        box({.id = "fade", .width = Prop<Dim>(Source{std::nullopt, [on] { return Value(on.get() ? 200.0 : 100.0); }, {}}),
                                             .height = 10, .background = "accent",
                                             .opacity = Prop<float>(Source{std::nullopt, [on] { return Value(on.get() ? 0.0 : 1.0); }, {}}),
                                             .animate = {{"opacity", linear}, {"width", linear}}}),
                                    }));
        rt.frame({seconds(0), {}});
        const NodeId fade = rt.find("main/fade");
        auto opacity = [&] {
            for (const auto& c : rt.display_list().commands())
                if (const auto* l = std::get_if<render::PushLayer>(&c)) return l->opacity;
            return 1.0f;
        };
        on.set(true);
        rt.frame({seconds(0), {}});
        rt.frame({seconds(0.25), {}});
        check(std::abs(opacity() - 0.75f) < 1e-3f && std::abs(rt.bounds(fade).width - 125) < 0.5f);

        // Paused: the host's time moves, the application's does not — nor do timers.
        (void)rt.after(seconds(0.1), [&] { ++fired; });
        rt.pause_time(true);
        rt.frame({seconds(0.75), {}});
        check(std::abs(opacity() - 0.75f) < 1e-3f, "paused");
        check(fired == 0, "timers wait too");
        check(rt.time_state().paused && rt.time_state().host == seconds(0.75) && std::abs(rt.time_state().shown.count() - 0.25) < 1e-9);
        // A step while paused.
        rt.step_time(seconds(0.25));
        rt.frame({seconds(0.75), {}});
        check(std::abs(opacity() - 0.5f) < 1e-3f && fired == 1, std::format("stepped: {}", opacity()));
        // Half speed.
        rt.pause_time(false);
        rt.set_time_rate(0.5);
        rt.frame({seconds(1.15), {}});  // 0.4 s of host time: 0.2 s shown
        check(std::abs(opacity() - 0.3f) < 1e-3f, std::format("half speed: {}", opacity()));
        // Run to the end, then seek back into the finished animation.
        rt.set_time_rate(1);
        rt.frame({seconds(3), {}});
        check(opacity() == 1.0f && rt.bounds(fade).width == 200 && !rt.semantic_node(fade).states.has(State::animating), "finished");
        rt.seek_time(seconds(0.5));
        rt.frame({seconds(3), {}});
        check(std::abs(opacity() - 0.5f) < 1e-3f, std::format("sought back: opacity {}", opacity()));
        check(std::abs(rt.bounds(fade).width - 150) < 0.5f, std::format("sought back: width {}", rt.bounds(fade).width));
        check(rt.semantic_node(fade).states.has(State::animating), "moving again");
        // And forward again to the end: exactly what was set.
        rt.seek_time(seconds(2));
        rt.frame({seconds(3), {}});
        check(opacity() == 1.0f && rt.bounds(fade).width == 200, "sought forward");
        // Released: the host's time again.
        rt.release_time();
        rt.frame({seconds(4), {}});
        check(!rt.time_state().controlled && rt.now() == seconds(4));
    });

    s.test("transitions: tweens follow the clock, interruptions continue from where they are", [] {
        Runtime rt(options());
        auto faded = signal(false);
        rt.mount("main", column({}, {
                                        box({.id = "panel", .width = 50, .height = 50, .background = "#ff0000",
                                             .opacity = Prop<float>(Source{std::nullopt, [faded] { return Value(faded.get() ? 0.0 : 1.0); }, {}}),
                                             .animate = {{"opacity", Transition::tween(seconds(1), anim::ease::linear)}}}),
                                    }));
        rt.frame({seconds(0), {}});
        auto sem = [&] { return rt.semantic_node(rt.find("main/panel")); };
        check(!sem().states.has(State::animating));
        faded.set(true);
        auto out = rt.frame({seconds(0), {}});
        check(out.animating);
        rt.frame({seconds(0.5), {}});
        check(sem().states.has(State::animating));
        // Paint shows the halfway opacity: a layer at 0.5.
        bool half = false;
        for (const auto& c : rt.display_list().commands())
            if (const auto* l = std::get_if<render::PushLayer>(&c)) half = half || std::abs(l->opacity - 0.5f) < 1e-4f;
        check(half, "the layer is at half opacity");
        auto settled = rt.frame({seconds(1.0), {}});
        check(!sem().states.has(State::animating));
        check(!settled.animating);
        // Reduced motion: values jump.
        rt.set_reduce_motion(true);
        faded.set(false);
        rt.frame({seconds(1.0), {}});
        check(!sem().states.has(State::animating));
    });

    s.test("removal plays the exit transition; the node refuses actions meanwhile (S-4)", [] {
        Runtime rt(options());
        auto show = signal(true);
        rt.mount("main", column({}, {
                                        when([show] { return show.get(); },
                                             [] {
                                                 return box({.id = "toast", .height = 20,
                                                             .on = {{"press", [](const Value&) {}}},
                                                             .animate = {{"exit", Transition::tween(seconds(0.2))}}});
                                             }),
                                        box({.id = "after", .height = 10}),
                                    }));
        rt.frame({seconds(0), {}});
        NodeId toast = rt.find("main/toast");
        check(rt.bounds(rt.find("main/after")).y == 20);
        show.set(false);
        rt.frame({seconds(0.05), {}});
        check(rt.valid(toast), "still there while it fades");
        check(rt.state(toast).removing);
        check(rt.semantic_node(toast).states.has(State::removing));
        check_eq(rt.act(toast, "activate").exit_code().value_or(-1), spec::exit_code::failure);
        check(rt.bounds(rt.find("main/after")).y == 0, "it left the flow at once");
        rt.frame({seconds(0.3), {}});
        rt.frame({seconds(0.3), {}});
        check(!rt.valid(toast));
    });

    s.test("an action's declared arguments are required and typed; others are refused (spec §3.3)", [] {
        Runtime rt(options({200, 100}));
        auto y = signal(0.0);
        std::vector<Element> rows;
        for (int i = 0; i < 10; ++i) rows.push_back(box({.id = std::format("r{}", i), .height = 30}));
        rt.mount("main", column({}, {scroll({.id = "list", .y = y, .height = 100}, std::move(rows))}));
        rt.frame({seconds(0), {}});
        const NodeId list = rt.find("main/list");
        auto refused = [&](Value args, std::string_view says) {
            const Envelope e = rt.act(list, "scroll_to", {std::move(args)});
            check_eq(e.exit_code().value_or(-1), spec::exit_code::failure);
            std::string text;
            for (const spec::Record& r : e.records())
                if (r.kind == spec::RecordKind::error) text = r.json();
            check(text.find("invalid-argument") != std::string::npos && text.find(says) != std::string::npos, text);
            check(text.find("it takes x: f64 | end | page | -page") != std::string::npos, text);
        };
        refused(Value{}.set("y", 10), "`x` (f64 | end | page | -page) is missing");
        refused(Value{}.set("x", 0).set("y", "sideways"), "`y` is a f64 | end | page | -page");
        refused(Value{}.set("x", 0).set("y", 10).set("z", 1), "`z` is not an argument of it");
        refused(Value("down"), "a map of names to values");
        rt.frame({seconds(0.5), {}});
        check(y.peek() == 0.0, "a refused call changes nothing");
        // A number, a symbol: both are what it takes.
        check_eq(rt.act(list, "scroll_to", {Value{}.set("x", 0).set("y", "end")}).exit_code().value_or(-1), 0);
        rt.frame({seconds(1), {}});
        check(y.peek() == 200.0, "end");
        check_eq(rt.act(list, "scroll_by", {Value{}.set("dx", 0).set("dy", -50.0)}).exit_code().value_or(-1), 0);
        // An action that declares none passes what it is given on.
        check_eq(rt.act(rt.find("main/list/r3"), "scroll_into_view", {Value{}.set("anything", 1)}).exit_code().value_or(-1), 0);
    });

    s.test("scroll views: wheel, actions with symbols, two-way offsets, scroll into view", [] {
        Runtime rt(options({200, 100}));
        auto y = signal(0.0);
        std::vector<Element> rows;
        for (int i = 0; i < 10; ++i)
            rows.push_back(box({.id = std::format("r{}", i), .height = 30, .on = {{"press", [](const Value&) {}}}}));
        rt.mount("main", column({}, {scroll({.id = "list", .y = y, .height = 100}, std::move(rows))}));
        rt.frame({seconds(0), {}});
        NodeId list = rt.find("main/list");
        WheelEvent w;
        w.position = {50, 50};
        w.delta = {0, 45};
        w.precise = true;
        rt.frame({seconds(0), {w}});
        check(y.peek() == 45.0, "the offset is written back to its signal");
        check_eq(rt.bounds(rt.find("main/list/r1")).y, -15.0f);
        check(rt.hit_test({50, 20}) == rt.find("main/list/r2"), "hit testing follows the offset");
        auto e = rt.act(list, "scroll_to", {Value{}.set("x", 0).set("y", "end")});
        check_eq(e.exit_code().value_or(-1), 0);
        rt.frame({seconds(1), {}});
        check(y.peek() == 200.0, "end is the content height minus the viewport");
        check_eq(rt.bounds(rt.find("main/list/r9")).bottom(), 100.0f);
        // Out of range is clamped.
        WheelEvent more = w;
        more.delta = {0, 500};
        rt.frame({seconds(1), {more}});
        check_eq(y.peek(), 200.0);
        // From outside: the bound signal moves the view.
        y.set(30.0);
        rt.frame({seconds(1), {}});
        check_eq(rt.bounds(rt.find("main/list/r1")).y, 0.0f);
        // Keyboard focus scrolls the focused row into view.
        rt.act(rt.find("main/list/r8"), "focus");
        rt.frame({seconds(3), {}});
        auto r8 = rt.bounds(rt.find("main/list/r8"));
        check(r8.y >= 0 && r8.bottom() <= 100, std::format("r8 at {}..{}", r8.y, r8.bottom()));
        auto sem = rt.semantic_node(list);
        check(sem.role == Role::scroll_view && sem.value.get("max_y") && sem.value.get("max_y")->as_double() == 200.0);
    });

    s.test("themes: tokens resolve again, semantics stay", [] {
        Runtime rt(options());
        rt.mount("main", column({}, {box({.id = "card", .width = 20, .height = 20, .background = "surface"})}));
        rt.frame();
        auto color_of = [&] {
            for (const auto& c : rt.display_list().commands())
                if (const auto* f = std::get_if<render::FillRect>(&c)) return f->paint.color;
            return Color::transparent();
        };
        check(color_of() == Theme::light().color("surface"));
        auto before = rt.semantic_node(rt.find("main/card"));
        rt.set_theme(Theme::dark_theme());
        rt.frame();
        check(color_of() == Theme::dark_theme().color("surface"));
        check(rt.semantic_node(rt.find("main/card")) == before);
    });

    s.test("grid tracks parse like CSS", [] {
        auto t = parse_tracks("120 1fr minmax(80, 2fr) repeat(2, auto 10%)");
        check_eq(t.size(), std::size_t{7});
        check(t[0].kind == layout::Track::Kind::points && t[0].value == 120);
        check(t[1].kind == layout::Track::Kind::fr);
        check(t[2].kind == layout::Track::Kind::minmax && t[2].min_value == 80 && t[2].max_kind == layout::Track::Kind::fr);
        check(t[3].kind == layout::Track::Kind::auto_ && t[4].kind == layout::Track::Kind::percent);
        Runtime rt(options({300, 100}));
        rt.mount("main", column({}, {grid({.id = "g", .gap = 10}, "1fr 2fr",
                                          {box({.id = "a", .height = 10}), box({.id = "b"}), box({.id = "c", .height = 5})})}));
        rt.frame();
        check(rt.bounds(rt.find("main/g/a")) == Rect{0, 0, 290.0f / 3, 10});
        check_near(rt.bounds(rt.find("main/g/b")).x, 290.0 / 3 + 10, 1e-4);
        check(rt.bounds(rt.find("main/g/c")).y == 20);
    });

    s.test("painters draw in their own coordinates; transforms move hit testing", [] {
        Runtime rt(options());
        rt.mount("main", column({.padding = 10}, {
                                    painter({.id = "dot", .width = 20, .height = 20,
                                             .paint = [](PaintContext& c) { c.list.fill_rect({0, 0, c.size.width, c.size.height}, Color::hex(0x00ff00)); }}),
                                    box({.id = "moved", .width = 20, .height = 20, .translate_x = 100}),
                                }));
        rt.frame();
        check(rt.hit_test({15, 15}) == rt.find("main/dot"));
        check(rt.hit_test({115, 35}) == rt.find("main/moved"));
        check(rt.hit_test({15, 35}) == rt.find("main"));
        typeset::GlyphCache glyphs(fonts());
        auto px = draw(rt, glyphs);
        auto p = px.at(15, 15);
        check(p.g == 255 && p.r == 0, "the painter's green");
        render::CpuRenderer r;
        raster::Surface s2(200, 100);
        render::CpuOptions o;
        o.object_ids = true;
        (void)r.render(rt.display_list(), s2, {&rt.images(), &glyphs}, o);
        check(rt.node_at_index(r.object_at(15, 15, 200)) == rt.find("main/dot"), "a pixel traces back to its node");
        check_eq(rt.origin(rt.find("main/dot")).line() > 0, true);
    });

    s.test("immediate mode: ids from call sites, actions return true once, no build without cause", [] {
        Runtime rt(options({300, 200}));
        int builds = 0, clicks = 0;
        bool dark = false;
        std::string label = "Add";
        rt.mount("main", immediate([&](Ui& ui) {
                     ++builds;
                     ui.column({.gap = 4}, [&] {
                         ui.text(std::format("clicked {} times", clicks));
                         if (ui.button(label, {.id = "add"})) ++clicks;
                         ui.checkbox("Dark", dark, {.id = "dark"});
                         for (int i = 0; i < 3; ++i) {
                             auto scope = ui.scope(i);
                             ui.text("row");
                         }
                     });
                 }));
        rt.frame({seconds(0), {}});
        check(builds == 1);
        NodeId add = rt.find("main/add");
        check(!rt.valid(add), "ids are paths: the button sits in the column");
        NodeId column = rt.children(rt.find("main"))[0];
        std::string col = rt.path(column);
        check(col.starts_with("main/column:i"), col);
        add = rt.find(col + "/add");
        check(rt.valid(add));
        auto kids = rt.children(column);
        check(kids.size() == 6);
        check(rt.path(kids[3]) != rt.path(kids[4]), "the id stack tells loop iterations apart");
        // Idle frames do not build.
        rt.frame({seconds(0.1), {}});
        rt.frame({seconds(0.2), {}});
        check(builds == 1);
        // An action builds again, once; the button reports it once.
        check(rt.act(add, "activate").exit_code() == std::optional<int>(0));
        check(clicks == 1);
        rt.frame({seconds(0.3), {}});
        check(rt.get(kids[0], "text").as_string() == "clicked 1 times");
        const int after = builds;
        rt.frame({seconds(0.4), {}});
        check(builds == after && clicks == 1);
        // Labels are not ids (S-9): renaming keeps the node.
        label = "Plus";
        check(rt.act(add, "activate").exit_code() == std::optional<int>(0));
        rt.frame({seconds(0.5), {}});
        check(rt.find(col + "/add") == add && clicks == 2);
        // The stand-in checkbox (no base components linked) toggles its bool.
        check(rt.act(rt.find(col + "/dark"), "activate").exit_code() == std::optional<int>(0));
        check(dark);
        // Ids of automatic nodes are the same in the next build.
        check(rt.path(rt.children(column)[3]) == rt.path(kids[3]));
    });

    s.test("immediate mode: timers and events drive builds; retained views nest", [] {
        Runtime rt(options({300, 200}));
        int builds = 0, ticks = 0, downs = 0;
        auto shared = signal(std::string("retained"));
        rt.mount("main", immediate([&](Ui& ui) {
                     ++builds;
                     if (ticks < 3) {
                         ++ticks;
                         ui.rebuild_after(seconds(1));
                     }
                     ui.add(painter({.id = "pad", .width = 50, .height = 50}));
                     if (ui.event("pad", "pointer_down")) ++downs;
                     ui.add(text({.id = "nested", .text = shared}));
                 }));
        rt.frame({seconds(0), {}});
        check(builds == 1);
        rt.frame({seconds(0.5), {}});
        check(builds == 1);
        auto out = rt.frame({seconds(1.0), {}});
        check(builds == 2);
        check(out.next_timer.has_value() && *out.next_timer == seconds(2.0));
        rt.frame({seconds(2.0), {}});
        rt.frame({seconds(3.0), {}});
        rt.frame({seconds(4.0), {}});
        check(builds == 4 && ticks == 3);
        // The first build asked about pointer_down: from then on it arrives.
        rt.frame({seconds(4.0), {pointer(PointerKind::down, 10, 10), pointer(PointerKind::up, 10, 10)}});
        check(downs == 1);
        // A retained text inside updates by its binding, no build needed.
        const int before = builds;
        shared.set("still retained");
        rt.frame({seconds(4.1), {}});
        check(builds == before);
        check(rt.get(rt.find("main/nested"), "text").as_string() == "still retained");
    });

    s.test("development aids: timings, history, inspection, debug overlays, slow frames", [] {
        Runtime rt(options({240, 160}));
        rt.mount("main", column({.padding = 8, .gap = 4}, {
                                    text({.id = "title", .text = "Hello"}),
                                    box({.id = "card", .width = 60, .height = 30, .background = "accent", .radius = 4}),
                                }));
        auto out = rt.frame();
        const FrameTimings& t = out.stats.timings;
        check(t.total > 0 && t.layout >= 0 && t.paint > 0 && t.total >= t.layout + t.paint - 1e-9);
        rt.frame();
        check(rt.frame_history().size() == 2);
        auto info = rt.inspect(rt.find("main/card"));
        check(info.get("type")->as_string() == "box" && info.get("id")->as_string() == "main/card");
        check(info.get("source")->as_string().find("view_test.cpp") != std::string_view::npos, "where it was built");
        check(info.get("props")->get("background")->as_string() == "accent");
        check(info.get("layout")->get("width")->as_double() == 60 && info.get("bounds")->list()->size() == 4);
        // Overlays add to the list; with them off it is what it was.
        const std::size_t plain = rt.display_list().size();
        rt.set_debug({.bounds = true, .hud = true});
        rt.frame();
        check(rt.display_list().size() > plain + 3, std::format("{} vs {}", rt.display_list().size(), plain));
        rt.set_debug({.repaints = true, .inspector = true});
        rt.set(rt.find("main/card"), "background", Value("#ff0000"));
        rt.frame({std::nullopt, {[] { PointerEvent p; p.position = {20, 40}; return InputEvent(p); }()}});
        const std::size_t with_flash = rt.display_list().size();
        check(with_flash > plain);
        rt.set_debug({});
        rt.frame();
        check(rt.display_list().size() == plain, "off: nothing of it remains");
        // A slow frame logs its phases in the "frame" category.
        set_log_history(64);
        set_log_sink([](void*, const LogRecord&) {}, nullptr);
        rt.set_debug({.slow_frame_ms = 1e-9});
        rt.set(rt.find("main/title"), "text", Value("Hello again"));
        rt.frame();
        auto logs = recent_logs(10, Level::warning);
        check(!logs.empty() && logs.back().category == "frame" && logs.back().message.find("layout") != std::string::npos,
              logs.empty() ? "no record" : logs.back().message);
        set_log_sink(nullptr, nullptr);
    });

    s.test("pixels are the same on every platform", [] {
        // Its own fonts: font references are numbered in the order they are
        // first used, and the list hash includes them.
        typeset::FontCollection own;
        check(own.add_defaults().has_value());
        RuntimeOptions o = options({240, 120});
        o.fonts = &own;
        o.scale = 2;
        Runtime rt(o);
        rt.mount("main", column({.padding = 12, .gap = 8, .background = "background"}, {
                                    text({.text = "openmui — 你好 مرحبا", .style = "heading"}),
                                    row({.gap = 6}, {
                                        box({.width = 40, .height = 24, .background = "accent", .radius = 6, .shadow = "md"}),
                                        box({.width = 40, .height = 24, .border_color = "border_strong", .border_width = 1.5f, .radius = 12}),
                                        box({.width = 24, .height = 24, .background = "#16a34a", .opacity = 0.5f, .rotation = 45}),
                                    }),
                                    text({.text = "Body text wraps when it is longer than the line.", .max_lines = 1}),
                                }));
        rt.frame();
        typeset::GlyphCache glyphs(own);
        auto px = draw(rt, glyphs);
        Hash h;
        for (int y = 0; y < px.height(); ++y)
            h.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
        const std::uint64_t list_hash = 0xe0666bd669386d14ULL;
        std::println("view pixels {:016x} list {:016x}", h.value(), rt.display_list().hash());
        imageio::View v{reinterpret_cast<const std::uint8_t*>(px.pixels().data()), px.width(), px.height(), px.width() * 4};
        check(imageio::save_png(scratch_dir("view") / "scene@2x.png", v).has_value());
        check(rt.display_list().balanced());
        // Pinned: GCC and LLVM, Linux, macOS and Windows render this scene to the same bytes.
        check(h.value() == 0xc843dc8d4c1ccdbcULL, std::format("pixels {:016x}", h.value()));
        check(rt.display_list().hash() == list_hash, std::format("list {:016x}", rt.display_list().hash()));
    });

    s.test("seeking back past an interruption shows what was played (D-60: the segments are the history)", [] {
        Runtime rt(options({400, 100}));
        auto wide = signal(0);
        rt.mount("main", column({}, {box({.id = "bar",
                                           .width = Prop<Dim>(Source{std::nullopt, [wide] { return Value(wide.get() == 0 ? 50.0 : wide.get() == 1 ? 350.0 : 120.0); }, {}}),
                                           .height = 10, .background = "#4f46e5",
                                           .animate = {{"width", Transition::spring_with(anim::springs::gentle)}}})}));
        rt.frame({seconds(0), {}});
        wide.set(1);                     // to 350 …
        std::vector<float> played;
        for (int f = 0; f <= 150; ++f) {
            if (f == 24) wide.set(2);    // … interrupted at 0.4 s, to 120
            rt.frame({seconds(f / 60.0), {}});
            played.push_back(rt.bounds(rt.find("main/bar")).width);
        }
        int differing = 0;
        for (int f : {3, 12, 23, 24, 30, 61, 90, 7, 140}) {
            rt.seek_time(seconds(f / 60.0));
            rt.frame({seconds(10 + f), {}});
            if (rt.bounds(rt.find("main/bar")).width != played[static_cast<std::size_t>(f)]) ++differing;
        }
        check_eq(differing, 0);
        rt.release_time();
    });

    s.test("glyph motion: any node's text, glyph by glyph, a function of time (D-68)", [] {
        typeset::GlyphCache glyphs(fonts());
        Runtime plain(options({260, 60}));
        plain.mount("main", column({.padding = 8}, {text({.id = "t", .text = "Hello glyph motion"})}));
        plain.frame({seconds(0), {}});
        const auto rest = draw(plain, glyphs).hash();
        Runtime empty(options({260, 60}));
        empty.mount("main", column({.padding = 8}, {}));
        empty.frame({seconds(0), {}});
        const auto blank = draw(empty, glyphs).hash();

        Runtime rt(options({260, 60}));
        rt.mount("main", column({.padding = 8}, {animate_glyphs(text({.id = "t", .text = "Hello glyph motion"}), GlyphMotion::rise())}));
        rt.frame({seconds(0), {}});
        check(draw(rt, glyphs).hash() == blank, "at its start every word is hidden");
        check(rt.semantic_node(rt.find("main/t")).states.has(State::animating), "and the node is animating");
        rt.frame({seconds(0.3), {}});
        const auto mid = draw(rt, glyphs).hash();
        check(mid != blank && mid != rest, "words on their way");
        rt.frame({seconds(3), {}});
        check(draw(rt, glyphs).hash() == rest, "at its end, the text as it is without motion");
        check(!rt.semantic_node(rt.find("main/t")).states.has(State::animating), "settled");
        // Seeking back renders the same frame as playing to it.
        rt.seek_time(seconds(0.3));
        rt.frame({seconds(3.1), {}});
        check(draw(rt, glyphs).hash() == mid, "seek consistency");
        rt.release_time();
        // Reduced motion: the end at once.
        Runtime calm(options({260, 60}));
        calm.set_reduce_motion(true);
        calm.mount("main", column({.padding = 8}, {animate_glyphs(text({.id = "t", .text = "Hello glyph motion"}), GlyphMotion::gather(7))}));
        calm.frame({seconds(0), {}});
        check(draw(calm, glyphs).hash() == rest, "reduced motion shows the end");
        // Set later (by an agent or a script): it starts at the runtime's time then.
        plain.frame({seconds(1), {}});
        plain.set(plain.find("main/t"), "glyph_motion", to_value(GlyphMotion::typewriter(seconds(0.05))));
        plain.frame({seconds(1), {}});
        check(draw(plain, glyphs).hash() == blank, "the typewriter starts empty");
        plain.frame({seconds(1.12), {}});
        const auto typed = draw(plain, glyphs).hash();
        check(typed != blank && typed != rest, "a few letters typed");
        plain.frame({seconds(9), {}});
        check(draw(plain, glyphs).hash() == rest);
    });

    s.test("pixel effects on any node: its whole drawing, driven by time (D-68)", [] {
        typeset::GlyphCache glyphs(fonts());
        auto card = [] {
            return box({.id = "card", .padding = 10, .background = "accent", .radius = 10},
                       {text({.id = "t", .text = "Burst into particles", .color = "on_accent"})});
        };
        Runtime plain(options({260, 80}));
        plain.mount("main", column({.padding = 8}, {card()}));
        plain.frame({seconds(0), {}});
        const auto rest = draw(plain, glyphs).hash();
        Runtime empty(options({260, 80}));
        empty.mount("main", column({.padding = 8}, {}));
        empty.frame({seconds(0), {}});
        const auto blank = draw(empty, glyphs).hash();

        Runtime rt(options({260, 80}));
        rt.mount("main", column({.padding = 8}, {card()}));
        rt.frame({seconds(0), {}});
        Value fx;
        fx.set("name", "particles").set("duration", 1.0).set("seed", 3.0).set("spread", 50.0).set("ease", "linear");
        rt.set(rt.find("main/card"), "pixel_effect", fx);
        rt.frame({seconds(0), {}});
        check(draw(rt, glyphs).hash() == rest, "progress 0: the node as it is");
        check(rt.semantic_node(rt.find("main/card")).states.has(State::animating));
        rt.frame({seconds(0.4), {}});
        const auto mid = draw(rt, glyphs).hash();
        check(mid != rest && mid != blank, "bursting");
        rt.frame({seconds(1.5), {}});
        check(draw(rt, glyphs).hash() == blank, "gone at the end");
        check(!rt.semantic_node(rt.find("main/card")).states.has(State::animating), "settled");
        rt.seek_time(seconds(0.4));
        rt.frame({seconds(2), {}});
        check(draw(rt, glyphs).hash() == mid, "seek consistency");
        rt.release_time();
        // Backwards: gathered out of the cloud, ending as the node itself.
        rt.frame({seconds(3), {}});
        fx.set("reverse", true);
        rt.set(rt.find("main/card"), "pixel_effect", fx);
        rt.frame({seconds(3), {}});
        check(draw(rt, glyphs).hash() == blank, "reversed: starts gone");
        rt.frame({seconds(5), {}});
        check(draw(rt, glyphs).hash() == rest, "and ends whole");
        // A static effect stays on; null removes it.
        Value blur;
        blur.set("name", "blur").set("radius", 3.0);
        rt.set(rt.find("main/card"), "pixel_effect", blur);
        rt.frame({seconds(5), {}});
        check(draw(rt, glyphs).hash() != rest, "blurred");
        rt.set(rt.find("main/card"), "pixel_effect", Value{});
        rt.frame({seconds(5), {}});
        check(draw(rt, glyphs).hash() == rest, "and back");
    });

    s.test("glyph motion: domains, ranges, orders, randomness, round trips", [] {
        std::vector<render::Glyph> g;
        for (std::uint32_t i = 0; i < 6; ++i) g.push_back({1, {static_cast<float>(i) * 10, 0}, i, i, i / 3, 0});
        const std::array<std::span<const render::Glyph>, 1> one{std::span<const render::Glyph>(g)};
        GlyphMotion m;
        m.domain = GlyphDomain::word;
        m.from = {.y = 10};
        m.to = {};
        m.transition = Transition::tween(seconds(0.2), anim::ease::linear);
        m.stagger = seconds(0.1);
        auto y = [&](const GlyphFrame& f, std::size_t i) { return f.styles[0].empty() ? 0.0f : f.styles[0][i].transform.f; };
        auto f = glyph_frame(m, seconds(0.1), one);
        check(std::abs(y(f, 0) - 5) < 1e-4 && std::abs(y(f, 2) - 5) < 1e-4, "the first word, half way");
        check(std::abs(y(f, 3) - 10) < 1e-4, "the second word, not started");
        check(std::abs(f.ends.count() - 0.3) < 1e-9 && f.moving);
        m.order = GlyphOrder::reverse;
        f = glyph_frame(m, seconds(0.1), one);
        check(std::abs(y(f, 3) - 5) < 1e-4 && std::abs(y(f, 0) - 10) < 1e-4, "reverse: the last word first");
        m.order = GlyphOrder::forward;
        m.begin = 3;
        f = glyph_frame(m, seconds(0), one);
        check(f.styles[0][0].plain() && !f.styles[0][3].plain(), "outside the byte range nothing moves");
        // Seeded randomness: the same seed, the same frame; another seed, another.
        auto r1 = GlyphMotion::gather(1), r2 = GlyphMotion::gather(2);
        const auto a = glyph_frame(r1, seconds(0.05), one), b = glyph_frame(r1, seconds(0.05), one), c = glyph_frame(r2, seconds(0.05), one);
        check(a.styles[0][1].transform == b.styles[0][1].transform && !(a.styles[0][1].transform == c.styles[0][1].transform));
        // Every preset survives the trip through a property value.
        for (const GlyphMotion& p : {GlyphMotion::typewriter(), GlyphMotion::rise(), GlyphMotion::gather(3), GlyphMotion::scatter(4), GlyphMotion::wave()}) {
            const auto back = glyph_motion_from(to_value(p));
            check(back && *back == p, to_value(p).json());
        }
        check(GlyphMotion::wave().repeat == 0 && glyph_frame(GlyphMotion::wave(), seconds(100), one).moving, "a wave never ends");
    });

    return s.run(argc, argv);
}
