// Incremental frames are exact: after any change, drawing only the rows a
// DamageTracker reports over the previous frame gives the same bytes — and
// the same object ids — as drawing the new list from scratch. And the rows
// it reports are few when little changed.
import std;
import mlibs.geom;
import xrender.raster;
import xrender.vector;
import mlibs.memory;
import mlibs.testing;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
namespace render = xrender;
namespace raster = xrender::raster;
namespace geom = mlibs::geom;

namespace {

constexpr int W = 320, H = 240;

// A scene with knobs: every kind of command, inside nodes, some of them
// nested, clipped, transformed, layered, shadowed, rotated.
struct Knobs {
    geom::Color   accent = geom::Color::hex(0x6366f1);
    float         card_y = 20;
    float         angle = 0.3f;
    float         layer_opacity = 0.8f;
    float         clip_width = 120;
    float         shadow_blur = 12;
    bool          extra = false;   // a node that comes and goes
    bool          swapped = false; // two overlapping siblings in the other order
    float         stroke_width = 3;
    geom::Point   wedge{200, 150};
};

void scene(render::DisplayList& dl, const Knobs& k) {
    dl.clear();
    dl.fill_rect({0, 0, W, H}, geom::Color::hex(0xf8fafc));  // outside every node: the root's
    dl.begin_node(1);
    dl.box_shadow({20, k.card_y, 140, 90}, geom::CornerRadii::all(10), {0, 4}, k.shadow_blur, 0, {0, 0, 0, 0.3f});
    dl.fill_round_rect({20, k.card_y, 140, 90}, geom::CornerRadii::all(10), render::Paint::solid(geom::Color::white()));
    dl.begin_node(2);
    dl.fill_round_rect({30, k.card_y + 10, 120, 24}, geom::CornerRadii::all(12), render::Paint::solid(k.accent));
    dl.end_node();
    dl.end_node();

    // A rotated, clipped group: diagonal edges cross every band boundary.
    dl.begin_node(3);
    dl.push_clip({180, 10, k.clip_width, 110}, geom::CornerRadii::all(8));
    dl.push_transform(geom::Affine::translate(240, 65) * render::rotate(k.angle));
    dl.begin_node(4);
    render::Path star;
    for (int i = 0; i < 10; ++i) {
        const float r = i % 2 ? 20.0f : 48.0f;
        const float a = static_cast<float>(i) * 0.6283185f;
        const geom::Point p{r * std::sin(a), -r * std::cos(a)};
        if (i == 0) star.move_to(p);
        else star.line_to(p);
    }
    star.close();
    dl.fill_path(star, render::FillRule::nonzero, render::Paint::solid(k.accent));
    dl.end_node();
    dl.pop_transform();
    dl.pop_clip();
    dl.end_node();

    // A translucent layer with two overlapping children.
    dl.begin_node(5);
    dl.push_layer(k.layer_opacity);
    auto a = [&] {
        dl.begin_node(6);
        dl.fill_round_rect({30, 140, 90, 60}, geom::CornerRadii::all(6), render::Paint::solid(geom::Color::hex(0xef4444)));
        dl.end_node();
    };
    auto b = [&] {
        dl.begin_node(7);
        dl.fill_rect({70, 160, 90, 60}, geom::Color::hex(0x22c55e));
        dl.end_node();
    };
    if (k.swapped) { b(); a(); } else { a(); b(); }
    dl.pop_layer();
    dl.end_node();

    dl.begin_node(8);
    render::Path wedge;
    wedge.move_to({180, 220}).line_to(k.wedge).cubic_to({250, 120}, {300, 200}, {310, 230}).close();
    dl.stroke_path(wedge, {.width = k.stroke_width, .join = render::LineJoin::miter}, render::Paint::solid(geom::Color::hex(0x0ea5e9)));
    dl.end_node();

    if (k.extra) {
        dl.begin_node(9);
        dl.fill_round_rect({200, 180, 60, 30}, geom::CornerRadii::all(15), render::Paint::solid(geom::Color::hex(0xf59e0b)));
        dl.end_node();
    }
}

// A pool for tests: one thread per task, so bands really run at the same time.
struct Threads final : render::Parallel {
    void run(std::size_t count, mlibs::memory::FunctionRef<void(std::size_t)> body) override {
        std::vector<std::jthread> threads;
        for (std::size_t i = 0; i < count; ++i) threads.emplace_back([&body, i] { body(i); });
    }
};

struct Pair {
    raster::Surface     incremental{W, H};
    render::CpuRenderer renderer;
    render::DamageTracker tracker;
    render::DisplayList list;
    std::size_t         rows = 0;

