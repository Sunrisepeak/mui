// A percentage-sized wrapping row must report its height at the same used
// width in measurement and placement. These are real Engine trees: no view
// or site dimensions, fonts, snapshots or golden hashes are involved.
import std;
import mlibs.geom;
import mlibs.layout;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace layout = mlibs::layout;
using mlibs::geom::Rect;
using layout::pt;
using layout::pct;

namespace {
layout::Style box(float width, float height) {
    layout::Style s;
    s.width = pt(width); s.height = pt(height);
    return s;
}
void frame(const layout::Engine& e, layout::NodeId id, Rect expected,
           std::source_location where = std::source_location::current()) {
    const Rect actual = e.frame(id);
    expect(std::abs(actual.x - expected.x) < 0.001f && std::abs(actual.y - expected.y) < 0.001f &&
               std::abs(actual.width - expected.width) < 0.001f && std::abs(actual.height - expected.height) < 0.001f,
           where)
        << std::format("actual ({}, {}, {} x {}), expected ({}, {}, {} x {})", actual.x, actual.y, actual.width, actual.height, expected.x,
                       expected.y, expected.width, expected.height);
}
layout::Style header(layout::Direction direction = layout::Direction::row) {
    layout::Style s;
    s.direction = direction; s.wrap = layout::Wrap::wrap; s.align_items = layout::Align::start;
    return s;
}
struct Fixture {
    layout::Engine engine;
    layout::NodeId root, bar, nav, footer;
    std::array<layout::NodeId, 3> items;
    Fixture(layout::Style bar_style, layout::Length nav_width = pct(100), layout::Wrap nav_wrap = layout::Wrap::wrap, float root_gap = 0) {
        layout::Style column;
        column.direction = layout::Direction::column;
        column.align_items = layout::Align::start;
        column.row_gap = root_gap;
        root = engine.create(column); bar = engine.create(std::move(bar_style));
        layout::Style navigation;
        navigation.width = nav_width; navigation.wrap = nav_wrap; navigation.align_items = layout::Align::start;
        navigation.column_gap = 10; navigation.row_gap = 10;
        nav = engine.create(navigation); footer = engine.create(box(10, 10));
        engine.append(root, bar); engine.append(root, footer); engine.append(bar, nav);
        for (auto& item : items) { item = engine.create(box(100, 20)); engine.append(nav, item); }
    }
    void compute(float width) { engine.compute(root, {width, 300}); }
};
}

