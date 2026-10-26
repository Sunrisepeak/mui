// Incremental frames are exact: after any change, drawing only the rows a
// DamageTracker reports over the previous frame gives the same bytes — and
// the same object ids — as drawing the new list from scratch. And the rows
// it reports are few when little changed.
import std;
import openmui.geom;
import openmui.raster;
import openmui.render;
import openmui.testing;

using namespace omui::testing;
namespace render = omui::render;
namespace raster = omui::raster;
namespace geom = omui::geom;

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

struct Pair {
    raster::Surface     incremental{W, H};
    render::CpuRenderer renderer;
    render::DamageTracker tracker;
    render::DisplayList list;
    std::size_t         rows = 0;

    explicit Pair(const Knobs& k) {
        scene(list, k);
        auto spans = tracker.update(list, W, H, 1);
        check(tracker.full() && spans.size() == 1 && spans[0] == render::RowSpan{0, H});
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
        check(std::ranges::equal(incremental.bytes(), full.bytes()), std::format("{}: pixels differ from the full render", what));
        check(std::ranges::equal(renderer.object_ids(), fresh.object_ids()), std::format("{}: object ids differ", what));
    }
};

}  // namespace

int main(int argc, char** argv) {
    Suite s{"render.damage"};

    s.test("every kind of change, incrementally, is the full render byte for byte", [] {
        Knobs k;
        Pair p(k);
        p.step(k, "nothing");
        check_eq(p.rows, std::size_t{0});
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
    });

    s.test("little change, few rows", [] {
        Knobs k;
        Pair p(k);
        k.layer_opacity = 0.6f;  // nodes 6 and 7: rows 139 … 221 and their edges
        p.step(k, "opacity");
        check(p.rows > 0 && p.rows < 100, std::format("{} rows", p.rows));
        k.wedge = {200, 151};
        p.step(k, "a point moved by a pixel");
        check(p.rows > 0 && p.rows < static_cast<std::size_t>(H), std::format("{} rows", p.rows));
    });

    s.test("a new size, a new scale, or invalidate() damage every row", [] {
        Knobs k;
        render::DisplayList dl;
        scene(dl, k);
        render::DamageTracker t;
        (void)t.update(dl, W, H, 1);
        check(t.update(dl, W, H, 1).empty() && !t.full());
        auto all = [&](std::span<const render::RowSpan> r, int h) { return t.full() && r.size() == 1 && r[0] == render::RowSpan{0, h}; };
        check(all(t.update(dl, W, H + 10, 1), H + 10));
        check(all(t.update(dl, W, H + 10, 2), H + 10));
        t.invalidate();
        check(all(t.update(dl, W, H + 10, 2), H + 10));
        check(t.update(dl, W, H + 10, 2).empty());
    });

    s.test("bands of a canvas are the rows of the whole surface, for edges that cross them", [] {
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
                check(same, std::format("rows {}…{} differ", top, bottom));
                check(outside_untouched, std::format("rows {}…{} drew outside", top, bottom));
            }
        }
    });

    s.test("a reused renderer allocates its buffers once", [] {
        // Two renders of the same list; the second reuses the canvas (and
        // gives the same pixels).
        Knobs k;
        render::DisplayList dl;
        scene(dl, k);
        render::CpuRenderer r;
        raster::Surface a(W, H), b(W, H);
        (void)r.render(dl, a, {}, {.background = geom::Color::white()});
        (void)r.render(dl, b, {}, {.background = geom::Color::white()});
        check(std::ranges::equal(a.bytes(), b.bytes()));
    });

    return s.run(argc, argv);
}