    explicit Pair(const Knobs& k) {
        scene(list, k);
        auto spans = tracker.update(list, W, H, 1);
        expect(tracker.full() && spans.size() == 1 && spans[0] == render::RowSpan{0, H});
        (void)renderer.render(list, incremental, {}, {.object_ids = true, .background = geom::Color::white(), .rows = spans});
    }

    // Moves to `k` incrementally and compares with a full render of `k`.
    void step(const Knobs& k, std::string_view what) {
        scene(list, k);
        auto spans = tracker.update(list, W, H, 1);
        rows = 0;
        for (auto r : spans) rows += static_cast<std::size_t>(r.bottom - r.top);
        if (!spans.empty())
            (void)renderer.render(list, incremental, {}, {.object_ids = true, .background = geom::Color::white(), .rows = spans});
        raster::Surface full(W, H);
        render::CpuRenderer fresh;
        (void)fresh.render(list, full, {}, {.object_ids = true, .background = geom::Color::white()});
        expect(std::ranges::equal(incremental.bytes(), full.bytes())) << std::format("{}: pixels differ from the full render", what);
        expect(std::ranges::equal(renderer.object_ids(), fresh.object_ids())) << std::format("{}: object ids differ", what);
    }
};

}  // namespace

UTest cases{"render.damage", [] {

    "every kind of change, incrementally, is the full render byte for byte"_test = [] {
        Knobs k;
        Pair p(k);
        p.step(k, "nothing");
        expect(eq(p.rows, std::size_t{0}));
        k.accent = geom::Color::hex(0xec4899);
        p.step(k, "a colour in two nodes");
        k.card_y = 26;
        p.step(k, "a card moved, with its shadow and child");
        k.angle = 0.9f;
        p.step(k, "a rotation under a rounded clip");
        k.clip_width = 90;
        p.step(k, "the clip narrowed");
        k.layer_opacity = 0.5f;
        p.step(k, "a layer's opacity");
        k.swapped = true;
        p.step(k, "two overlapping siblings swapped");
        k.shadow_blur = 24;
        p.step(k, "a wider shadow blur");
        k.extra = true;
        p.step(k, "a node appeared");
        k.extra = false;
        p.step(k, "and went");
        k.stroke_width = 7;
        k.wedge = {170, 100};
        p.step(k, "a stroke's width and a moved point");
    };

    "little change, few rows"_test = [] {
        Knobs k;
        Pair p(k);
        k.layer_opacity = 0.6f;  // nodes 6 and 7: rows 139 … 221 and their edges
        p.step(k, "opacity");
        expect(p.rows > 0 && p.rows < 100) << std::format("{} rows", p.rows);
        k.wedge = {200, 151};
        p.step(k, "a point moved by a pixel");
        expect(p.rows > 0 && p.rows < static_cast<std::size_t>(H)) << std::format("{} rows", p.rows);
    };

    "an image updated in place: its revisions, and only what changed redrawn — exactly"_test = [] {
        render::ImageStore store;
        constexpr int side = 64;
        auto pixels = std::make_shared<raster::Surface>(side, side);
        for (int y = 0; y < side; ++y)
            for (int x = 0; x < side; ++x) pixels->at(x, y) = raster::Rgba8{static_cast<std::uint8_t>(x * 4), static_cast<std::uint8_t>(y * 4), 90, 255};
        const auto image = store.add(std::shared_ptr<const raster::Surface>(pixels));
        expect(eq(store.revision(image), std::uint64_t{0}));
        expect(!store.update(image, std::make_shared<raster::Surface>(side, side + 1))) << "another size is refused";
        expect(!store.update(render::ImageRef{999}, std::make_shared<raster::Surface>(side, side))) << "an image not in the store";
        const render::Resources resources{&store, nullptr};
        const auto draw = [&](render::DisplayList& list, render::Sampling sampling) {
            list.clear();
            list.fill_rect({0, 0, W, H}, geom::Color::hex(0x101820));
            list.begin_node(1);
            list.draw_image(image, {0, 0, side, side}, {20, 20, side * 3, side * 3}, sampling);
            list.end_node();
        };
        for (const auto sampling : {render::Sampling::nearest, render::Sampling::linear}) {
            raster::Surface incremental(W, H);
            render::CpuRenderer renderer;
            render::DamageTracker tracker;
            render::DisplayList list;
            draw(list, sampling);
            (void)renderer.render(list, incremental, resources, {.rows = tracker.update(list, W, H, 1, resources)});
            // Four texels change at (30, 40): drawn ×3 from (20, 20), rows 140 … 152 and their neighbours.
            auto next = std::make_shared<raster::Surface>(*store.get(image));
            for (int y = 40; y < 44; ++y)
                for (int x = 30; x < 34; ++x) next->at(x, y) = raster::Rgba8{255, 255, 255, 255};
            const std::array changed{geom::PixelRect{30, 40, 4, 4}};
            expect(store.update(image, std::shared_ptr<const raster::Surface>(next), changed)) << "updated";
            draw(list, sampling);
            const auto spans = tracker.update(list, W, H, 1, resources);
            std::size_t rows = 0;
            for (auto r : spans) rows += static_cast<std::size_t>(r.bottom - r.top);
            expect(rows > 0 && rows <= 24 && spans.front().top >= 130 && spans.back().bottom <= 162) << std::format("{} rows from {}", rows, spans.empty() ? -1 : spans.front().top);
            (void)renderer.render(list, incremental, resources, {.rows = spans});
            raster::Surface full(W, H);
            render::CpuRenderer fresh;
            (void)fresh.render(list, full, resources);
            expect(std::ranges::equal(incremental.bytes(), full.bytes())) << "the incremental frame is the full one";
            // Unchanged since: nothing redrawn.
            draw(list, sampling);
            expect(tracker.update(list, W, H, 1, resources).empty()) << "nothing changed, nothing redrawn";
        }
        // Updates beyond what the store remembers: all of it, once.
        std::vector<geom::PixelRect> out;
        const auto revision = store.revision(image);
        expect(store.changed_since(image, revision - 1, out) && out.size() == 1 && out[0] == geom::PixelRect{30, 40, 4, 4}) << "the last update's rectangle";
        for (std::size_t k = 0; k < render::ImageStore::remembered + 1; ++k) expect(store.update(image, store.shared(image), std::array{geom::PixelRect{0, 0, 1, 1}}));
        out.clear();
        expect(!store.changed_since(image, revision, out)) << "forgotten: treat all of it as changed";
        expect(store.changed_since(image, store.revision(image), out)) << "up to date";
        // A snapshot store keeps the revision (canvas textures on another thread).
        render::ImageStore snapshot;
        snapshot.put(image, store.shared(image), store.revision(image));
        expect(eq(snapshot.revision(image), store.revision(image)));
    };

    "a new size, a new scale, or invalidate() damage every row"_test = [] {
        Knobs k;
        render::DisplayList dl;
        scene(dl, k);
        render::DamageTracker t;
        (void)t.update(dl, W, H, 1);
        expect(t.update(dl, W, H, 1).empty() && !t.full());
        auto all = [&](std::span<const render::RowSpan> r, int h) { return t.full() && r.size() == 1 && r[0] == render::RowSpan{0, h}; };
        expect(all(t.update(dl, W, H + 10, 1), H + 10));
        expect(all(t.update(dl, W, H + 10, 2), H + 10));
        t.invalidate();
        expect(all(t.update(dl, W, H + 10, 2), H + 10));
        expect(t.update(dl, W, H + 10, 2).empty());
    };

    "bands of a canvas are the rows of the whole surface, for edges that cross them"_test = [] {
        // Paths whose edges cross band boundaries at every angle, filled under
        // both rules, at fractional positions.
        raster::PathBuilder pb;
        for (int i = 0; i < 24; ++i) {
            const float a = static_cast<float>(i) * 0.2617994f;
            const geom::Point c{160.37f + 90 * std::cos(a), 120.61f + 90 * std::sin(a)};
            if (i == 0) pb.move_to(c);
            else pb.line_to({c.x + (i % 3) * 7.3f, c.y - (i % 4) * 5.1f});
        }
        pb.close();
        pb.move_to({-40.5f, 10.25f}).line_to({350.75f, 233.5f}).line_to({20.125f, 239.9f}).close();  // crosses the sides
        for (raster::FillRule rule : {raster::FillRule::nonzero, raster::FillRule::even_odd}) {
            raster::Surface whole(W, H);
            raster::Canvas c(whole);
            c.fill_path(pb.data(), rule, raster::Paint::solid(geom::Color::hex(0x334155)));
            for (auto [top, bottom] : {std::pair{0, 17}, {17, 18}, {50, 121}, {121, 240}, {200, 239}}) {
                raster::Surface band(W, H);
                raster::Canvas bc(band);
                bc.restrict_rows(top, bottom);
                bc.fill_path(pb.data(), rule, raster::Paint::solid(geom::Color::hex(0x334155)));
                bool same = true, outside_untouched = true;
                for (int y = 0; y < H; ++y)
                    for (int x = 0; x < W; ++x) {
                        if (y >= top && y < bottom) same = same && band.at(x, y) == whole.at(x, y);
                        else outside_untouched = outside_untouched && band.at(x, y) == raster::Rgba8{};
                    }
                expect(same) << std::format("rows {}…{} differ", top, bottom);
                expect(outside_untouched) << std::format("rows {}…{} drew outside", top, bottom);
            }
        }
    };

    if constexpr (threads) "bands drawn at the same time give the serial bytes and ids"_test = [] {
        Threads pool;
        Knobs k;
        render::DisplayList dl;
        scene(dl, k);
        raster::Surface serial(W, H), parallel(W, H);
        render::CpuRenderer a, b;
        (void)a.render(dl, serial, {}, {.object_ids = true, .background = geom::Color::white()});
        (void)b.render(dl, parallel, {}, {.object_ids = true, .background = geom::Color::white(), .parallel = &pool, .rows_per_task = 16});
        expect(std::ranges::equal(serial.bytes(), parallel.bytes())) << "whole frame";
        expect(std::ranges::equal(a.object_ids(), b.object_ids())) << "whole frame ids";
        // And incrementally: the damaged rows only, cut into tasks.
        render::DamageTracker t;
        (void)t.update(dl, W, H, 1);
        k.angle = 1.4f;
        k.card_y = 40;
        scene(dl, k);
        const auto rows = t.update(dl, W, H, 1);
        (void)b.render(dl, parallel, {}, {.object_ids = true, .background = geom::Color::white(), .rows = rows, .parallel = &pool, .rows_per_task = 8});
        raster::Surface full(W, H);
        render::CpuRenderer c;
        (void)c.render(dl, full, {}, {.object_ids = true, .background = geom::Color::white()});
        expect(std::ranges::equal(full.bytes(), parallel.bytes())) << "incremental, in parallel";
        expect(std::ranges::equal(c.object_ids(), b.object_ids())) << "incremental ids";
    };

    "a reused renderer allocates its buffers once"_test = [] {
        // Two renders of the same list; the second reuses the canvas (and
        // gives the same pixels).
        Knobs k;
        render::DisplayList dl;
        scene(dl, k);
        render::CpuRenderer r;
        raster::Surface a(W, H), b(W, H);
        (void)r.render(dl, a, {}, {.background = geom::Color::white()});
        (void)r.render(dl, b, {}, {.background = geom::Color::white()});
        expect(std::ranges::equal(a.bytes(), b.bytes()));
    };
}};