UTest cases{"layout.percent-wrap", [] {

    "an auto wrapping header capped by max-width reserves all of its percentage child's wrapped lines"_test = [] {
        auto s = header(); s.max_width = pt(230);
        Fixture f(s); f.compute(400);
        frame(f.engine, f.bar, {0, 0, 230, 50});
        frame(f.engine, f.nav, {0, 0, 230, 50});
        frame(f.engine, f.items[0], {0, 0, 100, 20});
        frame(f.engine, f.items[1], {110, 0, 100, 20});
        frame(f.engine, f.items[2], {0, 30, 100, 20});
        frame(f.engine, f.footer, {0, 50, 10, 10});
        expect(near(f.engine.content_size(f.bar).height, 50, 0.001));
    };

    "percentage and point-sized navigation agree after the same containing width is resolved"_test = [] {
        auto s = header(); s.max_width = pt(230);
        Fixture percent(s), points(s, pt(230));
        percent.compute(400); points.compute(400);
        frame(percent.engine, percent.bar, points.engine.frame(points.bar));
        frame(percent.engine, percent.nav, points.engine.frame(points.nav));
        frame(percent.engine, percent.footer, points.engine.frame(points.footer));
        expect(near(points.engine.frame(points.bar).height, 50, 0.001));
    };

    "a min-width expanded auto column remeasures its row rather than reserving obsolete extra lines"_test = [] {
        auto s = header(layout::Direction::column); s.min_width = pt(380);
        Fixture f(s); f.compute(230);
        frame(f.engine, f.bar, {0, 0, 380, 20});
        frame(f.engine, f.nav, {0, 0, 380, 20});
        frame(f.engine, f.items[2], {220, 0, 100, 20});
        frame(f.engine, f.footer, {0, 20, 10, 10});
    };

    "a max-width narrowed auto column reserves newly wrapped lines"_test = [] {
        auto s = header(layout::Direction::column); s.max_width = pt(230);
        Fixture f(s); f.compute(400);
        frame(f.engine, f.bar, {0, 0, 230, 50});
        frame(f.engine, f.nav, {0, 0, 230, 50});
        frame(f.engine, f.footer, {0, 50, 10, 10});
    };

    "definite point and percentage container widths retain the same wrapping"_test = [] {
        for (bool percentage : {false, true}) {
            auto s = header(); s.width = percentage ? pct(60) : pt(240);
            Fixture f(s); f.compute(400);
            frame(f.engine, f.bar, {0, 0, 240, 50});
            frame(f.engine, f.nav, {0, 0, 240, 50});
            frame(f.engine, f.footer, {0, 50, 10, 10});
        }
    };

    "the percentage base is the used content box including padding border and external gaps"_test = [] {
        auto s = header(); s.max_width = pt(250);
        s.padding = mlibs::geom::Insets::all(10); s.border = mlibs::geom::Insets::all(2);
        s.margin.left = 4;
        Fixture f(s, pct(100), layout::Wrap::wrap, 7); f.compute(400);
        frame(f.engine, f.bar, {4, 0, 250, 74});
        frame(f.engine, f.nav, {12, 12, 226, 50});
        frame(f.engine, f.items[2], {0, 30, 100, 20});
        frame(f.engine, f.footer, {0, 81, 10, 10});
    };

    "percentage caps reflow through resize and content changes while unchanged constraints remain cached"_test = [] {
        auto s = header(); s.max_width = pct(60);
        Fixture f(s);
        for (const auto& [width, bar_width, height] : {std::tuple{400.0f, 240.0f, 50.0f}, std::tuple{600.0f, 320.0f, 20.0f},
                                                       std::tuple{300.0f, 180.0f, 80.0f}, std::tuple{400.0f, 240.0f, 50.0f}}) {
            f.compute(width);
            frame(f.engine, f.bar, {0, 0, bar_width, height});
            frame(f.engine, f.footer, {0, height, 10, 10});
        }
        f.engine.reset_stats(); f.compute(400);
        expect(eq(f.engine.stats().layouts, std::uint64_t{0}));
        f.engine.set_style(f.items[0], box(100, 30)); f.compute(400);
        frame(f.engine, f.bar, {0, 0, 240, 60});
        frame(f.engine, f.items[2], {0, 40, 100, 20});
        frame(f.engine, f.footer, {0, 60, 10, 10});
    };

    "nowrap retains one line and its horizontal overflow"_test = [] {
        auto s = header(); s.max_width = pt(230);
        Fixture f(s, pct(100), layout::Wrap::no_wrap); f.compute(400);
        frame(f.engine, f.bar, {0, 0, 230, 20});
        frame(f.engine, f.nav, {0, 0, 230, 20});
        frame(f.engine, f.items[2], {220, 0, 100, 20});
        frame(f.engine, f.footer, {0, 20, 10, 10});
        expect(near(f.engine.content_size(f.nav).width, 320, 0.001));
    };
    // The two-pass correction must stay incremental on deep, non-stretched
    // auto-width trees too. Padding changes the available width at every
    // level; only the outermost container has a max-width. Each nested
    // container has a footer, so a stale height cannot hide in an ancestor.
    for (const std::size_t depth : {4u, 8u, 12u, 16u}) {
        mcpplibs::mtest::test(std::string_view(std::format("deep auto-width percentage wrap has linear work at depth {}", depth))) = [depth] {
            layout::Engine e;
            layout::Style root_style;
            root_style.direction = layout::Direction::column;
            root_style.align_items = layout::Align::start;
            const auto root = e.create(root_style);
            std::vector<layout::NodeId> layers, footers;
            auto parent = root;
            for (std::size_t i = 0; i < depth; ++i) {
                auto s = root_style;
                s.padding = mlibs::geom::Insets::all(2);
                s.row_gap = 3;
                if (i == 0) s.max_width = pt(230 + 4 * static_cast<float>(depth));
                auto layer = e.create(s);
                e.append(parent, layer);
                layers.push_back(layer);
                parent = layer;
            }
            layout::Style navigation;
            navigation.width = pct(100); navigation.wrap = layout::Wrap::wrap;
            navigation.align_items = layout::Align::start;
            navigation.column_gap = 10; navigation.row_gap = 10;
            const auto nav = e.create(navigation);
            e.append(parent, nav);
            std::array<layout::NodeId, 3> items;
            std::array<float, 3> heights{20, 20, 20};
            for (std::size_t i = 0; i < items.size(); ++i) {
                layout::Style item_style; item_style.width = pt(100);
                items[i] = e.create(item_style);
                e.set_measure(items[i], [](void* value, layout::NodeId, std::optional<float> width,
                                          std::optional<float> height, layout::Available, layout::Available) {
                    return mlibs::geom::Size{width.value_or(100), height.value_or(*static_cast<float*>(value))};
                }, &heights[i]);
                e.append(nav, items[i]);
            }
            for (auto layer : layers) {
                const auto footer = e.create(box(20, 7));
                e.append(layer, footer); footers.push_back(footer);
            }
            const auto outside = e.create(box(10, 10));
            e.append(root, outside);
            std::array<layout::Engine::Stats, 3> work;
            const std::array<std::string_view, 3> phases{"cold", "resize", "content-change"};
            auto compute = [&](std::size_t phase, float nav_width, float nav_height) {
                e.reset_stats();
                e.compute(root, {nav_width + 4 * static_cast<float>(depth) + (phase == 0 ? 170.0f : 0.0f), 10000});
                work[phase] = e.stats();
                std::println("percent-wrap depth={} phase={} layouts={} measures={} cache_hits={}",
                             depth, phases[phase], work[phase].layouts, work[phase].measures, work[phase].cache_hits);
                frame(e, nav, {2, 2, nav_width, nav_height});
                for (std::size_t i = 0; i < depth; ++i) {
                    const float child_height = nav_height + 14 * static_cast<float>(depth - i - 1);
                    frame(e, layers[i], {i == 0 ? 0.0f : 2.0f, i == 0 ? 0.0f : 2.0f,
                                        nav_width + 4 * static_cast<float>(depth - i), child_height + 14});
                    frame(e, footers[i], {2, child_height + 5, 20, 7});
                }
                frame(e, outside, {0, nav_height + 14 * static_cast<float>(depth), 10, 10});
            };
            compute(0, 230, 50);
            frame(e, items[2], {0, 30, 100, 20});
            compute(1, 180, 80);
            frame(e, items[2], {0, 60, 100, 20});
            heights[0] = 30; e.mark_dirty(items[0]);
            compute(2, 180, 90);
            frame(e, items[2], {0, 70, 100, 20});
            e.reset_stats(); e.compute(root, {180 + 4 * static_cast<float>(depth), 10000});
            std::println("percent-wrap depth={} phase=unchanged layouts={} measures={} cache_hits={}",
                         depth, e.stats().layouts, e.stats().measures, e.stats().cache_hits);
            expect(eq(e.stats().layouts, std::size_t{0}));
            expect(eq(e.stats().measures, std::size_t{0}));
            expect(eq(e.stats().cache_hits, std::size_t{1}));
            // A node may need several measurement regimes plus placement.
            // Budget at most 12 layouts and 48 cached requests per node, and
            // four leaf measurements per tree node. These grow with node
            // count, not depth squared or elapsed time. Do not raise them to
            // accommodate cache eviction/repeated subtree traversal.
            const std::size_t nodes = 2 * depth + 6;
            for (std::size_t phase = 0; phase < work.size(); ++phase) {
                expect(work[phase].layouts <= 12 * nodes + 24) << std::format("depth {} {}: {} layouts exceed linear budget {}", depth, phases[phase], work[phase].layouts, 12 * nodes + 24);
                expect(work[phase].measures <= 4 * nodes) << std::format("depth {} {}: {} measures exceed linear budget {}", depth, phases[phase], work[phase].measures, 4 * nodes);
                expect(work[phase].cache_hits <= 48 * nodes) << std::format("depth {} {}: {} cache hits exceed linear budget {}", depth, phases[phase], work[phase].cache_hits, 48 * nodes);
            }
        };
    }
}};
