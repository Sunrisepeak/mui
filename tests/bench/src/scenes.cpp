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
                                    .border_width = 1, .radius = 6},
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
