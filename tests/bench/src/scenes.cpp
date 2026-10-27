module openmui.bench;

import std;
import openmui.core;
import openmui.view;
import openmui.semantic;
import openmui.render;
import openmui.raster;
import openmui.text;
import openmui.trace;
import openmui.ui.base;
import openmui.host.headless;
import openmui.anim;
import openmui.markdown;

namespace omui::bench {

namespace {

constexpr Size        full_hd{1920, 1080};
constexpr std::size_t columns = 14;
constexpr std::size_t card_rows = 12;
constexpr std::size_t card_count = columns * card_rows;  // 168 cards of 6 nodes: 1008, and the frame around them

// M1 ⑤'s dashboard: every card has a title, an action, a value and a bar.
struct Dashboard {
    std::vector<Signal<std::int32_t>> values;
    Signal<std::int64_t>              tick = signal(std::int64_t{0});
    Signal<float>                     lift = signal(0.0f);  // every card moves by it, in transition

    Dashboard() {
        values.reserve(card_count);
        for (std::size_t i = 0; i < card_count; ++i) values.push_back(signal(static_cast<std::int32_t>(i * 7 % 100)));
    }

    [[nodiscard]] View view() const {
        std::vector<Element> cards;
        cards.reserve(card_count);
        for (std::size_t i = 0; i < card_count; ++i) {
            const Signal<std::int32_t> v = values[i];
            const Signal<std::int64_t> t = tick;
            cards.push_back(column({.id = std::format("c{}", i), .padding = 6, .gap = 3, .background = "surface", .border_color = "border",
                                    .border_width = 1, .radius = 6, .translate_y = lift,
                                    .animate = {{"translate_y", Transition::tween(seconds(0.5), anim::ease::linear)}}},
                                   {
                                       row({.id = "head", .gap = 4, .align = "center"},
                                           {
                                               text({.id = "title", .text = std::format("Metric {}", i), .style = "label", .grow = 1}),
                                               button({.id = "open", .text = "Open"}),
                                           }),
                                       text({.id = "value", .text = computed([v, t] { return std::format("{} · {}", v.get(), t.get()); }),
                                             .style = "heading"}),
                                       progress({.id = "bar", .value = computed([v] { return static_cast<double>(v.get() % 101) / 100.0; })}),
                                   }));
        }
        return column({.id = "dash", .padding = 8, .gap = 8, .background = "background"},
                      {
                          text({.id = "heading", .text = "Dashboard — 1000+ nodes", .style = "title"}),
                          grid({.id = "cards", .gap = 6, .grow = 1}, std::format("repeat({}, 1fr)", columns), std::move(cards)),
                      });
    }
};

HeadlessOptions scene_options(HeadlessLevel level = HeadlessLevel::pixels) {
    HeadlessOptions o;
    o.level = level;
    o.size = full_hd;
    o.app = "org.openmui.bench";
    return o;
}

// The phase times and the counts of a frame, as counters.
void frame_counters(State& s, const FrameOutput& f) {
    s.counter("nodes", static_cast<double>(f.stats.nodes));
    s.counter("laid_out", static_cast<double>(f.stats.laid_out));
    s.counter("painted", static_cast<double>(f.stats.painted));
    s.counter("layout_ms", f.stats.timings.layout);
    s.counter("paint_ms", f.stats.timings.paint);
    s.counter("runtime_ms", f.stats.timings.total);
}

// The centres of the cards' buttons, for the pointer to sweep.
std::vector<Point> button_centres(Runtime& rt) {
    std::vector<Point> out;
    rt.walk([&](NodeId id, int) {
        if (rt.type(id).name == "button") {
            const Rect b = rt.bounds(id);
            out.push_back({b.x + b.width / 2, b.y + b.height / 2});
        }
    });
    return out;
}

// A long Markdown answer as an assistant writes one: headings, paragraphs with
// inline styles and links, lists, fenced code with blank lines, a table.
std::string markdown_answer(int sections) {
    std::string out;
    for (int i = 0; i < sections; ++i)
        out += std::format(
            "## Step {0}\n\nWe compute **{1}** with `f({0})`, as [the guide](https://example.com/guide#{0}) explains; "
            "the *result* must stay positive.\n\n- check the input\n- call `f`\n- [x] compare with {1}\n\n"
            "```cpp\nauto x = f({0});\n\nassert(x > 0);\n```\n\n| n | f(n) |\n|--:|-----:|\n| {0} | {1} |\n\n",
            i, i * i);
    return out;
}

std::string paragraph_text() {
    std::string s;
    for (int i = 0; i < 12; ++i)
        s += std::format("Paragraph {} — openmui lays text out with HarfBuzz and its own line breaking (UAX #14); numbers like {} and "
                         "punctuation, “quotes”, and a long-wordthatmustbreak keep the shaper honest. ",
                         i, i * 1234.5);
    return s;
}

}  // namespace

SceneFacts reference_scene_facts() {
    register_base_components();
    Dashboard d;
    Headless h(scene_options(HeadlessLevel::layout));
    h.mount("main", d.view());
    const FrameOutput f = h.step();
    return {f.stats.nodes, card_count, full_hd.width, full_hd.height};
}

void add_framework_benchmarks() {
    static bool added = false;
    if (added) return;
    added = true;
    register_base_components();

    // ── M1 ⑤: the 1000-node dashboard at 1080p, through the CPU renderer ──
    add("ui/frame/idle", "nothing changed: the frame the runtime skips", [](State& s) {
        Dashboard d;
        Headless h(scene_options());
        h.mount("main", d.view());
        h.settle();
        (void)h.pixels();
        while (s.next()) {
            const FrameOutput f = h.step();
            (void)h.pixels();
            frame_counters(s, f);
        }
    });
    add("ui/frame/one-label", "M1 ⑤: one card's value changes; relayout, repaint and raster at 1920x1080", [](State& s) {
        Dashboard d;
        Headless h(scene_options());
        h.mount("main", d.view());
        h.settle();
        (void)h.pixels();
        std::size_t i = 0;
        while (s.next()) {
            const Signal<std::int32_t>& v = d.values[i++ % card_count];
            v.set(v.peek() + 1);
            const FrameOutput f = h.step();
            (void)h.pixels();
            frame_counters(s, f);
        }
    });
    add("ui/frame/all-labels", "every card's value changes at once (the worst ordinary frame)", [](State& s) {
        Dashboard d;
        Headless h(scene_options());
        h.mount("main", d.view());
        h.settle();
        (void)h.pixels();
        while (s.next()) {
            d.tick.set(d.tick.peek() + 1);
            const FrameOutput f = h.step();
            (void)h.pixels();
            frame_counters(s, f);
        }
    });
    add("ui/frame/animate-all", "every card moves at once (a transition on all 168): the whole frame is redrawn", [](State& s) {
        Dashboard d;
        Headless h(scene_options());
        h.mount("main", d.view());
        h.settle();
        (void)h.pixels();
        float to = 0;
        while (s.next()) {
            if (h.step().animating == false) {
                to = to == 0 ? 6.0f : 0.0f;  // turn around: another half second of motion
                d.lift.set(to);
            }
            (void)h.pixels();
        }
    });
    add("ui/frame/hover-sweep", "the pointer moves from button to button: hit testing, hover restyle, repaint", [](State& s) {
        Dashboard d;
        Headless h(scene_options());
        h.mount("main", d.view());
        h.settle();
        (void)h.pixels();
        const std::vector<Point> targets = button_centres(h.runtime());
        std::size_t i = 0;
        while (s.next()) {
            h.move(targets[i++ % targets.size()]);
            const FrameOutput f = h.step();
            (void)h.pixels();
            frame_counters(s, f);
        }
    });
    add("ui/layout/resize", "the window's width changes: the whole dashboard is laid out again (no raster)", [](State& s) {
        Dashboard d;
        Headless h(scene_options(HeadlessLevel::layout));
        h.mount("main", d.view());
        h.settle();
        std::size_t i = 0;
        while (s.next()) {
            h.runtime().resize({full_hd.width - static_cast<float>(i++ % 2) * 40, full_hd.height}, 1);
            frame_counters(s, h.step());
        }
    });
    add("ui/raster/full-1080p", "the whole dashboard's display list rasterised by the CPU renderer", [](State& s) {
        Dashboard d;
        Headless h(scene_options());
        h.mount("main", d.view());
        h.settle();
        typeset::GlyphCache glyphs(h.runtime().fonts());
        render::CpuRenderer renderer;
        raster::Surface target(static_cast<int>(full_hd.width), static_cast<int>(full_hd.height));
        render::CpuOptions o;
        o.background = Color::white();
        const render::DisplayList& list = h.runtime().display_list();
        s.counter("commands", static_cast<double>(list.size()));
        while (s.next()) (void)renderer.render(list, target, {&h.runtime().images(), &glyphs}, o);
        // Where the time goes, per kind of command (one measured pass after the timed ones).
        o.measure = true;
        const render::RenderReport r = renderer.render(list, target, {&h.runtime().images(), &glyphs}, o);
        for (std::size_t k = 0; k < r.kinds.size(); ++k)
            if (r.kinds[k].count) s.counter(std::format("{}_ms", render::command_kind_name(k)), r.kinds[k].ms);
    });
    add("ui/raster/full-1080p-parallel", "the same, in bands of 64 rows on the task pool (the same bytes)", [](State& s) {
        Dashboard d;
        Headless h(scene_options());
        h.mount("main", d.view());
        h.settle();
        typeset::GlyphCache glyphs(h.runtime().fonts());
        render::CpuRenderer renderer;
        raster::Surface target(static_cast<int>(full_hd.width), static_cast<int>(full_hd.height));
        render::CpuOptions o;
        o.background = Color::white();
        o.parallel = &raster_parallel();
        s.counter("threads", static_cast<double>(Tasks::shared().threads()));
        while (s.next()) (void)renderer.render(h.runtime().display_list(), target, {&h.runtime().images(), &glyphs}, o);
    });
    add("ui/mount", "the dashboard is built, mounted and laid out (its first frame)", [](State& s) {
        Dashboard d;
        Headless h(scene_options(HeadlessLevel::layout));
        while (s.next()) {
            h.mount("main", d.view());
            frame_counters(s, h.step());
            s.pause();
            h.runtime().unmount("main");
            (void)h.step();
            s.resume();
        }
    });

    // ── what agents and tools read ──
    add("agent/snapshot", "the semantic snapshot of the dashboard, as the JSON an agent receives", [](State& s) {
        Dashboard d;
        Headless h(scene_options(HeadlessLevel::layout));
        h.mount("main", d.view());
        h.settle();
        std::size_t bytes = 0;
        while (s.next()) {
            const std::string json = h.tree().snapshot().to_value().json();
            bytes = json.size();
        }
        s.counter("bytes", static_cast<double>(bytes));
    });

    // ── pieces alone ──
    add("text/paragraph", "shaping and breaking 2 KB of text into lines 600 px wide", [](State& s) {
        typeset::FontCollection fonts;
        if (auto r = fonts.add_defaults(); !r) warn("the default fonts did not load: {}", r.error());
        const std::string source = paragraph_text();
        s.items(source.size());
        while (s.next()) {
            typeset::ParagraphStyle style;
            typeset::Paragraph p(fonts, source, style);
            p.layout(600);
            s.counter("height", static_cast<double>(p.size().height));
        }
    });
    add("markdown/parse", "parsing a 40 KB Markdown answer into the document (md4c and the block / inline model)", [](State& s) {
        const std::string source = markdown_answer(160);
        s.items(source.size());
        while (s.next()) {
            const auto doc = md::parse(source);
            s.counter("blocks", static_cast<double>(doc.root.children.size()));
        }
    });
    add("markdown/stream-token", "appending a 40 KB answer 4 bytes at a time: the cost of one streamed token", [](State& s) {
        const std::string source = markdown_answer(160);
        constexpr std::size_t token = 4;
        s.items(source.size() / token);
        while (s.next()) {
            md::Stream stream;
            for (std::size_t pos = 0; pos < source.size(); pos += token) stream.append(std::string_view(source).substr(pos, token));
            s.counter("frozen_blocks", static_cast<double>(stream.stable_blocks()));
        }
    });
    add("markdown/to-html", "the document of a 40 KB answer back to HTML", [](State& s) {
        const std::string source = markdown_answer(160);
        const auto doc = md::parse(source);
        s.items(source.size());
        while (s.next()) s.counter("bytes", static_cast<double>(md::to_html(doc).size()));
    });
    add("text/glyph-motion", "one frame of glyph motion over 2 KB of laid-out text: every glyph's style (D-68)", [](State& s) {
        typeset::FontCollection fonts;
        if (auto r = fonts.add_defaults(); !r) warn("the default fonts did not load: {}", r.error());
        typeset::Paragraph p(fonts, paragraph_text(), {});
        p.layout(600);
        std::vector<std::span<const render::Glyph>> runs;
        std::size_t n = 0;
        for (const auto& l : p.lines())
            for (const auto& r : l.runs) {
                runs.emplace_back(r.glyphs);
                n += r.glyphs.size();
            }
        const GlyphMotion m = GlyphMotion::gather(3);
        s.items(n);
        double t = 0;
        while (s.next()) {
            const GlyphFrame f = glyph_frame(m, anim::seconds(t += 0.016), runs);
            s.counter("moving", f.moving ? 1 : 0);
        }
    });
    add("render/effect-particles", "a 480 × 320 layer bursting into particles: offscreen, effect, composite (D-68)", [](State& s) {
        render::DisplayList dl;
        const Rect area{20, 20, 480, 320};
        const auto ref = *render::effects().find("particles");
        const std::array<std::pair<std::string_view, float>, 2> given{{{"progress", 0.4f}, {"seed", 2}}};
        const auto params = render::effects().params(ref, given);
        dl.push_layer(1, render::BlendMode::normal, ref, params, area);
        for (int i = 0; i < 12; ++i)
            dl.fill_round_rect({area.x + 10.0f + 38.0f * static_cast<float>(i), area.y + 20, 30, 280}, CornerRadii::all(8),
                               render::Paint::solid(Color::hex(0x4f46e5 + 0x0a0a00 * static_cast<std::uint32_t>(i))));
        dl.pop_layer();
        raster::Surface surf(560, 400);
        render::CpuRenderer cpu;
        s.items(static_cast<std::size_t>(area.width * area.height));
        while (s.next()) {
            const auto r = cpu.render(dl, surf, {}, {.background = Color::white()});
            s.counter("skipped", static_cast<double>(r.effects_skipped));
        }
    });
    add("trace/span-off", "10 000 spans while nothing records: the cost tracing adds to a normal run", [](State& s) {
        (void)trace::stop();
        constexpr std::size_t n = 10'000;
        s.items(n);
        while (s.next())
            for (std::size_t i = 0; i < n; ++i) trace::Span span("bench", "bench");
    });
    add("trace/span-on", "10 000 spans while a profile records", [](State& s) {
        constexpr std::size_t n = 10'000;
        s.items(n);
        while (s.next()) {
            s.pause();
            trace::start({.events_per_thread = 2 * n});
            s.resume();
            for (std::size_t i = 0; i < n; ++i) trace::Span span("bench", "bench");
        }
        (void)trace::stop();
    });
    add("log/filtered-out", "10 000 debug records in a category the filter leaves out", [](State& s) {
        constexpr Log bench_log{"bench"};
        constexpr std::size_t n = 10'000;
        if (auto r = set_log_filter("*=info"); !r) warn("{}", r.error());
        s.items(n);
        while (s.next())
            for (std::size_t i = 0; i < n; ++i) bench_log.debug("record {}", i);
    });
}

}  // namespace omui::bench
